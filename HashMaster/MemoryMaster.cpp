#include "MemoryMaster.h"
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

// Constructor with MemoryMasterConfig
MemoryMaster::MemoryMaster(const MemoryMasterConfig& config)
    : Master(config), _lookup_count(0), _insert_count(0), _delete_count(0), _collision_count(0) {

    log(LOG_INFO, "MemoryMaster created with config: max_records=%d, max_size=%d, hash_count=%d",
        _config._max_record_count, _config._max_record_size, _config._hash_count);
}

// Constructor with base MasterConfig
MemoryMaster::MemoryMaster(const MasterConfig& config)
    : Master(config), _lookup_count(0), _insert_count(0), _delete_count(0), _collision_count(0) {

    log(LOG_INFO, "MemoryMaster created with base config: max_records=%d, max_size=%d",
        _config._max_record_count, _config._max_record_size);
}

// Destructor
MemoryMaster::~MemoryMaster() {
    pthread_mutex_destroy(&_rw_mutex);
}

// Initialize the memory master system
int MemoryMaster::init() {
    if (_initialized) {
        log(LOG_WARNING, "MemoryMaster already initialized");
        return MASTER_OK;
    }

    try {
        // Initialize storage structures
        _records.clear();
        _records.reserve(_config._max_record_count);

        _primary_index.clear();
        _primary_index.reserve(_config._max_record_count);

        _secondary_index.clear();
        _secondary_index.reserve(_config._max_record_count);

        _free_slots.clear();
        _free_slots.reserve(_config._max_record_count);

        // Initialize all slots as free
        for (int i = 0; i < _config._max_record_count; ++i) {
            _records.push_back(nullptr);
            _free_slots.push_back(i);
        }

        // Reset statistics
        _lookup_count = 0;
        _insert_count = 0;
        _delete_count = 0;
        _collision_count = 0;

        _initialized = true;
        log(LOG_INFO, "MemoryMaster initialized successfully");
        return MASTER_OK;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to initialize MemoryMaster: %s", e.what());
        return MASTER_ERROR_MEMORY_ERROR;
    }
}

// Clear all data from memory
int MemoryMaster::clear() {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    try {
        // Clear all data structures
        _records.clear();
        _primary_index.clear();
        _secondary_index.clear();
        _free_slots.clear();

        // Reinitialize
        _records.reserve(_config._max_record_count);
        for (int i = 0; i < _config._max_record_count; ++i) {
            _records.push_back(nullptr);
            _free_slots.push_back(i);
        }

        // Reset statistics
        _lookup_count = 0;
        _insert_count = 0;
        _delete_count = 0;
        _collision_count = 0;

        log(LOG_INFO, "MemoryMaster cleared successfully");
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_OK;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to clear MemoryMaster: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_ERROR_MEMORY_ERROR;
    }
}

// Store a record with primary and secondary keys
int MemoryMaster::put(const char* pkey, const char* skey, const char* record, int record_size) {
    if (!_initialized) {
        log(LOG_ERROR, "MemoryMaster not initialized");
        return MASTER_ERROR_NOT_INITIALIZED;
    }

    if (!is_valid_key(pkey) || !record || record_size <= 0 || record_size > _config._max_record_size) {
        log(LOG_ERROR, "Invalid parameters for put operation");
        return MASTER_ERROR_INVALID_PARAMETER;
    }

    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    try {
        std::string primary_key(pkey);
        std::string secondary_key = skey ? std::string(skey) : "";

        // Check if primary key already exists
        auto primary_it = _primary_index.find(primary_key);
        if (primary_it != _primary_index.end()) {
            log(LOG_ERROR, "Primary key already exists: %s", pkey);
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return MASTER_ERROR_KEY_EXISTS;
        }

        // Check if secondary key already exists (if provided)
        if (!secondary_key.empty()) {
            auto secondary_it = _secondary_index.find(secondary_key);
            if (secondary_it != _secondary_index.end()) {
                log(LOG_ERROR, "Secondary key already exists: %s", skey);
                if (_config._use_lock) {
                    pthread_mutex_unlock(&_rw_mutex);
                }
                return MASTER_ERROR_KEY_EXISTS;
            }
        }

        // Find free slot
        int slot = find_free_slot();
        if (slot == -1) {
            log(LOG_ERROR, "No free slots available");
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return MASTER_ERROR_NO_SPACE;
        }

        // Create and store the record
        auto memory_record = std::make_unique<MemoryRecord>(record, record_size, primary_key, secondary_key);
        _records[slot] = std::move(memory_record);

        // Update indices
        _primary_index[primary_key] = slot;
        if (!secondary_key.empty()) {
            _secondary_index[secondary_key] = slot;
        }

        update_statistics_on_insert();

        log(LOG_DEBUG, "Put record: pkey=%s, skey=%s, size=%d, slot=%d",
            pkey, skey ? skey : "(null)", record_size, slot);

        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_OK;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to put record: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_ERROR_MEMORY_ERROR;
    }
}

// Retrieve record by primary key
char* MemoryMaster::get_by_primary(const char* pkey) {
    if (!_initialized || !is_valid_key(pkey)) {
        return nullptr;
    }

    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    update_statistics_on_lookup();

    try {
        std::string primary_key(pkey);
        auto it = _primary_index.find(primary_key);

        if (it != _primary_index.end() && _records[it->second] && _records[it->second]->is_valid) {
            char* result = _records[it->second]->data.data();
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return result;
        }

        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return nullptr;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to get record by primary key: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return nullptr;
    }
}

// Retrieve record by secondary key
char* MemoryMaster::get_by_secondary(const char* skey) {
    if (!_initialized || !is_valid_key(skey)) {
        return nullptr;
    }

    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    update_statistics_on_lookup();

    try {
        std::string secondary_key(skey);
        auto it = _secondary_index.find(secondary_key);

        if (it != _secondary_index.end() && _records[it->second] && _records[it->second]->is_valid) {
            char* result = _records[it->second]->data.data();
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return result;
        }

        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return nullptr;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to get record by secondary key: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return nullptr;
    }
}

// Delete record by primary key
int MemoryMaster::del(const char* pkey) {
    if (!_initialized || !is_valid_key(pkey)) {
        return MASTER_ERROR_INVALID_PARAMETER;
    }

    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    try {
        std::string primary_key(pkey);
        auto primary_it = _primary_index.find(primary_key);

        if (primary_it == _primary_index.end()) {
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return MASTER_ERROR_KEY_NOT_FOUND;
        }

        int slot = primary_it->second;
        auto& record = _records[slot];

        if (!record || !record->is_valid) {
            if (_config._use_lock) {
                pthread_mutex_unlock(&_rw_mutex);
            }
            return MASTER_ERROR_KEY_NOT_FOUND;
        }

        // Remove from secondary index if exists
        if (!record->secondary_key.empty()) {
            _secondary_index.erase(record->secondary_key);
        }

        // Remove from primary index
        _primary_index.erase(primary_it);

        // Free the slot
        free_slot(slot);

        update_statistics_on_delete();

        log(LOG_DEBUG, "Deleted record: pkey=%s, slot=%d", pkey, slot);

        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_OK;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to delete record: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return MASTER_ERROR_MEMORY_ERROR;
    }
}

// Get system statistics
MasterStats MemoryMaster::get_statistics() {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    MasterStats stats = {};
    stats.total_records = _config._max_record_count;
    stats.free_records = static_cast<int>(_free_slots.size());
    stats.used_records = stats.total_records - stats.free_records;
    stats.record_utilization = static_cast<double>(stats.used_records) / stats.total_records;

    if (_config._use_lock) {
        pthread_mutex_unlock(&_rw_mutex);
    }
    return stats;
}

// Display statistics to stdout
void MemoryMaster::display_statistics() {
    auto stats = get_memory_statistics();

    printf("=== MemoryMaster Statistics ===\n");
    printf("Total records: %d\n", stats.total_records);
    printf("Used records: %d\n", stats.used_records);
    printf("Free records: %d\n", stats.free_records);
    printf("Record utilization: %.2f%%\n", stats.record_utilization * 100);
    printf("Memory usage: %zu bytes\n", stats.memory_usage_bytes);
    printf("Lookup count: %d\n", stats.lookup_count);
    printf("Insert count: %d\n", stats.insert_count);
    printf("Delete count: %d\n", stats.delete_count);
    printf("Collision count: %d\n", stats.collision_count);
    printf("Hit rate: %.2f%%\n", stats.hit_rate * 100);
    printf("Primary load factor: %.2f\n", stats.load_factor_primary);
    printf("Secondary load factor: %.2f\n", stats.load_factor_secondary);
}

// Get current record count
int MemoryMaster::get_record_count() const {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }
    int result = _config._max_record_count - static_cast<int>(_free_slots.size());
    if (_config._use_lock) {
        pthread_mutex_unlock(&_rw_mutex);
    }
    return result;
}

// Get free record count
int MemoryMaster::get_free_record_count() const {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }
    int result = static_cast<int>(_free_slots.size());
    if (_config._use_lock) {
        pthread_mutex_unlock(&_rw_mutex);
    }
    return result;
}

// Validate system integrity
bool MemoryMaster::validate_integrity() {
    if (!_initialized) {
        return false;
    }

    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    try {
        // Check that indices are consistent with records
        for (const auto& pair : _primary_index) {
            int slot = pair.second;
            if (slot < 0 || slot >= _config._max_record_count) {
                if (_config._use_lock) {
                    pthread_mutex_unlock(&_rw_mutex);
                }
                return false;
            }

            auto& record = _records[slot];
            if (!record || !record->is_valid || record->primary_key != pair.first) {
                if (_config._use_lock) {
                    pthread_mutex_unlock(&_rw_mutex);
                }
                return false;
            }
        }

        for (const auto& pair : _secondary_index) {
            int slot = pair.second;
            if (slot < 0 || slot >= _config._max_record_count) {
                if (_config._use_lock) {
                    pthread_mutex_unlock(&_rw_mutex);
                }
                return false;
            }

            auto& record = _records[slot];
            if (!record || !record->is_valid || record->secondary_key != pair.first) {
                if (_config._use_lock) {
                    pthread_mutex_unlock(&_rw_mutex);
                }
                return false;
            }
        }

        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return true;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Integrity validation failed: %s", e.what());
        if (_config._use_lock) {
            pthread_mutex_unlock(&_rw_mutex);
        }
        return false;
    }
}

// Get detailed memory master statistics
MemoryMaster::MemoryMasterStats MemoryMaster::get_memory_statistics() const {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    MemoryMasterStats stats = {};

    // Base statistics
    stats.total_records = _config._max_record_count;
    stats.free_records = static_cast<int>(_free_slots.size());
    stats.used_records = stats.total_records - stats.free_records;
    stats.record_utilization = static_cast<double>(stats.used_records) / stats.total_records;

    // Extended statistics
    stats.lookup_count = _lookup_count;
    stats.insert_count = _insert_count;
    stats.delete_count = _delete_count;
    stats.collision_count = _collision_count;

    // Calculate hit rate
    if (_lookup_count > 0) {
        stats.hit_rate = static_cast<double>(_lookup_count - _collision_count) / _lookup_count;
    } else {
        stats.hit_rate = 0.0;
    }

    // Calculate memory usage
    stats.memory_usage_bytes = estimate_memory_usage();

    // Calculate load factors
    stats.load_factor_primary = static_cast<double>(_primary_index.size()) / _config._hash_count;
    stats.load_factor_secondary = static_cast<double>(_secondary_index.size()) / _config._hash_count;

    if (_config._use_lock) {
        pthread_mutex_unlock(&_rw_mutex);
    }
    return stats;
}

// Reset all statistics counters
void MemoryMaster::reset_statistics() {
    if (_config._use_lock) {
        pthread_mutex_lock(&_rw_mutex);
    }

    _lookup_count = 0;
    _insert_count = 0;
    _delete_count = 0;
    _collision_count = 0;

    if (_config._use_lock) {
        pthread_mutex_unlock(&_rw_mutex);
    }
}

// Estimate memory usage in bytes
size_t MemoryMaster::estimate_memory_usage() const {
    size_t total_size = 0;

    // Size of record storage
    for (const auto& record : _records) {
        if (record && record->is_valid) {
            total_size += record->data.size();
            total_size += record->primary_key.size();
            total_size += record->secondary_key.size();
            total_size += sizeof(MemoryRecord);
        }
    }

    // Size of indices (approximation)
    total_size += _primary_index.size() * (sizeof(std::string) + sizeof(int));
    total_size += _secondary_index.size() * (sizeof(std::string) + sizeof(int));
    total_size += _free_slots.size() * sizeof(int);

    return total_size;
}

// Private helper methods
int MemoryMaster::find_free_slot() {
    if (_free_slots.empty()) {
        return -1;
    }

    int slot = _free_slots.back();
    _free_slots.pop_back();
    return slot;
}

void MemoryMaster::free_slot(int index) {
    if (index >= 0 && index < _config._max_record_count) {
        _records[index].reset();
        _free_slots.push_back(index);
    }
}

bool MemoryMaster::is_valid_key(const char* key) const {
    return key != nullptr && strlen(key) > 0;
}

void MemoryMaster::update_statistics_on_insert() {
    ++_insert_count;
}

void MemoryMaster::update_statistics_on_lookup() const {
    ++_lookup_count;
}

void MemoryMaster::update_statistics_on_delete() {
    ++_delete_count;
}

// Logging implementation
void MemoryMaster::log(LogLevel level, const char* format, ...) {
    if (level < _config._log_level) {
        return;
    }

    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    printf("[%s] MemoryMaster: ", level_str[level]);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}

// Memory Iterator Implementation
MemoryMaster::MemoryIterator::MemoryIterator(const MemoryMaster* master)
    : Iterator(nullptr, 0), _memory_master(master), _current_pos(0) {

    // Build list of valid indices
    for (int i = 0; i < master->_config._max_record_count; ++i) {
        if (master->_records[i] && master->_records[i]->is_valid) {
            _valid_indices.push_back(i);
        }
    }
}

bool MemoryMaster::MemoryIterator::has_next() {
    return _current_pos < _valid_indices.size();
}

char* MemoryMaster::MemoryIterator::next() {
    if (!has_next()) {
        return nullptr;
    }

    int index = _valid_indices[_current_pos++];
    return _memory_master->_records[index]->data.data();
}

std::unique_ptr<Master::Iterator> MemoryMaster::create_iterator() {
    return std::make_unique<MemoryIterator>(this);
}

// Factory function
std::unique_ptr<Master> create_memory_master(const MasterConfig& config) {
    return std::make_unique<MemoryMaster>(config);
}