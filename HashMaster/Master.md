# Master

Abstract base class for dual-indexed data storage systems providing a unified interface for high-performance storage implementations including file-based (HashMaster) and memory-based (MemoryMaster) solutions.

## Overview

The `Master` class defines a standardized interface for dual-indexed storage systems that support both primary and secondary key access. This abstraction enables seamless switching between different storage implementations while maintaining consistent performance characteristics and API compatibility.

## Architecture

### Core Design Pattern

```
┌─────────────────┐
│  Master (ABC)   │ ←── Abstract Base Class
└─────────────────┘
         ▲
         │
    ┌────┴────┐
    │         │
┌───▼───┐ ┌──▼──────┐
│HashM. │ │MemoryM. │ ←── Concrete Implementations
└───────┘ └─────────┘
```

### Interface Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│  Configuration  │────│   Operations    │────│   Statistics    │
│   Management    │    │   Interface     │    │  & Monitoring   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│ MasterConfig    │    │ CRUD Operations │    │  MasterStats    │
│ Validation      │    │ Key Management  │    │ Performance     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Key Features

### 1. **Unified Interface**
- **Consistent API**: Same interface across all implementations
- **Polymorphic Usage**: Runtime implementation switching
- **Type Safety**: Compile-time interface enforcement

### 2. **Dual-Key Support**
- **Primary Keys**: Unique identifiers for records
- **Secondary Keys**: Alternative lookup mechanism
- **Key Validation**: Automatic key format checking

### 3. **Configuration Management**
- **Extensible Config**: Base configuration with implementation-specific extensions
- **Validation**: Built-in configuration validation
- **Runtime Adjustment**: Dynamic configuration updates

### 4. **Performance Monitoring**
- **Statistics Interface**: Standardized performance metrics
- **Implementation-Specific**: Extended statistics per implementation
- **Real-time Monitoring**: Live performance tracking

### 5. **Error Handling**
- **Standardized Errors**: Consistent error codes across implementations
- **Detailed Logging**: Configurable logging levels
- **Exception Safety**: RAII and exception-safe design

## API Reference

### Base Configuration

```cpp
struct MasterConfig {
    int _max_record_count;      // Maximum number of records
    int _max_record_size;       // Maximum record size in bytes
    int _tot_size;              // Total size of record storage
    int _hash_count;            // Number of hash buckets
    int _primary_field_len;     // Primary key field length
    int _secondary_field_len;   // Secondary key field length
    bool _use_lock;             // Enable thread safety
    std::string _filename;      // Base filename for storage
    LogLevel _log_level;        // Logging verbosity

    // Validation method
    virtual bool validate() const;
};
```

### Core Interface

#### Lifecycle Management
```cpp
// Initialization
virtual int init() = 0;                    // Initialize the system
virtual int clear() = 0;                   // Clear all data
virtual bool is_initialized() const;       // Check initialization status
```

#### Data Operations
```cpp
// Primary operations
virtual int put(const char* pkey, const char* skey,
               const char* record, int record_size) = 0;
virtual char* get_by_primary(const char* pkey) = 0;
virtual char* get_by_secondary(const char* skey) = 0;
virtual int del(const char* pkey) = 0;

// Numeric key convenience wrappers
virtual int put(short pkey, const char* skey, const char* record, int record_size);
virtual int put(int pkey, const char* skey, const char* record, int record_size);
virtual char* get_by_primary(short pkey);
virtual char* get_by_primary(int pkey);
virtual char* get_by_secondary(short skey);
virtual char* get_by_secondary(int skey);
virtual int del(short pkey);
virtual int del(int pkey);
```

#### Statistics and Monitoring
```cpp
// Base statistics structure
struct MasterStats {
    int total_records;          // Total record capacity
    int free_records;           // Available record slots
    int used_records;           // Currently used records
    double record_utilization;  // Usage percentage
};

// Statistics interface
virtual MasterStats get_statistics() = 0;
virtual void display_statistics() = 0;
virtual int get_record_count() const = 0;
virtual int get_free_record_count() const = 0;
```

#### Configuration Management
```cpp
// Configuration access
virtual const MasterConfig& get_config() const;
virtual void set_log_level(LogLevel level);
virtual LogLevel get_log_level() const;
virtual void setUseLock(bool use_lock);
virtual bool getUseLock() const;
```

#### Advanced Operations
```cpp
// Integrity and maintenance
virtual bool validate_integrity();

// Iterator support
class Iterator {
public:
    virtual bool has_next() = 0;
    virtual char* next() = 0;
    virtual int get_current_index() const;
};

virtual std::unique_ptr<Iterator> create_iterator();
```

## Error Handling

### Error Codes
```cpp
enum MasterError {
    MASTER_OK = 0,                          // Success
    MASTER_ERROR_NULL_POINTER = -1,         // Null parameter
    MASTER_ERROR_INVALID_PARAMETER = -2,    // Invalid parameter
    MASTER_ERROR_KEY_NOT_FOUND = -3,        // Key doesn't exist
    MASTER_ERROR_KEY_EXISTS = -4,           // Duplicate key
    MASTER_ERROR_NO_SPACE = -5,             // Storage full
    MASTER_ERROR_FILE_ERROR = -6,           // File I/O error
    MASTER_ERROR_MEMORY_ERROR = -7,         // Memory allocation failure
    MASTER_ERROR_LOCK_ERROR = -8,           // Threading error
    MASTER_ERROR_NOT_INITIALIZED = -9       // System not initialized
};
```

### Error Handling Patterns
```cpp
// Standard error checking
std::unique_ptr<Master> master = create_master(config);
int result = master->init();
if (result != MASTER_OK) {
    // Handle initialization error
    switch (result) {
        case MASTER_ERROR_MEMORY_ERROR:
            // Handle memory issues
            break;
        case MASTER_ERROR_FILE_ERROR:
            // Handle file access issues
            break;
        default:
            // Handle other errors
            break;
    }
}

// Safe operations with error checking
result = master->put("primary_key", "secondary_key", data, size);
if (result == MASTER_ERROR_KEY_EXISTS) {
    // Handle duplicate key scenario
} else if (result == MASTER_ERROR_NO_SPACE) {
    // Handle storage full scenario
}
```

## Implementation Comparison

### HashMaster vs MemoryMaster

| Feature | HashMaster | MemoryMaster |
|---------|------------|--------------|
| **Storage** | Memory-mapped files | In-memory only |
| **Persistence** | Full persistence | Transient |
| **Performance** | High (disk-backed) | Very High (RAM) |
| **Capacity** | Limited by disk | Limited by RAM |
| **Thread Safety** | pthread locks | std::shared_mutex |
| **Use Cases** | Production systems | Testing, simulation |
| **Recovery** | Automatic from files | None |
| **Memory Usage** | Low (mapped) | High (full load) |

### When to Use Each Implementation

#### HashMaster
```cpp
// Production use cases
HashMasterConfig config;
config._max_record_count = 1000000;    // Large datasets
config._filename = "production_db";    // Persistent storage
config._use_lock = true;               // Multi-threaded access

auto master = std::make_unique<HashMaster>(config);
master->init();
```

#### MemoryMaster
```cpp
// Testing and simulation use cases
MemoryMasterConfig config;
config._max_record_count = 10000;      // Smaller datasets
config._thread_safe = true;            // Thread safety
config._enable_statistics = true;      // Detailed monitoring

auto master = std::make_unique<MemoryMaster>(config);
master->init();
```

## Usage Patterns

### Factory Pattern Implementation

```cpp
// Factory function type
typedef std::unique_ptr<Master> (*MasterFactory)(const MasterConfig& config);

// Factory implementations
std::unique_ptr<Master> create_hash_master(const MasterConfig& config) {
    return std::make_unique<HashMaster>(static_cast<const HashMasterConfig&>(config));
}

std::unique_ptr<Master> create_memory_master(const MasterConfig& config) {
    return std::make_unique<MemoryMaster>(config);
}

// Usage
enum class MasterType { HASH, MEMORY };

std::unique_ptr<Master> create_master(MasterType type, const MasterConfig& config) {
    switch (type) {
        case MasterType::HASH:
            return create_hash_master(config);
        case MasterType::MEMORY:
            return create_memory_master(config);
        default:
            return nullptr;
    }
}
```

### Polymorphic Usage

```cpp
class DataManager {
private:
    std::unique_ptr<Master> _master;

public:
    DataManager(std::unique_ptr<Master> master) : _master(std::move(master)) {
        _master->init();
    }

    int store_record(const std::string& primary, const std::string& secondary,
                    const char* data, int size) {
        return _master->put(primary.c_str(), secondary.c_str(), data, size);
    }

    char* retrieve_record(const std::string& key, bool use_secondary = false) {
        return use_secondary ? _master->get_by_secondary(key.c_str())
                             : _master->get_by_primary(key.c_str());
    }

    void print_statistics() {
        _master->display_statistics();
    }
};

// Usage with different implementations
void demo_polymorphic_usage() {
    MasterConfig config;
    config._max_record_count = 1000;

    // Use HashMaster
    {
        auto hash_master = create_hash_master(config);
        DataManager manager(std::move(hash_master));
        // ... operations
    }

    // Use MemoryMaster
    {
        auto memory_master = create_memory_master(config);
        DataManager manager(std::move(memory_master));
        // ... same operations, different implementation
    }
}
```

### Configuration Management

```cpp
// Base configuration with validation
class ConfigManager {
private:
    MasterConfig _base_config;

public:
    ConfigManager() {
        _base_config._max_record_count = 10000;
        _base_config._max_record_size = 1024;
        _base_config._hash_count = 1000;
        _base_config._primary_field_len = 32;
        _base_config._secondary_field_len = 16;
        _base_config._use_lock = true;
        _base_config._log_level = LOG_INFO;
    }

    // Create configuration for specific implementation
    template<typename ConfigType>
    ConfigType create_config(const std::string& filename) {
        ConfigType config(_base_config);
        config._filename = filename;

        if (!config.validate()) {
            throw std::invalid_argument("Invalid configuration");
        }

        return config;
    }

    // Runtime configuration adjustment
    void adjust_for_performance(MasterConfig& config, bool high_performance = true) {
        if (high_performance) {
            config._use_lock = false;           // Disable locking
            config._log_level = LOG_ERROR;      // Minimal logging
            config._hash_count *= 2;            // More buckets
        } else {
            config._use_lock = true;            // Enable safety
            config._log_level = LOG_DEBUG;      // Detailed logging
        }
    }
};
```

### Testing and Simulation

```cpp
class MasterTester {
private:
    std::unique_ptr<Master> _reference_master;  // Reference implementation
    std::unique_ptr<Master> _test_master;       // Implementation under test

public:
    MasterTester(std::unique_ptr<Master> reference, std::unique_ptr<Master> test)
        : _reference_master(std::move(reference)), _test_master(std::move(test)) {
        _reference_master->init();
        _test_master->init();
    }

    bool compare_implementations() {
        // Insert same data into both
        std::vector<std::tuple<std::string, std::string, std::vector<char>>> test_data;
        generate_test_data(test_data);

        for (const auto& [primary, secondary, data] : test_data) {
            int ref_result = _reference_master->put(primary.c_str(), secondary.c_str(),
                                                   data.data(), data.size());
            int test_result = _test_master->put(primary.c_str(), secondary.c_str(),
                                              data.data(), data.size());

            if (ref_result != test_result) {
                return false;
            }
        }

        // Compare retrievals
        for (const auto& [primary, secondary, data] : test_data) {
            char* ref_data = _reference_master->get_by_primary(primary.c_str());
            char* test_data_ptr = _test_master->get_by_primary(primary.c_str());

            if ((ref_data == nullptr) != (test_data_ptr == nullptr)) {
                return false;
            }

            if (ref_data && test_data_ptr) {
                if (memcmp(ref_data, test_data_ptr, data.size()) != 0) {
                    return false;
                }
            }
        }

        return true;
    }

private:
    void generate_test_data(std::vector<std::tuple<std::string, std::string, std::vector<char>>>& data) {
        // Generate consistent test data for comparison
        for (int i = 0; i < 100; ++i) {
            std::string primary = "key_" + std::to_string(i);
            std::string secondary = "sec_" + std::to_string(i % 10);
            std::vector<char> record_data(64, static_cast<char>('A' + (i % 26)));

            data.emplace_back(primary, secondary, record_data);
        }
    }
};
```

## Performance Guidelines

### Implementation Selection

#### Choose HashMaster When:
- **Large datasets** (> 100MB)
- **Persistence required**
- **Multi-process access**
- **Memory constraints**
- **Production environments**

#### Choose MemoryMaster When:
- **Testing and simulation**
- **Temporary data storage**
- **High-speed processing**
- **Prototyping**
- **Memory-rich environments**

### Configuration Optimization

```cpp
// High-performance configuration
MasterConfig create_performance_config() {
    MasterConfig config;
    config._max_record_count = 100000;
    config._hash_count = config._max_record_count / 0.75;  // 75% load factor
    config._use_lock = false;                              // Single-threaded
    config._log_level = LOG_ERROR;                         // Minimal logging
    return config;
}

// Memory-optimized configuration
MasterConfig create_memory_optimized_config() {
    MasterConfig config;
    config._max_record_count = 10000;                      // Smaller capacity
    config._max_record_size = 256;                         // Smaller records
    config._hash_count = config._max_record_count / 0.9;   // Higher load factor
    config._use_lock = true;                               // Thread safety
    return config;
}

// Balanced configuration
MasterConfig create_balanced_config() {
    MasterConfig config;
    config._max_record_count = 50000;                      // Medium capacity
    config._max_record_size = 512;                         // Medium records
    config._hash_count = config._max_record_count / 0.8;   // Optimal load factor
    config._use_lock = true;                               // Thread safety
    config._log_level = LOG_INFO;                          // Standard logging
    return config;
}
```

## Best Practices

### 1. **Interface Usage**
```cpp
// Always use smart pointers
std::unique_ptr<Master> master = create_master(type, config);

// Check initialization
if (master->init() != MASTER_OK) {
    // Handle error
}

// Use RAII for resource management
class MasterSession {
    std::unique_ptr<Master> _master;
public:
    MasterSession(std::unique_ptr<Master> master) : _master(std::move(master)) {
        if (_master->init() != MASTER_OK) {
            throw std::runtime_error("Failed to initialize master");
        }
    }
    // Automatic cleanup in destructor
};
```

### 2. **Error Handling**
```cpp
// Comprehensive error checking
int safe_put(Master& master, const char* pkey, const char* skey,
            const char* data, int size) {
    if (!pkey || !data || size <= 0) {
        return MASTER_ERROR_INVALID_PARAMETER;
    }

    int result = master.put(pkey, skey, data, size);

    switch (result) {
        case MASTER_OK:
            return result;
        case MASTER_ERROR_KEY_EXISTS:
            // Handle duplicate - maybe update instead
            return master.del(pkey) == MASTER_OK ?
                   master.put(pkey, skey, data, size) : result;
        case MASTER_ERROR_NO_SPACE:
            // Handle full storage
            log_error("Storage full, consider compaction");
            return result;
        default:
            log_error("Unexpected error: %d", result);
            return result;
    }
}
```

### 3. **Performance Monitoring**
```cpp
class PerformanceMonitor {
private:
    Master& _master;
    std::chrono::steady_clock::time_point _last_check;
    int _last_record_count;

public:
    PerformanceMonitor(Master& master) : _master(master), _last_record_count(0) {
        _last_check = std::chrono::steady_clock::now();
    }

    void check_performance() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _last_check);

        if (elapsed.count() >= 60) {  // Check every minute
            auto stats = _master.get_statistics();
            int records_added = stats.used_records - _last_record_count;

            double rate = static_cast<double>(records_added) / elapsed.count();

            if (stats.record_utilization > 0.9) {
                log_warning("High utilization: %.1f%%", stats.record_utilization * 100);
            }

            if (rate > 0) {
                log_info("Insert rate: %.1f records/second", rate);
            }

            _last_check = now;
            _last_record_count = stats.used_records;
        }
    }
};
```

## See Also

- [HashMaster.md](HashMaster.md) - File-based implementation
- [MemoryMaster.md](MemoryMaster.md) - Memory-based implementation
- [HashTable.md](HashTable.md) - Low-level hash table implementation
- [BinaryRecord.md](BinaryRecord.md) - Binary record processing system
- [README.md](README.md) - Project overview and setup guide