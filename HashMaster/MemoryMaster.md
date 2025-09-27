# MemoryMaster

A high-performance, in-memory implementation of the Master interface designed for simulation, testing, and scenarios requiring ultra-fast data access without persistence requirements.

## Overview

`MemoryMaster` provides a complete dual-indexed storage system that operates entirely in RAM using STL containers. It implements the same interface as `HashMaster` but optimizes for speed and flexibility rather than persistence, making it ideal for testing, prototyping, and high-frequency simulation workloads.

## Architecture

### Core Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Primary Index │    │ Secondary Index │    │   Memory Pool   │
│ (std::unordered │────│ (std::unordered │────│ (std::vector)   │
│     _map)       │    │     _map)       │    │                 │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Key -> Slot   │    │   Key -> Slot   │    │  MemoryRecord   │
│   Mapping       │    │   Mapping       │    │   Objects       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Memory Layout

#### MemoryRecord Structure
```cpp
struct MemoryRecord {
    std::vector<char> data;         // Record data
    std::string primary_key;        // Primary key
    std::string secondary_key;      // Secondary key
    bool is_valid;                  // Validity flag
};
```

#### Index Management
```cpp
// Primary index: key -> slot mapping
std::unordered_map<std::string, int> _primary_index;

// Secondary index: key -> slot mapping
std::unordered_map<std::string, int> _secondary_index;

// Record storage with slot management
std::vector<std::unique_ptr<MemoryRecord>> _records;
std::vector<int> _free_slots;
```

## Key Features

### 1. **Ultra-High Performance**
- **In-Memory Storage**: No disk I/O overhead
- **STL Optimization**: Leverages highly optimized standard containers
- **Cache-Friendly**: Sequential memory access patterns
- **Lock-Free Options**: Optional thread safety for single-threaded speed

### 2. **Advanced Statistics**
- **Real-Time Monitoring**: Live performance metrics
- **Memory Usage Tracking**: Detailed memory consumption analysis
- **Operation Counters**: Insert/lookup/delete performance tracking
- **Load Factor Analysis**: Hash table efficiency monitoring

### 3. **Simulation Features**
- **Workload Simulation**: Built-in performance testing
- **Data Export/Import**: Easy test data management
- **Comparison Tools**: Content comparison with other Master instances
- **Iterator Support**: Full record traversal capabilities

### 4. **Thread Safety Options**
- **std::shared_mutex**: High-performance read-write locking
- **Configurable Locking**: Enable/disable based on usage pattern
- **Lock-Free Mode**: Maximum speed for single-threaded scenarios

### 5. **Memory Management**
- **Dynamic Allocation**: Grows as needed within limits
- **Smart Pointers**: Automatic memory management
- **Compaction Support**: Memory defragmentation
- **Usage Estimation**: Real-time memory usage calculation

## API Reference

### Configuration

```cpp
struct MemoryMasterConfig : public MasterConfig {
    bool _enable_statistics;    // Enable detailed statistics collection
    bool _thread_safe;          // Enable thread safety

    // Constructor with defaults
    MemoryMasterConfig();

    // Copy constructor from base config
    MemoryMasterConfig(const MasterConfig& base);
};
```

### Core Operations

#### Initialization
```cpp
// Constructor options
MemoryMaster(const MemoryMasterConfig& config);
MemoryMaster(const MasterConfig& config);

// Lifecycle management
int init() override;                    // Initialize memory structures
int clear() override;                   // Clear all data
bool is_initialized() const override;   // Check initialization
```

#### Data Operations
```cpp
// Master interface implementation
int put(const char* pkey, const char* skey, const char* record, int record_size) override;
char* get_by_primary(const char* pkey) override;
char* get_by_secondary(const char* skey) override;
int del(const char* pkey) override;

// Statistics and monitoring
MasterStats get_statistics() override;
void display_statistics() override;
int get_record_count() const override;
int get_free_record_count() const override;
bool validate_integrity() override;
```

### MemoryMaster-Specific Methods

#### Extended Statistics
```cpp
struct MemoryMasterStats : public MasterStats {
    int lookup_count;               // Total lookup operations
    int insert_count;               // Total insert operations
    int delete_count;               // Total delete operations
    int collision_count;            // Hash collisions
    double hit_rate;                // Lookup success rate
    size_t memory_usage_bytes;      // Current memory usage
    double load_factor_primary;     // Primary index load factor
    double load_factor_secondary;   // Secondary index load factor
};

MemoryMasterStats get_memory_statistics() const;
void reset_statistics();
```

#### Memory Management
```cpp
int compact_memory();                   // Remove freed slots
size_t estimate_memory_usage() const;   // Calculate memory usage
```

#### Debug and Inspection
```cpp
std::vector<std::string> get_all_primary_keys() const;
std::vector<std::string> get_all_secondary_keys() const;
bool has_primary_key(const char* pkey) const;
bool has_secondary_key(const char* skey) const;
```

#### Simulation and Testing
```cpp
struct SimulationResult {
    double avg_read_time_ns;        // Average read time
    double avg_write_time_ns;       // Average write time
    int successful_operations;      // Successful operations
    int failed_operations;          // Failed operations
};

SimulationResult simulate_workload(int num_operations, double read_write_ratio = 0.8);

int load_test_data(const std::vector<std::string>& primary_keys,
                   const std::vector<std::string>& secondary_keys,
                   const std::vector<std::vector<char>>& records);

int export_all_data(std::vector<std::string>& primary_keys,
                    std::vector<std::string>& secondary_keys,
                    std::vector<std::vector<char>>& records) const;

bool compare_with(const Master& other) const;
```

### Iterator Support

```cpp
class MemoryIterator : public Master::Iterator {
public:
    MemoryIterator(const MemoryMaster* master);

    bool has_next() override;
    char* next() override;

    // Extended methods
    const std::string& get_current_primary_key() const;
    const std::string& get_current_secondary_key() const;
    int get_current_record_size() const;
};

std::unique_ptr<Iterator> create_iterator() override;
```

## Performance Characteristics

### Speed Benchmarks

```cpp
// Typical performance metrics (single-threaded)
Operation           Time/Op     Rate/Second
Insert              50-100 ns   10-20 million/sec
Primary Lookup      20-50 ns    20-50 million/sec
Secondary Lookup    20-50 ns    20-50 million/sec
Delete             100-200 ns   5-10 million/sec
```

### Memory Usage

```cpp
// Memory overhead calculation
Base Overhead:
- std::vector<unique_ptr>: 24 bytes + (8 bytes × capacity)
- std::unordered_map: ~56 bytes + (32 bytes × bucket_count)
- Free slot management: 24 bytes + (4 bytes × free_slots)

Per Record:
- MemoryRecord object: ~64 bytes
- Primary key string: key_length + 24 bytes
- Secondary key string: key_length + 24 bytes
- Record data: actual_data_size
- Hash table entries: ~64 bytes (both indices)

Total per record ≈ 152 + key_lengths + data_size bytes
```

### Scalability Analysis

```cpp
Records     Memory Usage    Insert Rate    Lookup Rate
1,000       ~200 KB        15M ops/sec    35M ops/sec
10,000      ~2 MB          12M ops/sec    30M ops/sec
100,000     ~20 MB         10M ops/sec    25M ops/sec
1,000,000   ~200 MB        8M ops/sec     20M ops/sec
```

## Usage Examples

### Basic Usage

```cpp
#include "MemoryMaster.h"

int main() {
    // Create configuration
    MemoryMasterConfig config;
    config._max_record_count = 10000;
    config._max_record_size = 1024;
    config._hash_count = 1000;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._use_lock = true;
    config._enable_statistics = true;

    // Create and initialize MemoryMaster
    MemoryMaster memory_master(config);
    if (memory_master.init() != MASTER_OK) {
        return -1;
    }

    // Store records
    std::string data = "Sample record data";
    int result = memory_master.put("primary_key_1", "secondary_key_1",
                                  data.c_str(), data.length());

    // Retrieve records
    char* retrieved = memory_master.get_by_primary("primary_key_1");
    if (retrieved) {
        std::cout << "Found: " << std::string(retrieved, data.length()) << std::endl;
    }

    // Display statistics
    memory_master.display_statistics();

    return 0;
}
```

### Performance Testing

```cpp
class MemoryMasterBenchmark {
private:
    MemoryMaster _master;
    std::mt19937 _rng;

public:
    MemoryMasterBenchmark(const MemoryMasterConfig& config)
        : _master(config), _rng(42) {
        _master.init();
    }

    void benchmark_inserts(int num_records) {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_records; ++i) {
            std::string primary = "key_" + std::to_string(i);
            std::string secondary = "sec_" + std::to_string(i % 100);
            std::string data = "data_" + std::to_string(i);

            _master.put(primary.c_str(), secondary.c_str(),
                       data.c_str(), data.length());
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        double avg_time = static_cast<double>(duration.count()) / num_records;
        double rate = 1e9 / avg_time;

        std::cout << "Insert benchmark:" << std::endl;
        std::cout << "  Records: " << num_records << std::endl;
        std::cout << "  Average time: " << avg_time << " ns/op" << std::endl;
        std::cout << "  Rate: " << rate << " ops/sec" << std::endl;
    }

    void benchmark_lookups(int num_lookups) {
        // Pre-populate with data
        for (int i = 0; i < num_lookups; ++i) {
            std::string key = "key_" + std::to_string(i);
            std::string data = "data_" + std::to_string(i);
            _master.put(key.c_str(), nullptr, data.c_str(), data.length());
        }

        auto start = std::chrono::high_resolution_clock::now();

        int found_count = 0;
        for (int i = 0; i < num_lookups; ++i) {
            std::string key = "key_" + std::to_string(i);
            char* result = _master.get_by_primary(key.c_str());
            if (result) found_count++;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        double avg_time = static_cast<double>(duration.count()) / num_lookups;
        double rate = 1e9 / avg_time;

        std::cout << "Lookup benchmark:" << std::endl;
        std::cout << "  Lookups: " << num_lookups << std::endl;
        std::cout << "  Found: " << found_count << std::endl;
        std::cout << "  Average time: " << avg_time << " ns/op" << std::endl;
        std::cout << "  Rate: " << rate << " ops/sec" << std::endl;
    }
};
```

### Financial Data Simulation

```cpp
#include "BinaryRecord.h"

class MarketDataSimulator {
private:
    MemoryMaster _master;
    std::shared_ptr<RecordLayout> _layout;
    std::mt19937 _rng;

public:
    MarketDataSimulator() : _rng(42) {
        // Configure for market data
        MemoryMasterConfig config;
        config._max_record_count = 100000;
        config._max_record_size = 256;
        config._hash_count = 10000;
        config._primary_field_len = 16; // RIC code
        config._secondary_field_len = 8; // Symbol
        config._use_lock = false; // Single-threaded simulation
        config._enable_statistics = true;

        _master = MemoryMaster(config);
        _master.init();

        // Create record layout
        _layout = std::make_shared<RecordLayout>("EQUITY_QUOTE");
        _layout->addField("RIC", FieldType::CHAR, 16, 0, true);
        _layout->addField("SYMBOL", FieldType::CHAR, 8, 0, true);
        _layout->addField("BID_PRICE", FieldType::NINE_MODE, 12, 4);
        _layout->addField("ASK_PRICE", FieldType::NINE_MODE, 12, 4);
        _layout->addField("BID_SIZE", FieldType::NINE_MODE, 10, 0);
        _layout->addField("ASK_SIZE", FieldType::NINE_MODE, 10, 0);
        _layout->addField("TIMESTAMP", FieldType::INT, 4);
        _layout->calculateLayout();
    }

    void simulate_market_data(int num_instruments, int ticks_per_instrument) {
        std::vector<std::string> symbols = {
            "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "META", "NVDA"
        };

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_instruments; ++i) {
            std::string symbol = symbols[i % symbols.size()];
            std::string ric = symbol + ".O";

            for (int tick = 0; tick < ticks_per_instrument; ++tick) {
                BinaryRecord record(_layout);

                // Generate market data
                record.setString("RIC", ric);
                record.setString("SYMBOL", symbol);
                record.set9Mode("BID_PRICE", generate_price());
                record.set9Mode("ASK_PRICE", generate_price());
                record.set9Mode("BID_SIZE", std::to_string(generate_size()));
                record.set9Mode("ASK_SIZE", std::to_string(generate_size()));
                record.setInt("TIMESTAMP", generate_timestamp());

                // Store in memory master
                std::string primary_key = ric + "_" + std::to_string(tick);
                _master.put(primary_key.c_str(), symbol.c_str(),
                           record.getBuffer(), record.getSize());
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        int total_records = num_instruments * ticks_per_instrument;
        double rate = static_cast<double>(total_records) / duration.count() * 1000;

        std::cout << "Market data simulation completed:" << std::endl;
        std::cout << "  Instruments: " << num_instruments << std::endl;
        std::cout << "  Ticks per instrument: " << ticks_per_instrument << std::endl;
        std::cout << "  Total records: " << total_records << std::endl;
        std::cout << "  Time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Rate: " << rate << " records/sec" << std::endl;

        _master.display_statistics();
    }

private:
    std::string generate_price() {
        std::uniform_real_distribution<double> dist(10.0, 1000.0);
        return std::to_string(dist(_rng));
    }

    int generate_size() {
        std::uniform_int_distribution<int> dist(100, 10000);
        return dist(_rng);
    }

    int generate_timestamp() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }
};
```

### Comparison and Validation

```cpp
class MasterComparator {
public:
    static bool compare_implementations(Master& reference, Master& test) {
        // Test data
        std::vector<TestRecord> test_data = generate_test_data(1000);

        // Insert into both
        for (const auto& record : test_data) {
            int ref_result = reference.put(record.primary.c_str(),
                                          record.secondary.c_str(),
                                          record.data.data(), record.data.size());
            int test_result = test.put(record.primary.c_str(),
                                      record.secondary.c_str(),
                                      record.data.data(), record.data.size());

            if (ref_result != test_result) {
                std::cerr << "Insert result mismatch for key: " << record.primary << std::endl;
                return false;
            }
        }

        // Compare retrievals
        for (const auto& record : test_data) {
            char* ref_data = reference.get_by_primary(record.primary.c_str());
            char* test_data = test.get_by_primary(record.primary.c_str());

            if ((ref_data == nullptr) != (test_data == nullptr)) {
                std::cerr << "Retrieval existence mismatch for key: " << record.primary << std::endl;
                return false;
            }

            if (ref_data && test_data) {
                if (memcmp(ref_data, test_data, record.data.size()) != 0) {
                    std::cerr << "Data content mismatch for key: " << record.primary << std::endl;
                    return false;
                }
            }
        }

        // Compare statistics
        auto ref_stats = reference.get_statistics();
        auto test_stats = test.get_statistics();

        if (ref_stats.used_records != test_stats.used_records) {
            std::cerr << "Record count mismatch: " << ref_stats.used_records
                      << " vs " << test_stats.used_records << std::endl;
            return false;
        }

        std::cout << "Implementation comparison: PASSED" << std::endl;
        return true;
    }

private:
    struct TestRecord {
        std::string primary;
        std::string secondary;
        std::vector<char> data;
    };

    static std::vector<TestRecord> generate_test_data(int count) {
        std::vector<TestRecord> data;
        data.reserve(count);

        for (int i = 0; i < count; ++i) {
            TestRecord record;
            record.primary = "key_" + std::to_string(i);
            record.secondary = "sec_" + std::to_string(i % 100);
            record.data.resize(64);
            std::fill(record.data.begin(), record.data.end(), 'A' + (i % 26));

            data.push_back(std::move(record));
        }

        return data;
    }
};
```

## Advanced Features

### Memory Pool Management

```cpp
class MemoryPoolManager {
private:
    MemoryMaster& _master;
    size_t _max_memory_usage;

public:
    MemoryPoolManager(MemoryMaster& master, size_t max_memory_mb)
        : _master(master), _max_memory_usage(max_memory_mb * 1024 * 1024) {}

    bool check_memory_limits() {
        size_t current_usage = _master.estimate_memory_usage();

        if (current_usage > _max_memory_usage) {
            std::cout << "Memory limit exceeded: " << current_usage
                      << " bytes (limit: " << _max_memory_usage << ")" << std::endl;

            // Trigger compaction
            _master.compact_memory();

            current_usage = _master.estimate_memory_usage();
            if (current_usage > _max_memory_usage) {
                std::cerr << "Memory limit still exceeded after compaction" << std::endl;
                return false;
            }
        }

        return true;
    }

    void print_memory_usage() {
        size_t usage = _master.estimate_memory_usage();
        double usage_mb = static_cast<double>(usage) / (1024 * 1024);
        double limit_mb = static_cast<double>(_max_memory_usage) / (1024 * 1024);
        double percentage = (usage_mb / limit_mb) * 100;

        std::cout << "Memory usage: " << usage_mb << " MB / " << limit_mb
                  << " MB (" << percentage << "%)" << std::endl;
    }
};
```

### Multi-threaded Testing

```cpp
class ConcurrentTester {
private:
    MemoryMaster& _master;
    std::atomic<int> _operations_completed{0};
    std::atomic<int> _errors{0};

public:
    ConcurrentTester(MemoryMaster& master) : _master(master) {}

    void run_concurrent_test(int num_threads, int ops_per_thread) {
        std::vector<std::thread> threads;

        auto start = std::chrono::high_resolution_clock::now();

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([this, t, ops_per_thread]() {
                worker_thread(t, ops_per_thread);
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        int total_ops = _operations_completed.load();
        double rate = static_cast<double>(total_ops) / duration.count() * 1000;

        std::cout << "Concurrent test results:" << std::endl;
        std::cout << "  Threads: " << num_threads << std::endl;
        std::cout << "  Operations per thread: " << ops_per_thread << std::endl;
        std::cout << "  Total operations: " << total_ops << std::endl;
        std::cout << "  Errors: " << _errors.load() << std::endl;
        std::cout << "  Time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Rate: " << rate << " ops/sec" << std::endl;
    }

private:
    void worker_thread(int thread_id, int operations) {
        std::mt19937 rng(42 + thread_id);
        std::uniform_int_distribution<int> op_dist(0, 2); // 0=insert, 1=lookup, 2=delete

        for (int i = 0; i < operations; ++i) {
            std::string key = "thread_" + std::to_string(thread_id) + "_key_" + std::to_string(i);
            std::string data = "data_" + std::to_string(i);

            int operation = op_dist(rng);
            int result = MASTER_OK;

            switch (operation) {
                case 0: // Insert
                    result = _master.put(key.c_str(), nullptr, data.c_str(), data.length());
                    break;

                case 1: // Lookup
                    {
                        char* retrieved = _master.get_by_primary(key.c_str());
                        if (!retrieved) result = MASTER_ERROR_KEY_NOT_FOUND;
                    }
                    break;

                case 2: // Delete
                    result = _master.del(key.c_str());
                    break;
            }

            if (result != MASTER_OK && result != MASTER_ERROR_KEY_NOT_FOUND &&
                result != MASTER_ERROR_KEY_EXISTS) {
                _errors.fetch_add(1);
            }

            _operations_completed.fetch_add(1);
        }
    }
};
```

## Best Practices

### 1. **Configuration Optimization**
```cpp
// High-performance configuration
MemoryMasterConfig create_high_performance_config() {
    MemoryMasterConfig config;
    config._max_record_count = 1000000;
    config._max_record_size = 512;
    config._hash_count = config._max_record_count / 0.75; // 75% load factor
    config._use_lock = false;                             // Single-threaded
    config._thread_safe = false;                          // No std::shared_mutex
    config._enable_statistics = false;                    // Minimal overhead
    config._log_level = LOG_ERROR;                        // Minimal logging
    return config;
}

// Memory-conscious configuration
MemoryMasterConfig create_memory_conscious_config() {
    MemoryMasterConfig config;
    config._max_record_count = 10000;                     // Smaller capacity
    config._max_record_size = 128;                        // Smaller records
    config._hash_count = config._max_record_count / 0.9;  // Higher load factor
    config._use_lock = true;                              // Thread safety
    config._enable_statistics = true;                     // Monitor usage
    return config;
}
```

### 2. **Error Handling**
```cpp
class SafeMemoryMaster {
private:
    std::unique_ptr<MemoryMaster> _master;

public:
    SafeMemoryMaster(const MemoryMasterConfig& config) {
        _master = std::make_unique<MemoryMaster>(config);

        if (_master->init() != MASTER_OK) {
            throw std::runtime_error("Failed to initialize MemoryMaster");
        }
    }

    bool safe_put(const std::string& primary, const std::string& secondary,
                  const std::vector<char>& data) {
        try {
            int result = _master->put(primary.c_str(),
                                     secondary.empty() ? nullptr : secondary.c_str(),
                                     data.data(), data.size());

            if (result == MASTER_ERROR_NO_SPACE) {
                // Try compaction and retry
                _master->compact_memory();
                result = _master->put(primary.c_str(),
                                     secondary.empty() ? nullptr : secondary.c_str(),
                                     data.data(), data.size());
            }

            return result == MASTER_OK;

        } catch (const std::exception& e) {
            std::cerr << "Exception in safe_put: " << e.what() << std::endl;
            return false;
        }
    }
};
```

### 3. **Performance Monitoring**
```cpp
class MemoryMasterMonitor {
private:
    MemoryMaster& _master;
    std::chrono::steady_clock::time_point _start_time;

public:
    MemoryMasterMonitor(MemoryMaster& master) : _master(master) {
        _start_time = std::chrono::steady_clock::now();
        _master.reset_statistics();
    }

    void print_periodic_stats(std::chrono::seconds interval = std::chrono::seconds(30)) {
        static auto last_check = _start_time;
        auto now = std::chrono::steady_clock::now();

        if (now - last_check >= interval) {
            auto stats = _master.get_memory_statistics();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - _start_time);

            std::cout << "=== MemoryMaster Stats (Uptime: " << uptime.count() << "s) ===" << std::endl;
            std::cout << "Records: " << stats.used_records << "/" << stats.total_records
                      << " (" << stats.record_utilization * 100 << "%)" << std::endl;
            std::cout << "Memory: " << stats.memory_usage_bytes / 1024 << " KB" << std::endl;
            std::cout << "Operations: I=" << stats.insert_count
                      << " L=" << stats.lookup_count
                      << " D=" << stats.delete_count << std::endl;
            std::cout << "Hit rate: " << stats.hit_rate * 100 << "%" << std::endl;
            std::cout << "Load factors: P=" << stats.load_factor_primary
                      << " S=" << stats.load_factor_secondary << std::endl;

            last_check = now;
        }
    }
};
```

## Troubleshooting

### Common Issues

#### 1. **Memory Usage Growth**
```cpp
// Problem: Excessive memory usage
// Solution: Monitor and compact regularly

void monitor_memory_usage(MemoryMaster& master) {
    auto stats = master.get_memory_statistics();
    size_t usage_mb = stats.memory_usage_bytes / (1024 * 1024);

    if (usage_mb > 100) { // Threshold check
        std::cout << "High memory usage: " << usage_mb << " MB" << std::endl;

        // Compact if utilization is low
        if (stats.record_utilization < 0.7) {
            master.compact_memory();
            std::cout << "Memory compacted" << std::endl;
        }
    }
}
```

#### 2. **Performance Degradation**
```cpp
// Problem: Decreasing performance
// Solution: Monitor load factors and statistics

void check_performance_health(MemoryMaster& master) {
    auto stats = master.get_memory_statistics();

    if (stats.load_factor_primary > 0.9) {
        std::cout << "Warning: High primary load factor: "
                  << stats.load_factor_primary << std::endl;
    }

    if (stats.hit_rate < 0.8 && stats.lookup_count > 1000) {
        std::cout << "Warning: Low hit rate: " << stats.hit_rate << std::endl;
    }

    if (stats.collision_count > stats.lookup_count * 0.3) {
        std::cout << "Warning: High collision rate" << std::endl;
    }
}
```

#### 3. **Thread Safety Issues**
```cpp
// Problem: Race conditions in multi-threaded environment
// Solution: Ensure proper configuration

MemoryMasterConfig create_thread_safe_config() {
    MemoryMasterConfig config;
    config._use_lock = true;        // Enable base locking
    config._thread_safe = true;     // Enable std::shared_mutex

    // Reduce contention by using larger hash tables
    config._hash_count *= 2;

    return config;
}
```

## See Also

- [Master.md](Master.md) - Abstract base class interface
- [HashMaster.md](HashMaster.md) - File-based implementation
- [HashTable.md](HashTable.md) - Low-level hash table implementation
- [BinaryRecord.md](BinaryRecord.md) - Binary record processing system
- [README.md](README.md) - Project overview and setup guide