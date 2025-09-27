#ifndef IMPROVED_HASH_MASTER_H
#define IMPROVED_HASH_MASTER_H

#include "Master.h"
#include "HashTable.h"
#include <memory>
#include <string>

// HashMaster-specific configuration extends MasterConfig
struct HashMasterConfig : public MasterConfig {
    // Constructor with defaults
    HashMasterConfig() : MasterConfig() {
        _filename = "hashmaster";
    }

    // Copy constructor from base config
    HashMasterConfig(const MasterConfig& base) : MasterConfig(base) {}
};

// Record management
struct RecordHeader {
    int size;           // Size of the record
    int primary_index;  // Index in primary hash table
    int secondary_index; // Index in secondary hash table
    bool occupied;      // Whether the record slot is occupied
    
    RecordHeader() : size(0), primary_index(-1), secondary_index(-1), occupied(false) {}
};


struct HashMasterHeader {
    // Free record management
    int _first_free_record;
    // config
    int _max_record_count;      // Maximum number of records
    int _max_record_size;       // Maximum size of each record
    int _storage_size;          // Total size of record storage
    int _hash_count;            // Number of hash buckets
    int _primary_field_len;     // Primary key field length
    int _secondary_field_len;   // Secondary key field length
    bool _use_lock;             // Enable thread safety
};

// Data entry structure (variable length)
struct DataRecordEntry {
    bool _occupied;       // 0: empty, 1: occupied
    char _filler[3];
    int _nextEmpty;      // Next empty slot in free list
    char _value[];       // Variable length key value
    
    DataRecordEntry() : _occupied(0), _nextEmpty(-1){}
};

class HashMaster : public Master {
private:
    
    // File management
    int _records_fd;
    void* _records_addr;
    // size_t _records_size;
    size_t _record_entry_size;
    size_t _storage_size;
    
    // Hash tables for dual indexing
    std::unique_ptr<HashTable> _primary_hash_table;
    std::unique_ptr<HashTable> _secondary_hash_table;
    
    // Record storage
    // char* _records;  //  DataRecordEntry
    // RecordHeader* _record_headers;   // HashMasterHeader
    HashMasterHeader *_htmaster_header;
    char *_record_entry_addr;
    
    // Free record management
    // int _first_free_record; // move to HashMasterHeader
    pthread_rwlock_t _master_rwlock;
    
    // Statistics
    int _total_records;
    int _free_records;
    
    // Internal helper methods
    int allocate_record_storage();
    void cleanup_record_storage();
    int find_free_record();
    void free_record(int index);
    char* get_record_data(int index);
    inline DataRecordEntry *get_record_entry(int index) { 
       return (DataRecordEntry *)(_records_addr + sizeof(HashMasterHeader) + (_record_entry_size * index));
    }
    
    // Lock management
    int init_master_locks();
    void destroy_master_locks();
    
    // Validation
    bool validate_record_index(int index);
    bool validate_keys(const char* pkey, const char* skey);
    
    // Logging implementation
    void log(LogLevel level, const char* format, ...) override;

public:
    // Constructor and destructor
    HashMaster(const HashMasterConfig& config);
    ~HashMaster() override;

    // Disable copy constructor and assignment
    HashMaster(const HashMaster&) = delete;
    HashMaster& operator=(const HashMaster&) = delete;

    // Implementation of Master interface
    int init() override;
    int clear() override;

    // Main operations - implementing Master interface
    int put(const char* pkey, const char* skey, const char* record, int record_size) override;
    char* get_by_primary(const char* pkey) override;
    char* get_by_secondary(const char* skey) override;
    int del(const char* pkey) override;

    // Additional HashMaster-specific operations
    char* get(int field_index, const char* key);  // field_index: 0=primary, 1=secondary
    
    // Convenience wrappers for numeric primary keys
    int put(short pkey, const char* skey, const char* record, int record_size) {
        return put(reinterpret_cast<const char*>(&pkey), skey, record, record_size);
    }
    char* get_by_primary(short pkey) {
        return get_by_primary(reinterpret_cast<const char*>(&pkey));
    }
    int del(short pkey) {
        return del(reinterpret_cast<const char*>(&pkey));
    }
    
    int put(int pkey, const char* skey, const char* record, int record_size) {
        return put(reinterpret_cast<const char*>(&pkey), skey, record, record_size);
    }
    char* get_by_primary(int pkey) {
        return get_by_primary(reinterpret_cast<const char*>(&pkey));
    }
    int del(int pkey) {
        return del(reinterpret_cast<const char*>(&pkey));
    }
    
    // Convenience wrappers for numeric secondary keys
    char* get_by_secondary(short skey) {
        return get_by_secondary(reinterpret_cast<const char*>(&skey));
    }
    char* get_by_secondary(int skey) {
        return get_by_secondary(reinterpret_cast<const char*>(&skey));
    }
    
    // Record management
    char* add_record(const char* pkey, const char* skey, int record_size);
    int update_record(const char* pkey, const char* record, int record_size);
    int get_record_size(const char* pkey);
    
    // Sequential access
    int getBySeq(int seq);
    char* get_record_by_seq(int seq);
    
    // Statistics and monitoring
    struct HashMasterStats : public MasterStats {
        HashTableStats primary_stats;
        HashTableStats secondary_stats;
    };

    // Master interface implementation
    MasterStats get_statistics() override;
    void display_statistics() override;
    int get_record_count() const override;
    int get_free_record_count() const override;
    bool validate_integrity() override;

    // HashMaster-specific statistics
    HashMasterStats get_hash_master_statistics();

    // Configuration management
    const HashMasterConfig& get_config() const {
        return static_cast<const HashMasterConfig&>(_config);
    }
    void set_log_level(LogLevel level) override;

    // Lock management
    void setUseLock(bool use_lock) override;
    bool getUseLock() const override { return _config._use_lock; }
    
    // Display and debugging
    void display_hashtable() const;
    void display_records();
    
    // Maintenance operations
    int defragment_records();
    int compact_storage();
    
    // Iterator support (future enhancement)
    class Iterator {
    private:
        HashMaster* _master;
        int _current_index;
        
    public:
        Iterator(HashMaster* master, int start_index = 0) 
            : _master(master), _current_index(start_index) {}
        
        bool has_next();
        char* next();
        int get_current_index() const { return _current_index; }
    };
    
    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, _config._max_record_count); }
};

// Utility functions
HashMasterConfig load_config_from_file(const char* config_file);
int save_config_to_file(const HashMasterConfig& config, const char* config_file);
HashMasterConfig get_config_from_hashmaster(const char* filename);

#endif // IMPROVED_HASH_MASTER_H