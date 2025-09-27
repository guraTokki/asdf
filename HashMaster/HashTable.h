#ifndef IMPROVED_HASH_TABLE_H
#define IMPROVED_HASH_TABLE_H

#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <memory>
#include "Master.h"

// Error codes
enum HashTableError {
    HASH_OK = 0,
    HASH_ERROR_NULL_POINTER = -1,
    HASH_ERROR_INVALID_PARAMETER = -2,
    HASH_ERROR_KEY_NOT_FOUND = -3,
    HASH_ERROR_KEY_EXISTS = -4,
    HASH_ERROR_NO_SPACE = -5,
    HASH_ERROR_FILE_ERROR = -6,
    HASH_ERROR_MEMORY_ERROR = -7,
    HASH_ERROR_LOCK_ERROR = -8
};


// Hash table statistics
struct HashTableStats {
    int total_slots;
    int used_slots;
    int free_slots;
    int collision_count;
    double load_factor;
    int max_chain_length;
    int min_chain_length;
    double avg_chain_length;
};

// Hash entry structure
struct HashEntry {
    int index;  // Index of first data entry in chain
    
    HashEntry() : index(-1) {}
};

// Data index entry structure (variable length)
struct DataIndexEntry {
    int occupied;       // 0: empty, 1: occupied
    int nextIndex;      // Next index in collision chain
    int nextEmpty;      // Next empty slot in free list
    int dataIndex;      // Associated data index
    char value[];       // Variable length key value
    
    DataIndexEntry() : occupied(0), nextIndex(-1), nextEmpty(-1), dataIndex(-1) {}
};

// Hash index table header
struct HashIndexTable {
    int _first_free_slot;               // First free slot index
    int _magic_number;                  // Magic number for validation
    int _version;                       // Version number
    int _hash_count;                    // Number of hash entries
    int _data_count;                    // Number of data entries
    int _field_len;                     // Key field length
    int _is_char_key;                   // 1: char string key, 0: binary key
    int _reserved[3];                   // Reserved for future use
    struct HashEntry _hash_entries[];   // Hash entries array
    
    HashIndexTable() : _first_free_slot(0), _magic_number(0x48415348), 
                      _version(1), _hash_count(0), _data_count(0), _field_len(0),
                      _is_char_key(0), _reserved{0, 0, 0} {}
};

// Hash function type
typedef uint32_t (*HashFunction)(const char* key, int len);

class HashTable {
private:
    // File descriptors and memory addresses
    int _fd_hash_index_table;
    int _fd_data_index_table;
    void* _hash_index_table_addr;
    void* _data_index_table_addr;
    
    // Configuration
    char _filename[256];        // _filename.hashindex, _filename.dataindex
    int _hash_count;            // Number of hash entries
    int _data_count;            // Number of data entries
    int _field_len;             // Key field length
    int _hash_table_size;       // sizeof(HashIndexTable) + _hash_count * sizeof(HashEntry);
    int _data_table_size;       // _data_count * _sizeof_data_entry;
    int _sizeof_data_entry;     // sizeof(DataIndexEntry) + field_len;
    bool _use_lock;
    bool _is_char;
    
    // Runtime objects
    HashIndexTable* _hash_index_table;
    DataIndexEntry* _data_index_table;
    pthread_rwlock_t _rwlock;
    bool _initialized;
    
    // Hash function
    HashFunction _hash_function;
    
    // Logging
    LogLevel _log_level;
    
    // Internal helper methods
    uint32_t default_hash(const char* key, int len);
    uint32_t djb2_hash(const char* key, int len);
    uint32_t djb2_string_hash(const char* key, int len);
    
    inline int compare(const char* key1, const char* key2, int len) {
        if (!key1 || !key2) return -1;
        return _is_char ? strncmp(key1, key2, len) : memcmp(key1, key2, len);
    }
    
    inline void* copy(char* dest, const char* src, int len) {
        if (!dest || !src) return nullptr;
        return _is_char ? (void*)strncpy(dest, src, len) : (void*)memcpy(dest, src, len);
    }
    
    // Validation methods
    bool validate_key(const char* key);
    bool validate_data_index(int dataIndex);
    bool validate_slot_index(int index);
    
    // Memory management
    /* create hash index file -> mmap hash index file
     * create data index file -> mmap data index file
    */
    int allocate_files();
    void cleanup_resources();
    
    // Lock management
    int init_locks();
    void destroy_locks();
    
    // Logging
    void log(LogLevel level, const char* format, ...);
    
    // Statistics helpers
    int calculate_chain_length(int start_index);
    
public:
    // Constructor and destructor
    HashTable(int hash_count, int field_len, int data_count, bool use_lock = true, 
              const char* filename = "hashtable", bool is_char = false);
    ~HashTable();
    
    // Disable copy constructor and assignment
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;
    
    // Initialization and cleanup
    int init(); /* allocate files, set up pointers(hash index, data index), init locks */
    int clear();
    bool is_initialized() const { return _initialized; }
    
    // Main operations
    int put(const char* key, int dataIndex);
    int get(const char* key);
    int del(const char* key);
    int add(const char* key, int dataIndex);  // Same as put but checks for duplicates
    
    // Convenience wrappers for numeric key types
    int put(short key, int dataIndex) { return put(reinterpret_cast<const char*>(&key), dataIndex); }
    int get(short key) { return get(reinterpret_cast<const char*>(&key)); }
    int del(short key) { return del(reinterpret_cast<const char*>(&key)); }
    int add(short key, int dataIndex) { return add(reinterpret_cast<const char*>(&key), dataIndex); }
    
    int put(int key, int dataIndex) { return put(reinterpret_cast<const char*>(&key), dataIndex); }
    int get(int key) { return get(reinterpret_cast<const char*>(&key)); }
    int del(int key) { return del(reinterpret_cast<const char*>(&key)); }
    int add(int key, int dataIndex) { return add(reinterpret_cast<const char*>(&key), dataIndex); }
    
    // Sequential access
    int getBySeq(int seq);

    // Find key by data index (for reverse lookup)
    int find_key_by_data_index(int target_data_index, char* found_key);

    // Hash function management
    void setHashFunction(HashFunction func) { _hash_function = func; }
    HashFunction getHashFunction() const { return _hash_function; }
    
    // Lock management
    void setUseLock(bool use_lock);
    bool getUseLock() const { return _use_lock; }

    inline int get_first_free_slot() { return _hash_index_table->_first_free_slot;}
    
    // Logging
    void setLogLevel(LogLevel level) { _log_level = level; }
    LogLevel getLogLevel() const { return _log_level; }
    
    // Statistics and debugging
    HashTableStats get_statistics();
    void display_hashtable();
    void display_statistics();
    
    // Getters
    HashIndexTable* get_hash_index_table() { return _hash_index_table; }
    DataIndexEntry* get_data_index_table() { return _data_index_table; }
    int get_first_free_slot() const { 
        return _hash_index_table ? _hash_index_table->_first_free_slot : -1; 
    }
    int get_hash_count() const { return _hash_count; }
    int get_data_count() const { return _data_count; }
    int get_field_len() const { return _field_len; }
    bool is_char_key() const { return _is_char; }
    bool get_stored_char_key_flag() const { 
        return _hash_index_table ? (_hash_index_table->_is_char_key == 1) : false; 
    }
    
    // Safe data entry access
    DataIndexEntry* get_data_entry(int index);
    const DataIndexEntry* get_data_entry(int index) const;
    
    // File validation
    bool validate_file_integrity();
    
    // Maintenance operations
    int defragment();  // Compact deleted entries
    int resize(int new_hash_count, int new_data_count);  // Resize hash table
};

#endif // IMPROVED_HASH_TABLE_H