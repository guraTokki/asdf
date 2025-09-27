#include "HashMaster.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include "common/YAMLParser.h"

// Constructor
HashMaster::HashMaster(const HashMasterConfig& config)
    : Master(config), _records_fd(-1), 
      _records_addr(MAP_FAILED), _storage_size(0), _record_entry_addr(nullptr),
      _htmaster_header(nullptr),  _total_records(0), _free_records(0) {
    
    if (!_config.validate()) {
        log(LOG_ERROR, "Invalid HashMaster configuration");
        return;
    }
    
    // Initialize record storage size
    _record_entry_size = sizeof(DataRecordEntry) + _config._max_record_size;
    _storage_size = sizeof(HashMasterHeader) + _config._max_record_count * _record_entry_size;
    
    // Initialize locks
    memset(&_master_rwlock, 0, sizeof(_master_rwlock));
    
    log(LOG_INFO, "HashMaster created with config: max_records=%d, max_size=%d, hash_count=%d",
        _config._max_record_count, _config._max_record_size, _config._hash_count);
}

// Destructor
HashMaster::~HashMaster() {
    log(LOG_INFO, "HashMaster destructor called");
    
    // Clean up hash tables
    _primary_hash_table.reset();
    _secondary_hash_table.reset();
    
    // Clean up record storage
    cleanup_record_storage();
    
    // Destroy locks
    destroy_master_locks();
}

// Initialization
int HashMaster::init() {
    if (_initialized) {
        log(LOG_WARNING, "HashMaster already initialized");
        return HASH_OK;
    }
    
    // Initialize master locks
    if (_config._use_lock) {
        int ret = init_master_locks();
        if (ret != HASH_OK) {
            log(LOG_ERROR, "Failed to initialize master locks: %d", ret);
            return ret;
        }
    }
    
    // Create hash tables
    _primary_hash_table = std::unique_ptr<HashTable>(new HashTable(
        _config._hash_count, _config._primary_field_len, _config._max_record_count,
        _config._use_lock, (_config._filename + "_primary").c_str(), true
    ));

    if(_config._secondary_field_len > 0) {
        _secondary_hash_table = std::unique_ptr<HashTable>(new HashTable(
            _config._hash_count, _config._secondary_field_len, _config._max_record_count,
            _config._use_lock, (_config._filename + "_secondary").c_str(), true
    ));
    } else {
        _secondary_hash_table = nullptr;
    }
    
    // Set log levels
    _primary_hash_table->setLogLevel(_config._log_level);
    
    // Initialize hash tables
    int ret = _primary_hash_table->init();
    if (ret != HASH_OK) {
        log(LOG_ERROR, "Failed to initialize primary hash table: %d", ret);
        return ret;
    }

    if(_secondary_hash_table != nullptr) {
        _secondary_hash_table->setLogLevel(_config._log_level);
        
        ret = _secondary_hash_table->init();
        if (ret != HASH_OK) {
            log(LOG_ERROR, "Failed to initialize secondary hash table: %d", ret);
            return ret;
        }
    }
    
    
    // Allocate record storage
    ret = allocate_record_storage();
    if (ret != HASH_OK) {
        log(LOG_ERROR, "Failed to allocate record storage: %d", ret);
        return ret;
    }
    
    // Initialize record management
    // 마스터는 공유되므로 지우는 것은 명시적으로 하는 것으로
    // clear();
    
    _initialized = true;
    log(LOG_INFO, "HashMaster initialized successfully");
    return HASH_OK;
}

// Clear all data
int HashMaster::clear() {
    if (_config._use_lock) {
        pthread_rwlock_wrlock(&_master_rwlock);
    }
    
    // Clear hash tables
    if (_primary_hash_table) {
        _primary_hash_table->clear();
    }
    if (_secondary_hash_table) {
        _secondary_hash_table->clear();
    }
    
    // Initialize record headers and free list
    _htmaster_header->_first_free_record = 0;
    _htmaster_header->_max_record_count = _config._max_record_count;
    _htmaster_header->_max_record_size = _config._max_record_size;
    _htmaster_header->_storage_size = _storage_size;
    _htmaster_header->_hash_count = _config._hash_count;
    _htmaster_header->_primary_field_len = _config._primary_field_len;
    _htmaster_header->_secondary_field_len = _config._secondary_field_len;
    _htmaster_header->_use_lock = _config._use_lock;

    _total_records = 0;
    _free_records = _config._max_record_count;
    
    for (int i = 0; i < _config._max_record_count; i++) {
        DataRecordEntry *record_entry = get_record_entry(i);
        record_entry->_occupied = false;
        // Set up free list
        record_entry->_nextEmpty = (i == _config._max_record_count - 1) ? -1 : i + 1;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    log(LOG_INFO, "HashMaster cleared successfully");
    return HASH_OK;
}

// Allocate record storage
int HashMaster::allocate_record_storage() {
    std::string records_filename = "mmap/" + _config._filename + "_records.dat";
    
    // Create records file
    _records_fd = open(records_filename.c_str(), O_RDWR | O_CREAT, 0644);
    if (_records_fd == -1) {
        log(LOG_ERROR, "Failed to create records file %s: %s", 
            records_filename.c_str(), strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Set file size
    if (ftruncate(_records_fd, _storage_size) == -1) {
        log(LOG_ERROR, "Failed to set records file size: %s", strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Map header + records file
    _records_addr = mmap(NULL, _storage_size, PROT_READ | PROT_WRITE, MAP_SHARED, _records_fd, 0);
    if (_records_addr == MAP_FAILED) {
        log(LOG_ERROR, "Failed to map records file: %s", strerror(errno));
        return HASH_ERROR_MEMORY_ERROR;
    }
    
    // Set up pointers
    _htmaster_header = (HashMasterHeader*)_records_addr;
    _record_entry_addr = (char *)(_records_addr + sizeof(HashMasterHeader));
    
    // Initialize header if this is a new file (all zeros)
    if (_htmaster_header->_max_record_count == 0) {
        _htmaster_header->_first_free_record = 0;
        _htmaster_header->_max_record_count = _config._max_record_count;
        _htmaster_header->_max_record_size = _config._max_record_size;
        _htmaster_header->_storage_size = _storage_size;
        _htmaster_header->_hash_count = _config._hash_count;
        _htmaster_header->_primary_field_len = _config._primary_field_len;
        _htmaster_header->_secondary_field_len = _config._secondary_field_len;
        _htmaster_header->_use_lock = _config._use_lock;
        
        log(LOG_INFO, "Initialized new HashMaster header with config");
    } else {
        log(LOG_INFO, "Using existing HashMaster header");
    }
    
    log(LOG_INFO, "Record storage allocated: %zu bytes", _storage_size);
    return HASH_OK;
}

// Cleanup record storage
void HashMaster::cleanup_record_storage() {
    if (_records_addr != MAP_FAILED) {
        if (munmap(_records_addr, _storage_size) == -1) {
            log(LOG_ERROR, "Failed to unmap records: %s", strerror(errno));
        }
        _records_addr = MAP_FAILED;
    }
    
    if (_records_fd != -1) {
        close(_records_fd);
        _records_fd = -1;
    }
    
    _record_entry_addr = nullptr;
    _htmaster_header = nullptr;
}

// Initialize master locks
int HashMaster::init_master_locks() {
    pthread_rwlockattr_t attr;
    int ret = pthread_rwlockattr_init(&attr);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to initialize master rwlock attributes: %s", strerror(ret));
        return HASH_ERROR_LOCK_ERROR;
    }
    
    ret = pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to set master rwlock process shared: %s", strerror(ret));
        pthread_rwlockattr_destroy(&attr);
        return HASH_ERROR_LOCK_ERROR;
    }
    
    ret = pthread_rwlock_init(&_master_rwlock, &attr);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to initialize master rwlock: %s", strerror(ret));
        pthread_rwlockattr_destroy(&attr);
        return HASH_ERROR_LOCK_ERROR;
    }
    
    pthread_rwlockattr_destroy(&attr);
    return HASH_OK;
}

// Destroy master locks
void HashMaster::destroy_master_locks() {
    if (_config._use_lock) {
        int ret = pthread_rwlock_destroy(&_master_rwlock);
        if (ret != 0) {
            log(LOG_ERROR, "Failed to destroy master rwlock: %s", strerror(ret));
        }
    }
}

// Get record data
char* HashMaster::get_record_data(int index) {
    if (!validate_record_index(index)) {
        return nullptr;
    }
    return get_record_entry(index)->_value;
}

// Find free record
int HashMaster::find_free_record() {
    
    if (_htmaster_header->_first_free_record == -1) {
        return -1;  // No free records
    }
    return _htmaster_header->_first_free_record;
}

// Free record
void HashMaster::free_record(int index) {
    if (!validate_record_index(index)) {
        return;
    }
    
    // Add to free list
    DataRecordEntry *re = get_record_entry(index);
    re->_occupied = false;
    re->_nextEmpty = _htmaster_header->_first_free_record;
    _htmaster_header->_first_free_record = index;
    _free_records++;
}

// Put operation
int HashMaster::put(const char* pkey, const char* skey, const char* record, int record_size) {
    if (!_initialized) {
        log(LOG_ERROR, "HashMaster not initialized");
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (!validate_keys(pkey, skey) || !record || record_size <= 0 || record_size > _config._max_record_size) {
        log(LOG_ERROR, "Invalid parameters for put operation");
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_wrlock(&_master_rwlock);
    }
    
    int result = HASH_OK;
    
    do {
        // Check if primary key already exists
        int existing_index = _primary_hash_table->get(pkey);
        if (existing_index != HASH_ERROR_KEY_NOT_FOUND) {
            log(LOG_ERROR, "Primary key already exists: %s", pkey);
            result = HASH_ERROR_KEY_EXISTS;
            break;
        }
        
        // Find free record
        int record_index = _htmaster_header->_first_free_record;
        if (record_index == -1) {
            log(LOG_ERROR, "No free records available");
            result = HASH_ERROR_NO_SPACE;
            break;
        }
        
        DataRecordEntry *re = get_record_entry(record_index);
        _htmaster_header->_first_free_record = re->_nextEmpty;
        re->_occupied = true;
        re->_nextEmpty = -1;
        memcpy(re->_value,  record, record_size);
        
        // Add to primary hash table
        int ret = _primary_hash_table->put(pkey, record_index);
        if (ret != HASH_OK) {
            log(LOG_ERROR, "Failed to add primary key: %d", ret);
            free_record(record_index);
            result = ret;
            break;
        }
        
        // Add to secondary hash table only if secondary indexing is enabled and key is provided
        if(_config.use_secondary_index() && _secondary_hash_table != nullptr && skey && strlen(skey) > 0) {
            ret = _secondary_hash_table->put(skey, record_index);
            if (ret != HASH_OK) {
                log(LOG_ERROR, "Failed to add secondary key: %d", ret);
                _primary_hash_table->del(pkey);
                free_record(record_index);
                result = ret;
                break;
            }
        }
        
        _total_records++;
        log(LOG_DEBUG, "Put record: pkey=%s, skey=%s, size=%d, index=%d", pkey, skey ? skey : "(null)", record_size, record_index);
        
    } while(0);
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return result;
}

// Get by field index
char* HashMaster::get(int field_index, const char* key) {
    if (field_index == 0) {
        return get_by_primary(key);
    } else if (field_index == 1) {
        return get_by_secondary(key);
    }
    return nullptr;
}

// Get by primary key
char* HashMaster::get_by_primary(const char* pkey) {
    if (!_initialized || !pkey) {
        return nullptr;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_rdlock(&_master_rwlock);
    }
    
    char* result = nullptr;
    
    int record_index = _primary_hash_table->get(pkey);
    if (record_index != HASH_ERROR_KEY_NOT_FOUND) {
        result = get_record_entry(record_index)->_value;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return result;
}

// Get by secondary key
char* HashMaster::get_by_secondary(const char* skey) {
    if (!_initialized || !skey) {
        return nullptr;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_rdlock(&_master_rwlock);
    }
    
    char* result = nullptr;
    
    int record_index = _secondary_hash_table->get(skey);
    if (record_index != HASH_ERROR_KEY_NOT_FOUND) {
        result = get_record_entry(record_index)->_value;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return result;
}

// Delete operation
int HashMaster::del(const char* pkey) {
    if (!_initialized || !pkey) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_wrlock(&_master_rwlock);
    }
    
    int result = HASH_OK;
    
    do {
        // Find record by primary key
        int record_index = _primary_hash_table->get(pkey);
        if (record_index == HASH_ERROR_KEY_NOT_FOUND) {
            result = HASH_ERROR_KEY_NOT_FOUND;
            break;
        }
        
        // Remove from primary hash table
        int ret = _primary_hash_table->del(pkey);
        if (ret != HASH_OK) {
            log(LOG_ERROR, "Failed to delete from primary hash table: %d", ret);
            result = ret;
            break;
        }

        // Find and delete from secondary hash table if it exists
        if(_secondary_hash_table != nullptr) {
            char secondary_key[_config._secondary_field_len];
            int find_ret = _secondary_hash_table->find_key_by_data_index(record_index, secondary_key);
            if (find_ret == HASH_OK) {
                int del_ret = _secondary_hash_table->del(secondary_key);
                if (del_ret != HASH_OK) {
                    log(LOG_ERROR, "Failed to delete from secondary hash table: %d", del_ret);
                    // Note: We continue with deletion even if secondary deletion fails
                    // The primary deletion should still succeed for data consistency
                }
                else {
                    log(LOG_DEBUG, "Successfully deleted secondary key: %s", secondary_key);
                }
            }
            else {
                log(LOG_WARNING, "Secondary key not found for record_index %d", record_index);
            }
        }
        
        // Free the record
        free_record(record_index);
        _total_records--;
        
        log(LOG_DEBUG, "Deleted record: pkey=%s, index=%d", pkey, record_index);
        
    } while(0);
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return result;
}

// Add record (allocate space and return pointer)
char* HashMaster::add_record(const char* pkey, const char* skey, int record_size) {
    if (!_initialized) {
        return nullptr;
    }
    
    if (!validate_keys(pkey, skey) || record_size <= 0 || record_size > _config._max_record_size) {
        return nullptr;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_wrlock(&_master_rwlock);
    }
    
    char* result = nullptr;
    
    DataRecordEntry *entry = nullptr;
    do {
        // Check if primary key already exists
        int existing_index = _primary_hash_table->get(pkey);
        if (existing_index != HASH_ERROR_KEY_NOT_FOUND) {
            break;
        }
        
        // Find free record
        int record_index = _htmaster_header->_first_free_record;
        if (record_index == -1) {
            break;
        }
        entry = get_record_entry(record_index);
        _htmaster_header->_first_free_record = entry->_nextEmpty;
        entry->_occupied = true;
        entry->_nextEmpty = -1;
        
        // Add to hash tables
        if (_primary_hash_table->put(pkey, record_index) != HASH_OK) {
            free_record(record_index);
            break;
        }
        
        // Add to secondary hash table only if secondary indexing is enabled and key is provided
        if (_config.use_secondary_index() && skey && strlen(skey) > 0) {
            if (_secondary_hash_table->put(skey, record_index) != HASH_OK) {
                _primary_hash_table->del(pkey);
                free_record(record_index);
                break;
            }
        }
        
        _total_records++;
        result = entry->_value;
        
    } while(0);
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return result;
}

// Get statistics (Master interface implementation)
MasterStats HashMaster::get_statistics() {
    auto hash_stats = get_hash_master_statistics();
    MasterStats stats = {};
    stats.total_records = hash_stats.total_records;
    stats.free_records = hash_stats.free_records;
    stats.used_records = hash_stats.used_records;
    stats.record_utilization = hash_stats.record_utilization;
    return stats;
}

// Get HashMaster-specific statistics
HashMaster::HashMasterStats HashMaster::get_hash_master_statistics() {
    HashMasterStats stats = {};
    
    if (!_initialized) {
        return stats;
    }
    
    if (_config._use_lock) {
        pthread_rwlock_rdlock(&_master_rwlock);
    }
    
    stats.total_records = _config._max_record_count;
    stats.free_records = _free_records;
    stats.used_records = _total_records;
    stats.record_utilization = (double)_total_records / _config._max_record_count;
    
    if (_primary_hash_table) {
        stats.primary_stats = _primary_hash_table->get_statistics();
    }
    
    if (_secondary_hash_table) {
        stats.secondary_stats = _secondary_hash_table->get_statistics();
    }
    
    if (_config._use_lock) {
        pthread_rwlock_unlock(&_master_rwlock);
    }
    
    return stats;
}

// Display statistics
void HashMaster::display_statistics() {
    HashMasterStats stats = get_hash_master_statistics();
    
    printf("=== HashMaster Statistics ===\n");
    printf("Total records: %d\n", stats.total_records);
    printf("Used records: %d\n", stats.used_records);
    printf("Free records: %d\n", stats.free_records);
    printf("Record utilization: %.2f%%\n", stats.record_utilization * 100);
    
    printf("\n--- Primary Hash Table ---\n");
    printf("Used slots: %d\n", stats.primary_stats.used_slots);
    printf("Load factor: %.2f%%\n", stats.primary_stats.load_factor * 100);
    printf("Max chain length: %d\n", stats.primary_stats.max_chain_length);
    
    printf("\n--- Secondary Hash Table ---\n");
    printf("Used slots: %d\n", stats.secondary_stats.used_slots);
    printf("Load factor: %.2f%%\n", stats.secondary_stats.load_factor * 100);
    printf("Max chain length: %d\n", stats.secondary_stats.max_chain_length);
}

// Display hash tables
void HashMaster::display_hashtable() const {
    if (_primary_hash_table) {
        printf("=== Primary Hash Table ===\n");
        _primary_hash_table->display_hashtable();
    }
    
    if (_secondary_hash_table) {
        printf("\n=== Secondary Hash Table ===\n");
        _secondary_hash_table->display_hashtable();
    }
}

// Validation methods
bool HashMaster::validate_record_index(int index) {
    if (index < 0 || index >= _config._max_record_count) {
        log(LOG_ERROR, "Invalid record index: %d (valid range: 0-%d)", 
            index, _config._max_record_count - 1);
        return false;
    }
    return true;
}

bool HashMaster::validate_keys(const char* pkey, const char* skey) {
    if (!pkey) {
        log(LOG_ERROR, "Primary key cannot be null");
        return false;
    }

    if (strlen(pkey) >= _config._primary_field_len) {
        log(LOG_ERROR, "Primary key length exceeds field length");
        return false;
    }

    // Secondary key validation only if secondary indexing is enabled
    if (_config.use_secondary_index()) {
        if (!skey) {
            log(LOG_ERROR, "Secondary key cannot be null when secondary indexing is enabled");
            return false;
        }
        if (strlen(skey) >= _config._secondary_field_len) {
            log(LOG_ERROR, "Secondary key length exceeds field length");
            return false;
        }
    }

    return true;
}

// Logging
void HashMaster::log(LogLevel level, const char* format, ...) {
    if (level < _config._log_level) {
        return;
    }
    
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    printf("[%s] HashMaster: ", level_str[level]);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

// Set log level
void HashMaster::set_log_level(LogLevel level) {
    _config._log_level = level;
    if (_primary_hash_table) {
        _primary_hash_table->setLogLevel(level);
    }
    if (_secondary_hash_table) {
        _secondary_hash_table->setLogLevel(level);
    }
}

// Set lock usage
void HashMaster::setUseLock(bool use_lock) {
    _config._use_lock = use_lock;
    if (_primary_hash_table) {
        _primary_hash_table->setUseLock(use_lock);
    }
    if (_secondary_hash_table) {
        _secondary_hash_table->setUseLock(use_lock);
    }
}

// Sequential access methods
int HashMaster::getBySeq(int seq) {
    if (!validate_record_index(seq)) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    return seq;
}

char* HashMaster::get_record_by_seq(int seq) {
    if (!validate_record_index(seq)) {
        return nullptr;
    }
    DataRecordEntry *re = get_record_entry(seq-1);
    return re->_value;
}

// Validate integrity
bool HashMaster::validate_integrity() {
    if (!_initialized) {
        return false;
    }
    
    return _primary_hash_table->validate_file_integrity() && 
           _secondary_hash_table->validate_file_integrity();
}

// Future enhancement stubs
int HashMaster::defragment_records() {
    // TODO: Implement record defragmentation
    return HASH_OK;
}

int HashMaster::compact_storage() {
    // TODO: Implement storage compaction
    return HASH_OK;
}

int HashMaster::update_record(const char* /*pkey*/, const char* /*record*/, int /*record_size*/) {
    // TODO: Implement record update
    return HASH_OK;
}

int HashMaster::get_record_size(const char* /*pkey*/) {
    // TODO: Implement get record size
    return 0;
}

void HashMaster::display_records() {
    // TODO: Implement record display
}

// Iterator methods
bool HashMaster::Iterator::has_next() {
    // TODO: Implement iterator
    return false;
}

char* HashMaster::Iterator::next() {
    // TODO: Implement iterator
    return nullptr;
}

// Config utility functions - DEPRECATED: Use MasterManager instead
/*
HashMasterConfig load_config_from_file(const char* config_file) {
    HashMasterConfig config;

    try {
        Core::YAMLParser parser;

        if (!parser.load_from_file(config_file)) {
            std::cerr << "HashMaster: Failed to load config file: " << config_file << std::endl;
            return config;  // Return default config
        }

        // hashmaster 섹션에서 설정 값 읽기
        if (parser.has_section("hashmaster")) {
            Core::YAMLValue hashmaster_section = parser.get_section("hashmaster");
            
            // 각 설정값 읽기 (기본값 유지하면서 덮어쓰기)
            if (hashmaster_section.has_key("max_record_count")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("max_record_count");
                if (value.is_int()) {
                    config._max_record_count = value.as_int();
                }
            }
            
            if (hashmaster_section.has_key("max_record_size")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("max_record_size");
                if (value.is_int()) {
                    config._max_record_size = value.as_int();
                }
            }
            
            if (hashmaster_section.has_key("hash_count")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("hash_count");
                if (value.is_int()) {
                    config._hash_count = value.as_int();
                }
            }
            
            if (hashmaster_section.has_key("primary_field_len")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("primary_field_len");
                if (value.is_int()) {
                    config._primary_field_len = value.as_int();
                }
            }
            
            if (hashmaster_section.has_key("secondary_field_len")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("secondary_field_len");
                if (value.is_int()) {
                    config._secondary_field_len = value.as_int();
                }
            }
            
            if (hashmaster_section.has_key("use_lock")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("use_lock");
                if (value.is_bool()) {
                    config._use_lock = value.as_bool();
                }
            }
            
            if (hashmaster_section.has_key("filename")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("filename");
                if (value.is_string()) {
                    config._filename = value.as_string();
                }
            }
            
            if (hashmaster_section.has_key("log_level")) {
                Core::YAMLValue value = hashmaster_section.get_section_value("log_level");
                if (value.is_int()) {
                    int log_level = value.as_int();
                    // Convert integer to LogLevel enum
                    switch (log_level) {
                        case 0: config._log_level = LOG_ERROR; break;
                        case 1: config._log_level = LOG_WARNING; break;
                        case 2: config._log_level = LOG_INFO; break;
                        case 3: config._log_level = LOG_DEBUG; break;
                        default: config._log_level = LOG_INFO; break;
                    }
                }
            }
            
            // 총 크기 계산
            config._tot_size = config._max_record_count * config._max_record_size;
            
            std::cout << "HashMaster config loaded from: " << config_file << std::endl;
            std::cout << "  max_record_count: " << config._max_record_count << std::endl;
            std::cout << "  max_record_size: " << config._max_record_size << std::endl;
            std::cout << "  hash_count: " << config._hash_count << std::endl;
            std::cout << "  primary_field_len: " << config._primary_field_len << std::endl;
            std::cout << "  secondary_field_len: " << config._secondary_field_len << std::endl;
            std::cout << "  filename: " << config._filename << std::endl;
            
        } else {
            std::cerr << "HashMaster: 'hashmaster' section not found in config file" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "HashMaster: Error parsing config file: " << e.what() << std::endl;
    }
    
    return config;
}

int save_config_to_file(const HashMasterConfig& config, const char* config_file) {
    std::ofstream file(config_file);
    if (!file.is_open()) {
        return HASH_ERROR_FILE_ERROR;
    }

    // TODO: Implement config file saving
    return HASH_OK;
}
*/

HashMasterConfig get_config_from_hashmaster(const char* filename) {
    HashMasterConfig config;
    
    if (!filename) {
        printf("[ERROR] HashMaster: filename cannot be null\n");
        return config;
    }
    
    // Construct records filename
    std::string records_filename = "mmap/" + std::string(filename) + "_records.dat";
    
    // Open the records file
    int fd = open(records_filename.c_str(), O_RDONLY);
    if (fd == -1) {
        printf("[ERROR] HashMaster: Failed to open records file: %s (errno: %d)\n", 
               records_filename.c_str(), errno);
        return config;
    }
    
    // Get file size to validate it has at least the header
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        printf("[ERROR] HashMaster: Failed to get file stats: %s (errno: %d)\n", 
               records_filename.c_str(), errno);
        close(fd);
        return config;
    }
    
    if (file_stat.st_size < (off_t)sizeof(HashMasterHeader)) {
        printf("[ERROR] HashMaster: File too small, missing header: %s (size: %ld, required: %lu)\n", 
               records_filename.c_str(), file_stat.st_size, sizeof(HashMasterHeader));
        close(fd);
        return config;
    }
    
    // Memory map the header
    void* mapped_addr = mmap(nullptr, sizeof(HashMasterHeader), PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_addr == MAP_FAILED) {
        printf("[ERROR] HashMaster: Failed to mmap header: %s (errno: %d)\n", 
               records_filename.c_str(), errno);
        close(fd);
        return config;
    }
    
    // Read configuration from the header
    HashMasterHeader* header = static_cast<HashMasterHeader*>(mapped_addr);
    
    config._max_record_count = header->_max_record_count;
    config._max_record_size = header->_max_record_size;
    config._tot_size = header->_storage_size;
    config._hash_count = header->_hash_count;
    config._primary_field_len = header->_primary_field_len;
    config._secondary_field_len = header->_secondary_field_len;
    config._use_lock = header->_use_lock;
    config._filename = filename;
    config._log_level = LOG_INFO;  // Default log level
    
    printf("[INFO] HashMaster: Configuration loaded from %s\n", records_filename.c_str());
    printf("  max_record_count: %d\n", config._max_record_count);
    printf("  max_record_size: %d\n", config._max_record_size);
    printf("  hash_count: %d\n", config._hash_count);
    printf("  primary_field_len: %d\n", config._primary_field_len);
    printf("  secondary_field_len: %d\n", config._secondary_field_len);
    printf("  use_lock: %s\n", config._use_lock ? "true" : "false");
    
    // Clean up
    munmap(mapped_addr, sizeof(HashMasterHeader));
    close(fd);

    return config;
}

// Get record count implementation
int HashMaster::get_record_count() const {
    if (!_initialized) {
        return 0;
    }
    return _total_records;
}

// Get free record count implementation
int HashMaster::get_free_record_count() const {
    if (!_initialized) {
        return 0;
    }
    return _free_records;
}

