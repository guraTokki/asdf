#ifndef MEMORY_MASTER_H
#define MEMORY_MASTER_H

#include "Master.h"
#include "../common/Compat.h"  // For GCC 4.8.5 compatibility
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
// Note: shared_mutex not available in GCC 4.8.5, using regular mutex

// Memory-based configuration
struct MemoryMasterConfig : public MasterConfig {
    bool _enable_statistics;    // Enable detailed statistics collection
    bool _thread_safe;          // Enable thread safety (uses std::shared_mutex)

    // Constructor with defaults
    MemoryMasterConfig() : MasterConfig() {
        _filename = "memory_master";
        _enable_statistics = true;
        _thread_safe = true;
    }

    // Copy constructor from base config
    MemoryMasterConfig(const MasterConfig& base) : MasterConfig(base) {
        _enable_statistics = true;
        _thread_safe = true;
    }
};

/**
 * @brief In-memory implementation of Master interface for simulation and testing
 *
 * MemoryMaster provides a high-performance, in-memory dual-indexed storage system
 * that implements the same interface as HashMaster but operates entirely in RAM.
 * It's designed for simulation, testing, and scenarios where persistence is not required.
 */
class MemoryMaster : public Master {
private:
    // Record storage structure
    struct MemoryRecord {
        std::vector<char> data;
        std::string primary_key;
        std::string secondary_key;
        bool is_valid;

        MemoryRecord() : is_valid(false) {}
        MemoryRecord(const char* record_data, int size, const std::string& pkey, const std::string& skey)
            : data(record_data, record_data + size), primary_key(pkey), secondary_key(skey), is_valid(true) {}
    };

    // Memory storage
    std::vector<std::unique_ptr<MemoryRecord>> _records;
    std::unordered_map<std::string, int> _primary_index;
    std::unordered_map<std::string, int> _secondary_index;
    std::vector<int> _free_slots;

    // Process-level thread safety using pthread_mutex
    mutable pthread_mutex_t _rw_mutex;

    // Statistics
    mutable int _lookup_count;
    mutable int _insert_count;
    mutable int _delete_count;
    mutable int _collision_count;

    // Internal methods
    int find_free_slot();
    void free_slot(int index);
    bool is_valid_key(const char* key) const;
    void update_statistics_on_insert();
    void update_statistics_on_lookup() const;
    void update_statistics_on_delete();

    // Logging implementation
    void log(LogLevel level, const char* format, ...) override;

public:
    /**
     * @brief Constructor with memory-specific configuration
     * @param config MemoryMaster configuration
     */
    explicit MemoryMaster(const MemoryMasterConfig& config);

    /**
     * @brief Constructor with base Master configuration
     * @param config Base Master configuration
     */
    explicit MemoryMaster(const MasterConfig& config);

    /**
     * @brief Destructor
     */
    ~MemoryMaster() override;

    // Disable copy constructor and assignment
    MemoryMaster(const MemoryMaster&) = delete;
    MemoryMaster& operator=(const MemoryMaster&) = delete;

    // ===== Master Interface Implementation =====

    /**
     * @brief Initialize the memory master system
     * @return MASTER_OK on success, error code on failure
     */
    int init() override;

    /**
     * @brief Clear all data from memory
     * @return MASTER_OK on success, error code on failure
     */
    int clear() override;

    /**
     * @brief Store a record with primary and secondary keys
     * @param pkey Primary key
     * @param skey Secondary key (can be nullptr if not used)
     * @param record Record data
     * @param record_size Size of record data
     * @return MASTER_OK on success, error code on failure
     */
    int put(const char* pkey, const char* skey, const char* record, int record_size) override;

    /**
     * @brief Retrieve record by primary key
     * @param pkey Primary key
     * @return Pointer to record data, nullptr if not found
     */
    char* get_by_primary(const char* pkey) override;

    /**
     * @brief Retrieve record by secondary key
     * @param skey Secondary key
     * @return Pointer to record data, nullptr if not found
     */
    char* get_by_secondary(const char* skey) override;

    /**
     * @brief Delete record by primary key
     * @param pkey Primary key
     * @return MASTER_OK on success, error code on failure
     */
    int del(const char* pkey) override;

    /**
     * @brief Get system statistics
     * @return Statistics structure
     */
    MasterStats get_statistics() override;

    /**
     * @brief Display statistics to stdout
     */
    void display_statistics() override;

    /**
     * @brief Get current record count
     * @return Number of active records
     */
    int get_record_count() const override;

    /**
     * @brief Get free record count
     * @return Number of available record slots
     */
    int get_free_record_count() const override;

    /**
     * @brief Validate system integrity
     * @return true if system is consistent
     */
    bool validate_integrity() override;

    // ===== MemoryMaster-Specific Methods =====

    /**
     * @brief Extended statistics for memory master
     */
    struct MemoryMasterStats : public MasterStats {
        int lookup_count;
        int insert_count;
        int delete_count;
        int collision_count;
        double hit_rate;
        size_t memory_usage_bytes;
        double load_factor_primary;
        double load_factor_secondary;
    };

    /**
     * @brief Get detailed memory master statistics
     * @return Extended statistics structure
     */
    MemoryMasterStats get_memory_statistics() const;

    /**
     * @brief Reset all statistics counters
     */
    void reset_statistics();

    /**
     * @brief Get memory configuration
     * @return Reference to memory configuration
     */
    const MemoryMasterConfig& get_memory_config() const {
        return static_cast<const MemoryMasterConfig&>(_config);
    }

    /**
     * @brief Compact memory by removing free slots
     * @return MASTER_OK on success
     */
    int compact_memory();

    /**
     * @brief Estimate memory usage in bytes
     * @return Estimated memory usage
     */
    size_t estimate_memory_usage() const;

    /**
     * @brief Get all primary keys (for debugging/testing)
     * @return Vector of all primary keys
     */
    std::vector<std::string> get_all_primary_keys() const;

    /**
     * @brief Get all secondary keys (for debugging/testing)
     * @return Vector of all secondary keys
     */
    std::vector<std::string> get_all_secondary_keys() const;

    /**
     * @brief Check if a primary key exists
     * @param pkey Primary key to check
     * @return true if key exists
     */
    bool has_primary_key(const char* pkey) const;

    /**
     * @brief Check if a secondary key exists
     * @param skey Secondary key to check
     * @return true if key exists
     */
    bool has_secondary_key(const char* skey) const;

    // ===== Iterator Implementation =====

    /**
     * @brief Memory-specific iterator for record traversal
     */
    class MemoryIterator : public Master::Iterator {
    private:
        const MemoryMaster* _memory_master;
        std::vector<int> _valid_indices;
        size_t _current_pos;

    public:
        MemoryIterator(const MemoryMaster* master);
        ~MemoryIterator() override = default;

        bool has_next() override;
        char* next() override;

        // Additional methods for memory iterator
        const std::string& get_current_primary_key() const;
        const std::string& get_current_secondary_key() const;
        int get_current_record_size() const;
    };

    /**
     * @brief Create iterator for record traversal
     * @return Unique pointer to memory iterator
     */
    std::unique_ptr<Iterator> create_iterator() override;

    // ===== Simulation and Testing Features =====

    /**
     * @brief Simulate random access patterns for performance testing
     * @param num_operations Number of operations to simulate
     * @param read_write_ratio Ratio of reads to writes (0.0 = all writes, 1.0 = all reads)
     * @return Performance metrics
     */
    struct SimulationResult {
        double avg_read_time_ns;
        double avg_write_time_ns;
        int successful_operations;
        int failed_operations;
    };

    SimulationResult simulate_workload(int num_operations, double read_write_ratio = 0.8);

    /**
     * @brief Load test data from vectors (for testing)
     * @param primary_keys Vector of primary keys
     * @param secondary_keys Vector of secondary keys
     * @param records Vector of record data
     * @return MASTER_OK on success
     */
    int load_test_data(const std::vector<std::string>& primary_keys,
                      const std::vector<std::string>& secondary_keys,
                      const std::vector<std::vector<char>>& records);

    /**
     * @brief Export all data to vectors (for testing/backup)
     * @param primary_keys Output vector for primary keys
     * @param secondary_keys Output vector for secondary keys
     * @param records Output vector for record data
     * @return MASTER_OK on success
     */
    int export_all_data(std::vector<std::string>& primary_keys,
                       std::vector<std::string>& secondary_keys,
                       std::vector<std::vector<char>>& records) const;

    /**
     * @brief Compare contents with another Master instance
     * @param other Another Master instance to compare with
     * @return true if contents are identical
     */
    bool compare_with(const Master& other) const;
};

// Factory function for creating MemoryMaster instances
std::unique_ptr<Master> create_memory_master(const MasterConfig& config);

#endif // MEMORY_MASTER_H