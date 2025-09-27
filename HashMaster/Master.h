#ifndef MASTER_H
#define MASTER_H

#include <memory>
#include <string>

// Forward declarations
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3
};

enum MasterError {
    MASTER_OK = 0,
    MASTER_ERROR_NULL_POINTER = -1,
    MASTER_ERROR_INVALID_PARAMETER = -2,
    MASTER_ERROR_KEY_NOT_FOUND = -3,
    MASTER_ERROR_KEY_EXISTS = -4,
    MASTER_ERROR_NO_SPACE = -5,
    MASTER_ERROR_FILE_ERROR = -6,
    MASTER_ERROR_MEMORY_ERROR = -7,
    MASTER_ERROR_LOCK_ERROR = -8,
    MASTER_ERROR_NOT_INITIALIZED = -9
};

// Base configuration structure
struct MasterConfig {
    int _max_record_count;      // Maximum number of records
    int _max_record_size;       // Maximum size of each record
    int _tot_size;              // Total size of record storage
    int _hash_count;            // Number of hash buckets
    int _primary_field_len;     // Primary key field length
    int _secondary_field_len;   // Secondary key field length
    bool _use_lock;             // Enable thread safety
    std::string _filename;      // Base filename for storage
    LogLevel _log_level;        // Logging level

    // Constructor with defaults
    MasterConfig()
        : _max_record_count(10000), _max_record_size(1024),
          _tot_size(0), _hash_count(1000),
          _primary_field_len(64), _secondary_field_len(64),
          _use_lock(true), _filename("master"), _log_level(LOG_INFO) {
        _tot_size = _max_record_count * _max_record_size;
    }

    // Helper methods
    bool use_secondary_index() const {
        return _secondary_field_len > 0;
    }

    // Validation
    virtual bool validate() const {
        return _max_record_count > 0 && _max_record_size > 0 &&
               _hash_count > 0 && _primary_field_len > 0 &&
               _secondary_field_len >= 0 && !_filename.empty();
    }
};

// Statistics structure for monitoring
struct MasterStats {
    int total_records;
    int free_records;
    int used_records;
    double record_utilization;
    // Derived classes can extend this
};

/**
 * @brief Abstract base class for dual-indexed data storage systems
 *
 * The Master class defines the interface for high-performance dual-indexed
 * storage systems that support both primary and secondary key access.
 * Implementations can be file-based (HashMaster) or memory-based (MemoryMaster).
 */
class Master {
protected:
    MasterConfig _config;
    bool _initialized;

    // Protected logging method
    virtual void log(LogLevel level, const char* format, ...) = 0;

public:
    /**
     * @brief Constructor with configuration
     * @param config Configuration parameters for the master system
     */
    explicit Master(const MasterConfig& config)
        : _config(config), _initialized(false) {}

    /**
     * @brief Virtual destructor
     */
    virtual ~Master() = default;

    // Disable copy constructor and assignment
    Master(const Master&) = delete;
    Master& operator=(const Master&) = delete;

    // ===== Core Lifecycle Operations =====

    /**
     * @brief Initialize the master system
     * @return MASTER_OK on success, error code on failure
     */
    virtual int init() = 0;

    /**
     * @brief Clear all data from the system
     * @return MASTER_OK on success, error code on failure
     */
    virtual int clear() = 0;

    /**
     * @brief Check if the system is initialized
     * @return true if initialized, false otherwise
     */
    virtual bool is_initialized() const { return _initialized; }

    // ===== Data Operations =====

    /**
     * @brief Store a record with primary and secondary keys
     * @param pkey Primary key
     * @param skey Secondary key (can be nullptr if not used)
     * @param record Record data
     * @param record_size Size of record data
     * @return MASTER_OK on success, error code on failure
     */
    virtual int put(const char* pkey, const char* skey, const char* record, int record_size) = 0;

    /**
     * @brief Retrieve record by primary key
     * @param pkey Primary key
     * @return Pointer to record data, nullptr if not found
     */
    virtual char* get_by_primary(const char* pkey) = 0;

    /**
     * @brief Retrieve record by secondary key
     * @param skey Secondary key
     * @return Pointer to record data, nullptr if not found
     */
    virtual char* get_by_secondary(const char* skey) = 0;

    /**
     * @brief Delete record by primary key
     * @param pkey Primary key
     * @return MASTER_OK on success, error code on failure
     */
    virtual int del(const char* pkey) = 0;

    // ===== Convenience Wrappers for Numeric Keys =====

    // Short key wrappers
    virtual int put(short pkey, const char* skey, const char* record, int record_size) {
        return put(reinterpret_cast<const char*>(&pkey), skey, record, record_size);
    }
    virtual char* get_by_primary(short pkey) {
        return get_by_primary(reinterpret_cast<const char*>(&pkey));
    }
    virtual int del(short pkey) {
        return del(reinterpret_cast<const char*>(&pkey));
    }

    // Int key wrappers
    virtual int put(int pkey, const char* skey, const char* record, int record_size) {
        return put(reinterpret_cast<const char*>(&pkey), skey, record, record_size);
    }
    virtual char* get_by_primary(int pkey) {
        return get_by_primary(reinterpret_cast<const char*>(&pkey));
    }
    virtual int del(int pkey) {
        return del(reinterpret_cast<const char*>(&pkey));
    }

    // Secondary key numeric wrappers
    virtual char* get_by_secondary(short skey) {
        return get_by_secondary(reinterpret_cast<const char*>(&skey));
    }
    virtual char* get_by_secondary(int skey) {
        return get_by_secondary(reinterpret_cast<const char*>(&skey));
    }

    // ===== Statistics and Monitoring =====

    /**
     * @brief Get system statistics
     * @return Statistics structure
     */
    virtual MasterStats get_statistics() = 0;

    /**
     * @brief Display statistics to stdout
     */
    virtual void display_statistics() = 0;

    // ===== Configuration Management =====

    /**
     * @brief Get current configuration
     * @return Reference to configuration
     */
    virtual const MasterConfig& get_config() const { return _config; }

    /**
     * @brief Set logging level
     * @param level New log level
     */
    virtual void set_log_level(LogLevel level) { _config._log_level = level; }

    /**
     * @brief Get logging level
     * @return Current log level
     */
    virtual LogLevel get_log_level() const { return _config._log_level; }

    /**
     * @brief Set lock usage
     * @param use_lock Enable/disable locking
     */
    virtual void setUseLock(bool use_lock) { _config._use_lock = use_lock; }

    /**
     * @brief Get lock usage setting
     * @return true if locking is enabled
     */
    virtual bool getUseLock() const { return _config._use_lock; }

    // ===== Optional Advanced Operations =====

    /**
     * @brief Validate system integrity
     * @return true if system is consistent, false otherwise
     */
    virtual bool validate_integrity() { return _initialized; }

    /**
     * @brief Get record count
     * @return Number of records stored
     */
    virtual int get_record_count() const = 0;

    /**
     * @brief Get free record count
     * @return Number of free record slots
     */
    virtual int get_free_record_count() const = 0;

    // ===== Iterator Support (Optional) =====

    /**
     * @brief Base iterator class for record traversal
     */
    class Iterator {
    protected:
        Master* _master;
        int _current_index;

    public:
        Iterator(Master* master, int start_index = 0)
            : _master(master), _current_index(start_index) {}

        virtual ~Iterator() = default;
        virtual bool has_next() = 0;
        virtual char* next() = 0;
        virtual int get_current_index() const { return _current_index; }
    };

    /**
     * @brief Create iterator for record traversal
     * @return Unique pointer to iterator
     */
    virtual std::unique_ptr<Iterator> create_iterator() {
        // Default implementation returns nullptr
        // Derived classes should override if they support iteration
        return nullptr;
    }
};

// Factory function type for creating master instances
typedef std::unique_ptr<Master> (*MasterFactory)(const MasterConfig& config);

// Utility functions
const char* masterErrorToString(MasterError error);
MasterConfig load_master_config_from_file(const char* config_file);
int save_master_config_to_file(const MasterConfig& config, const char* config_file);

#endif // MASTER_H