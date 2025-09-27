#include "HashTable.h"
#include "Master.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <iostream>
#include <algorithm>
#include <climits>

// Constructor
HashTable::HashTable(int hash_count, int field_len, int data_count, bool use_lock, 
                     const char* filename, bool is_char)
    : _fd_hash_index_table(-1), _fd_data_index_table(-1),
      _hash_index_table_addr(MAP_FAILED), _data_index_table_addr(MAP_FAILED),
      _hash_count(hash_count), _data_count(data_count), _field_len(field_len),
      _use_lock(use_lock), _is_char(is_char),
      _hash_index_table(nullptr), _data_index_table(nullptr),
      _initialized(false), _hash_function(nullptr), _log_level(LOG_INFO) {
    
    // Input validation
    if (hash_count <= 0 || field_len <= 0 || data_count <= 0) {
        log(LOG_ERROR, "Invalid parameters: hash_count=%d, field_len=%d, data_count=%d", 
            hash_count, field_len, data_count);
        return;
    }
    
    // Calculate sizes
    _hash_table_size = sizeof(HashIndexTable) + hash_count * sizeof(HashEntry);
    _sizeof_data_entry = sizeof(DataIndexEntry) + field_len;
    _data_table_size = data_count * _sizeof_data_entry;
    
    // Set filename
    if (filename) {
        snprintf(_filename, sizeof(_filename), "%s", filename);
    } else {
        snprintf(_filename, sizeof(_filename), "hashtable");
    }
    
    // Set default hash function
    _hash_function = nullptr;  // Will use default_hash method
    
    // Initialize rwlock attributes
    memset(&_rwlock, 0, sizeof(_rwlock));
    
    log(LOG_INFO, "HashTable created: hash_count=%d, field_len=%d, data_count=%d", 
        hash_count, field_len, data_count);
}

// Destructor
HashTable::~HashTable() {
    log(LOG_INFO, "HashTable destructor called");
    cleanup_resources();
}

// Resource cleanup
void HashTable::cleanup_resources() {
    // Destroy locks first
    destroy_locks();
    
    // Unmap memory
    if (_hash_index_table_addr != MAP_FAILED) {
        if (munmap(_hash_index_table_addr, _hash_table_size) == -1) {
            log(LOG_ERROR, "Failed to unmap hash index table: %s", strerror(errno));
        }
        _hash_index_table_addr = MAP_FAILED;
    }
    
    if (_data_index_table_addr != MAP_FAILED) {
        if (munmap(_data_index_table_addr, _data_table_size) == -1) {
            log(LOG_ERROR, "Failed to unmap data index table: %s", strerror(errno));
        }
        _data_index_table_addr = MAP_FAILED;
    }
    
    // Close file descriptors
    if (_fd_hash_index_table != -1) {
        close(_fd_hash_index_table);
        _fd_hash_index_table = -1;
    }
    
    if (_fd_data_index_table != -1) {
        close(_fd_data_index_table);
        _fd_data_index_table = -1;
    }
    
    // Reset pointers
    _hash_index_table = nullptr;
    _data_index_table = nullptr;
    _initialized = false;
}

// Initialization
int HashTable::init() {
    if (_initialized) {
        log(LOG_WARNING, "HashTable already initialized");
        return HASH_OK;
    }
    
    // Allocate files and memory
    int ret = allocate_files();
    if (ret != HASH_OK) {
        log(LOG_ERROR, "Failed to allocate files: %d", ret);
        return ret;
    }
    
    // Set up pointers
    _hash_index_table = (HashIndexTable*)_hash_index_table_addr;
    _data_index_table = (DataIndexEntry*)_data_index_table_addr;
    
    // Initialize locks
    if (_use_lock) {
        ret = init_locks();
        if (ret != HASH_OK) {
            log(LOG_ERROR, "Failed to initialize locks: %d", ret);
            cleanup_resources();
            return ret;
        }
    }
    
    // Validate file integrity
    if (!validate_file_integrity()) {
        log(LOG_INFO, "File integrity check failed, initializing new hash table");
        clear();
    }
    
    _initialized = true;
    log(LOG_INFO, "HashTable initialized successfully");
    return HASH_OK;
}

// File allocation
int HashTable::allocate_files() {
    char hash_filename[512];
    char data_filename[512];
    
    snprintf(hash_filename, sizeof(hash_filename), "mmap/%s.hashindex", _filename);
    snprintf(data_filename, sizeof(data_filename), "mmap/%s.dataindex", _filename);
    
    // Create hash index file
    _fd_hash_index_table = open(hash_filename, O_RDWR | O_CREAT, 0644);
    if (_fd_hash_index_table == -1) {
        log(LOG_ERROR, "Failed to create hash index file %s: %s", 
            hash_filename, strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Set file size
    if (ftruncate(_fd_hash_index_table, _hash_table_size) == -1) {
        log(LOG_ERROR, "Failed to set hash index file size: %s", strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Map hash index file
    _hash_index_table_addr = mmap(NULL, _hash_table_size, 
                                  PROT_READ | PROT_WRITE, MAP_SHARED, 
                                  _fd_hash_index_table, 0);
    if (_hash_index_table_addr == MAP_FAILED) {
        log(LOG_ERROR, "Failed to map hash index file: %s", strerror(errno));
        return HASH_ERROR_MEMORY_ERROR;
    }
    
    // Create data index file
    _fd_data_index_table = open(data_filename, O_RDWR | O_CREAT, 0644);
    if (_fd_data_index_table == -1) {
        log(LOG_ERROR, "Failed to create data index file %s: %s", 
            data_filename, strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Set file size
    if (ftruncate(_fd_data_index_table, _data_table_size) == -1) {
        log(LOG_ERROR, "Failed to set data index file size: %s", strerror(errno));
        return HASH_ERROR_FILE_ERROR;
    }
    
    // Map data index file
    _data_index_table_addr = mmap(NULL, _data_table_size, 
                                  PROT_READ | PROT_WRITE, MAP_SHARED, 
                                  _fd_data_index_table, 0);
    if (_data_index_table_addr == MAP_FAILED) {
        log(LOG_ERROR, "Failed to map data index file: %s", strerror(errno));
        return HASH_ERROR_MEMORY_ERROR;
    }
    
    return HASH_OK;
}

// Lock initialization
int HashTable::init_locks() {
    pthread_rwlockattr_t attr;
    int ret = pthread_rwlockattr_init(&attr);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to initialize rwlock attributes: %s", strerror(ret));
        return HASH_ERROR_LOCK_ERROR;
    }
    
    ret = pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to set rwlock process shared: %s", strerror(ret));
        pthread_rwlockattr_destroy(&attr);
        return HASH_ERROR_LOCK_ERROR;
    }
    
    ret = pthread_rwlock_init(&_rwlock, &attr);
    if (ret != 0) {
        log(LOG_ERROR, "Failed to initialize rwlock: %s", strerror(ret));
        pthread_rwlockattr_destroy(&attr);
        return HASH_ERROR_LOCK_ERROR;
    }
    
    pthread_rwlockattr_destroy(&attr);
    return HASH_OK;
}

// Lock destruction
void HashTable::destroy_locks() {
    if (_use_lock) {
        int ret = pthread_rwlock_destroy(&_rwlock);
        if (ret != 0) {
            log(LOG_ERROR, "Failed to destroy rwlock: %s", strerror(ret));
        }
    }
}

// Clear hash table
int HashTable::clear() {
    if (!_initialized && !_hash_index_table) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_use_lock) {
        pthread_rwlock_wrlock(&_rwlock);
    }
    
    // Initialize header
    _hash_index_table->_first_free_slot = 0;
    _hash_index_table->_magic_number = 0x48415348;
    _hash_index_table->_version = 1;
    _hash_index_table->_hash_count = _hash_count;
    _hash_index_table->_data_count = _data_count;
    _hash_index_table->_field_len = _field_len;
    _hash_index_table->_is_char_key = _is_char ? 1 : 0;
    _hash_index_table->_reserved[0] = 0;
    _hash_index_table->_reserved[1] = 0;
    _hash_index_table->_reserved[2] = 0;
    
    // Initialize hash entries
    for (int i = 0; i < _hash_count; i++) {
        _hash_index_table->_hash_entries[i].index = -1;
    }
    
    // Initialize data entries and free list
    for (int i = 0; i < _data_count; i++) {
        DataIndexEntry* de = get_data_entry(i);
        if (de) {
            de->occupied = 0;
            de->nextIndex = -1;
            de->nextEmpty = (i == _data_count - 1) ? -1 : i + 1;
            de->dataIndex = -1;
            memset(de->value, 0, _field_len);
        }
    }
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    log(LOG_INFO, "HashTable cleared successfully");
    return HASH_OK;
}

// Safe data entry access
DataIndexEntry* HashTable::get_data_entry(int index) {
    if (!validate_slot_index(index)) {
        return nullptr;
    }
    return (DataIndexEntry*)((char*)_data_index_table_addr + index * _sizeof_data_entry);
}

const DataIndexEntry* HashTable::get_data_entry(int index) const {
    if (index < 0 || index >= _data_count) {
        return nullptr;
    }
    return (const DataIndexEntry*)((const char*)_data_index_table_addr + index * _sizeof_data_entry);
}

// Hash functions
uint32_t HashTable::djb2_hash(const char* key, int len) {
    uint32_t hash = 5381;
    for (int i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)key[i];
    }
    return hash % _hash_count;
}

uint32_t HashTable::djb2_string_hash(const char* key, int len) {
    uint32_t hash = 5381;
    for (int i = 0; i < len && key[i] != '\0'; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)key[i];
    }
    return hash % _hash_count;
}

uint32_t HashTable::default_hash(const char* key, int len) {
    return _is_char ? djb2_string_hash(key, len) : djb2_hash(key, len);
}

// Validation methods
bool HashTable::validate_key(const char* key) {
    if (!key) {
        log(LOG_ERROR, "Key is null");
        return false;
    }
    
    if (_is_char) {
        size_t key_len = strnlen(key, _field_len);
        if (key_len >= _field_len) {
            log(LOG_ERROR, "String key too long: %zu >= %d", key_len, _field_len);
            return false;
        }
    }
    
    return true;
}

bool HashTable::validate_data_index(int dataIndex) {
    if (dataIndex < 0) {
        log(LOG_ERROR, "Invalid data index: %d", dataIndex);
        return false;
    }
    return true;
}

bool HashTable::validate_slot_index(int index) {
    if (index < 0 || index >= _data_count) {
        log(LOG_ERROR, "Invalid slot index: %d (valid range: 0-%d)", index, _data_count - 1);
        return false;
    }
    return true;
}

// Put operation
int HashTable::put(const char* key, int dataIndex) {
    if (!_initialized) {
        log(LOG_ERROR, "HashTable not initialized");
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (!validate_key(key) || !validate_data_index(dataIndex)) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_use_lock) {
        pthread_rwlock_wrlock(&_rwlock);
    }
    
    int result = HASH_OK;
    
    do {
        // Get free slot
        int index = _hash_index_table->_first_free_slot;
        if (index == -1) {
            log(LOG_ERROR, "No free slots available");
            result = HASH_ERROR_NO_SPACE;
            break;
        }
        
        // Get data entry
        DataIndexEntry* de = get_data_entry(index);
        if (!de) {
            log(LOG_ERROR, "Failed to get data entry at index %d", index);
            result = HASH_ERROR_MEMORY_ERROR;
            break;
        }
        
        // Update data entry
        de->occupied = 1;
        de->dataIndex = dataIndex;
        copy(de->value, key, _field_len);
        
        // Update free slot list
        _hash_index_table->_first_free_slot = de->nextEmpty;
        
        // Insert into hash chain
        uint32_t hash_value = _hash_function ? _hash_function(key, _field_len) : default_hash(key, _field_len);
        de->nextIndex = _hash_index_table->_hash_entries[hash_value].index;
        _hash_index_table->_hash_entries[hash_value].index = index;
        
        log(LOG_DEBUG, "Put key at index %d, hash %u, dataIndex %d", index, hash_value, dataIndex);
        
    } while (0);
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    return result;
}

// Get operation
int HashTable::get(const char* key) {
    if (!_initialized) {
        log(LOG_ERROR, "HashTable not initialized");
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (!validate_key(key)) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_use_lock) {
        pthread_rwlock_rdlock(&_rwlock);
    }
    
    int result = HASH_ERROR_KEY_NOT_FOUND;
    
    uint32_t hash_value = _hash_function ? _hash_function(key, _field_len) : default_hash(key, _field_len);
    int index = _hash_index_table->_hash_entries[hash_value].index;
    
    while (index != -1) {
        DataIndexEntry* de = get_data_entry(index);
        if (!de) {
            log(LOG_ERROR, "Invalid data entry at index %d", index);
            break;
        }
        
        if (de->occupied && compare(de->value, key, _field_len) == 0) {
            result = de->dataIndex;
            log(LOG_DEBUG, "Found key at index %d, dataIndex %d", index, result);
            break;
        }
        
        index = de->nextIndex;
    }
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    return result;
}

// Delete operation
int HashTable::del(const char* key) {
    if (!_initialized) {
        log(LOG_ERROR, "HashTable not initialized");
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (!validate_key(key)) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_use_lock) {
        pthread_rwlock_wrlock(&_rwlock);
    }
    
    int result = HASH_ERROR_KEY_NOT_FOUND;
    
    uint32_t hash_value = _hash_function ? _hash_function(key, _field_len) : default_hash(key, _field_len);
    int index = _hash_index_table->_hash_entries[hash_value].index;
    int prev_index = -1;
    
    while (index != -1) {
        DataIndexEntry* de = get_data_entry(index);
        if (!de) {
            log(LOG_ERROR, "Invalid data entry at index %d", index);
            break;
        }
        
        if (de->occupied && compare(de->value, key, _field_len) == 0) {
            // Remove from hash chain
            if (prev_index == -1) {
                _hash_index_table->_hash_entries[hash_value].index = de->nextIndex;
            } else {
                DataIndexEntry* prev_de = get_data_entry(prev_index);
                if (prev_de) {
                    prev_de->nextIndex = de->nextIndex;
                }
            }
            
            // Mark as free and add to free list
            de->occupied = 0;
            de->nextEmpty = _hash_index_table->_first_free_slot;
            _hash_index_table->_first_free_slot = index;
            
            result = HASH_OK;
            log(LOG_DEBUG, "Deleted key at index %d", index);
            break;
        }
        
        prev_index = index;
        index = de->nextIndex;
    }
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    return result;
}

// Add operation (put with duplicate check)
int HashTable::add(const char* key, int dataIndex) {
    if (get(key) != HASH_ERROR_KEY_NOT_FOUND) {
        return HASH_ERROR_KEY_EXISTS;
    }
    return put(key, dataIndex);
}

// Get by sequence
int HashTable::getBySeq(int seq) {
    if (!_initialized) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (!validate_slot_index(seq)) {
        return HASH_ERROR_INVALID_PARAMETER;
    }
    
    if (_use_lock) {
        pthread_rwlock_rdlock(&_rwlock);
    }
    
    DataIndexEntry* de = get_data_entry(seq);
    int result = de ? de->dataIndex : HASH_ERROR_KEY_NOT_FOUND;
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    return result;
}

// Find key by data index (for reverse lookup)
int HashTable::find_key_by_data_index(int target_data_index, char* found_key) {
    if (!_initialized) {
        log(LOG_ERROR, "HashTable not initialized");
        return HASH_ERROR_INVALID_PARAMETER;
    }

    if (target_data_index < 0 || !found_key) {
        log(LOG_ERROR, "Invalid parameters: target_data_index=%d, found_key=%p",
            target_data_index, found_key);
        return HASH_ERROR_INVALID_PARAMETER;
    }

    if (_use_lock) {
        pthread_rwlock_rdlock(&_rwlock);
    }

    int result = HASH_ERROR_KEY_NOT_FOUND;

    // Iterate through all data entries to find the one with matching dataIndex
    for (int i = 0; i < _data_count; i++) {
        DataIndexEntry* de = get_data_entry(i);
        if (de && de->occupied && de->dataIndex == target_data_index) {
            // Found the entry, copy the key
            copy(found_key, de->value, _field_len);
            result = HASH_OK;
            log(LOG_DEBUG, "Found key for data_index %d at slot %d", target_data_index, i);
            break;
        }
    }

    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }

    if (result != HASH_OK) {
        log(LOG_DEBUG, "Key not found for data_index %d", target_data_index);
    }

    return result;
}

// Set lock usage
void HashTable::setUseLock(bool use_lock) {
    if (_use_lock == use_lock) {
        return;
    }
    
    if (use_lock) {
        init_locks();
    } else {
        destroy_locks();
    }
    
    _use_lock = use_lock;
}

// Get statistics
HashTableStats HashTable::get_statistics() {
    HashTableStats stats = {};
    
    if (!_initialized) {
        return stats;
    }
    
    if (_use_lock) {
        pthread_rwlock_rdlock(&_rwlock);
    }
    
    stats.total_slots = _data_count;
    stats.used_slots = 0;
    stats.free_slots = 0;
    stats.collision_count = 0;
    stats.max_chain_length = 0;
    stats.min_chain_length = INT_MAX;
    
    int total_chain_length = 0;
    int chain_count = 0;
    
    // Count used/free slots
    for (int i = 0; i < _data_count; i++) {
        DataIndexEntry* de = get_data_entry(i);
        if (de) {
            if (de->occupied) {
                stats.used_slots++;
            } else {
                stats.free_slots++;
            }
        }
    }
    
    // Calculate chain statistics
    for (int i = 0; i < _hash_count; i++) {
        int chain_len = calculate_chain_length(_hash_index_table->_hash_entries[i].index);
        if (chain_len > 0) {
            chain_count++;
            total_chain_length += chain_len;
            stats.max_chain_length = std::max(stats.max_chain_length, chain_len);
            stats.min_chain_length = std::min(stats.min_chain_length, chain_len);
            if (chain_len > 1) {
                stats.collision_count += chain_len - 1;
            }
        }
    }
    
    stats.load_factor = (double)stats.used_slots / _data_count;
    stats.avg_chain_length = chain_count > 0 ? (double)total_chain_length / chain_count : 0.0;
    
    if (stats.min_chain_length == INT_MAX) {
        stats.min_chain_length = 0;
    }
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
    
    return stats;
}

// Calculate chain length
int HashTable::calculate_chain_length(int start_index) {
    int length = 0;
    int index = start_index;
    
    while (index != -1) {
        DataIndexEntry* de = get_data_entry(index);
        if (!de) break;
        length++;
        index = de->nextIndex;
    }
    
    return length;
}

// Display hash table
void HashTable::display_hashtable() {
    if (!_initialized) {
        printf("HashTable not initialized\n");
        return;
    }
    
    if (_use_lock) {
        pthread_rwlock_rdlock(&_rwlock);
    }
    
    printf("=== Hash Table Contents ===\n");
    printf("Hash Count: %d, Data Count: %d, Field Length: %d\n", 
           _hash_count, _data_count, _field_len);
    printf("Key Type: %s\n", _hash_index_table->_is_char_key ? "char string" : "binary");
    printf("First Free Slot: %d\n", _hash_index_table->_first_free_slot);
    
    for (int i = 0; i < _hash_count; i++) {
        if (_hash_index_table->_hash_entries[i].index == -1) {
            continue;
        }
        
        printf("Bucket %d: ", i);
        int index = _hash_index_table->_hash_entries[i].index;
        
        while (index != -1) {
            DataIndexEntry* de = get_data_entry(index);
            if (!de) break;
            
            printf("[%d:occ=%d,datai=%d,ni=%d,ne=%d] ", 
                   index, de->occupied, de->dataIndex, de->nextIndex, de->nextEmpty);
            
            index = de->nextIndex;
        }
        printf("\n");
    }
    
    if (_use_lock) {
        pthread_rwlock_unlock(&_rwlock);
    }
}

// Display statistics
void HashTable::display_statistics() {
    HashTableStats stats = get_statistics();
    
    printf("=== Hash Table Statistics ===\n");
    printf("Total slots: %d\n", stats.total_slots);
    printf("Used slots: %d\n", stats.used_slots);
    printf("Free slots: %d\n", stats.free_slots);
    printf("Load factor: %.2f%%\n", stats.load_factor * 100);
    printf("Collisions: %d\n", stats.collision_count);
    printf("Max chain length: %d\n", stats.max_chain_length);
    printf("Min chain length: %d\n", stats.min_chain_length);
    printf("Avg chain length: %.2f\n", stats.avg_chain_length);
}

// File validation
bool HashTable::validate_file_integrity() {
    if (!_hash_index_table) {
        return false;
    }
    
    return _hash_index_table->_magic_number == 0x48415348 &&
           _hash_index_table->_version == 1 &&
           _hash_index_table->_hash_count == _hash_count &&
           _hash_index_table->_data_count == _data_count &&
           _hash_index_table->_field_len == _field_len &&
           _hash_index_table->_is_char_key == (_is_char ? 1 : 0);
}

// Logging
void HashTable::log(LogLevel level, const char* format, ...) {
    if (level < _log_level) {
        return;
    }
    
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    printf("[%s] HashTable: ", level_str[level]);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

// Defragmentation (future enhancement)
int HashTable::defragment() {
    // TODO: Implement defragmentation logic
    return HASH_OK;
}

// Resize (future enhancement)
int HashTable::resize(int /*new_hash_count*/, int /*new_data_count*/) {
    // TODO: Implement resize logic
    return HASH_OK;
}

