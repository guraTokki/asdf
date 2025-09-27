# HashMaster

A high-performance dual-indexed hash table system with memory-mapped storage, thread safety, and comprehensive record management for financial data processing. HashMaster is a concrete implementation of the Master abstract interface, providing persistent file-based storage.

## Overview

The `HashMaster` class extends the `Master` abstract base class to provide a dual-indexed hash table system that allows records to be stored and retrieved using both primary and secondary keys. It manages two separate `HashTable` instances and handles memory-mapped record storage with persistent file-based storage, making it ideal for production systems requiring data persistence.

## Architecture

### Core Components

```
┌─────────────────┐
│  Master (ABC)   │ ←── Abstract Base Class
└─────────────────┘
         ▲
         │
┌────────┴────────┐
│   HashMaster    │ ←── File-based Implementation
└─────────────────┘
         │
         ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Primary Hash  │    │  Secondary Hash │    │   Record Data   │
│     Table       │────│     Table       │────│    Storage      │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│{filename}_      │    │{filename}_      │    │{filename}_      │
│primary.*        │    │secondary.*      │    │records.dat      │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### File Structure

HashMaster creates five files for persistent storage:

- **`{filename}_records.dat`**: Memory-mapped record storage with header
- **`{filename}_primary.hashindex`**: Primary hash table index
- **`{filename}_primary.dataindex`**: Primary hash table data
- **`{filename}_secondary.hashindex`**: Secondary hash table index  
- **`{filename}_secondary.dataindex`**: Secondary hash table data

### Memory Layout

#### HashMaster Header Structure
```cpp
struct HashMasterHeader {
    int magic_number;           // File validation (0x484D5354)
    int version;                // Version number
    int max_record_count;       // Maximum number of records
    int max_record_size;        // Maximum size per record
    int hash_count;             // Number of hash buckets
    int primary_field_len;      // Primary key field length
    int secondary_field_len;    // Secondary key field length
    int use_lock;               // Thread safety flag
    int record_count;           // Current number of records
    int first_free_slot;        // First available record slot
    char filename[256];         // Base filename
    int reserved[10];           // Future expansion
};
```

## Key Features

### 1. **Dual Indexing System**
- **Primary Index**: Fast lookup by primary key
- **Secondary Index**: Fast lookup by secondary key
- **Record Mapping**: Both keys map to the same physical record

### 2. **Memory-Mapped Storage**
- **Zero-Copy Access**: Direct memory mapping for record data
- **Persistent Storage**: Automatic file synchronization
- **Large Dataset Support**: Efficient handling of millions of records

### 3. **Configuration Recovery**
- **Automatic Loading**: Extract configuration from existing files
- **Header Validation**: Magic number and version checking
- **Parameter Recovery**: Restore all original settings

### 4. **Thread Safety**
- **Configurable Locking**: Enable/disable based on usage pattern
- **Read-Write Locks**: Concurrent read access, exclusive write access
- **Lock-Free Mode**: Single-threaded optimization

### 5. **Comprehensive Statistics**
- **Dual Table Monitoring**: Statistics from both hash tables
- **Performance Metrics**: Load factors, collision rates, chain lengths
- **Health Indicators**: Memory usage, error rates, optimization suggestions

## API Reference

### Configuration Structure

```cpp
struct HashMasterConfig {
    int _max_record_count;      // Maximum number of records
    int _max_record_size;       // Maximum record size in bytes
    int _hash_count;            // Number of hash buckets
    int _primary_field_len;     // Primary key field length
    int _secondary_field_len;   // Secondary key field length
    bool _use_lock;             // Enable thread safety
    std::string _filename;      // Base filename
    int _log_level;             // Logging verbosity (0-3)
};
```

### Constructor

```cpp
HashMaster(const HashMasterConfig& config);
```

**Parameters:**
- `config`: Configuration structure with all required settings

### Core Operations

#### Initialization
```cpp
int init();                 // Initialize HashMaster system
int clear();               // Clear all records
bool is_initialized();     // Check initialization status
```

#### Record Operations
```cpp
// Store record with dual keys
int put(const char* primary_key, const char* secondary_key, 
        const char* data, int data_len);

// Retrieve by primary key
char* get_by_primary(const char* primary_key);

// Retrieve by secondary key  
char* get_by_secondary(const char* secondary_key);

// Delete record (removes from both indices)
int del(const char* primary_key);

// Add with duplicate checking
int add(const char* primary_key, const char* secondary_key,
        const char* data, int data_len);
```

#### Statistics and Monitoring
```cpp
HashTableStats get_primary_stats();    // Primary table statistics
HashTableStats get_secondary_stats();  // Secondary table statistics
void display_statistics();             // Print comprehensive stats
int get_record_count();                // Current number of records
```

#### Configuration Management
```cpp
// Load configuration from existing files
HashMasterConfig get_config_from_hashmaster(const char* filename);

// Get current configuration
HashMasterConfig get_current_config();
```

## Configuration Guidelines

### Sizing Recommendations

#### Record Count Planning
```cpp
// Plan for growth with buffer
int max_record_count = expected_records * 1.3;  // 30% growth buffer

// Consider deletion patterns
int max_record_count = peak_records * 1.5;      // Account for fragmentation
```

#### Hash Count Optimization
```cpp
// Target 50-75% load factor for optimal performance
int optimal_hash_count = max_record_count / 0.6;

// Use prime numbers for better distribution
int hash_count = next_prime(optimal_hash_count);
```

#### Field Length Considerations
```cpp
// Primary key length (typically shorter, more frequent lookups)
int primary_field_len = max_primary_key_length + 1;  // +1 for null terminator

// Secondary key length (can be longer, less frequent lookups)
int secondary_field_len = max_secondary_key_length + 1;
```

#### Record Size Planning
```cpp
// Fixed-size records for consistent performance
int max_record_size = largest_expected_record;

// Variable-size records with buffer
int max_record_size = average_record_size * 2;
```

## Performance Characteristics

### Time Complexity
- **Insert**: O(1) average, O(n) worst case (per hash table)
- **Primary Lookup**: O(1) average, O(n) worst case
- **Secondary Lookup**: O(1) average, O(n) worst case
- **Delete**: O(1) average, O(n) worst case (updates both tables)

### Space Complexity
- **Hash Tables**: 2 × (hash_count × sizeof(HashEntry) + data_count × (sizeof(DataIndexEntry) + field_len))
- **Record Storage**: max_record_count × max_record_size
- **Total Memory**: Linear in number of records and record size

### Performance Targets
- **Insert Rate**: 100,000-500,000 ops/sec (depending on record size)
- **Lookup Rate**: 500,000-1,000,000 ops/sec
- **Memory Efficiency**: ~90% utilization at optimal load factor
- **File I/O**: Zero-copy access through memory mapping

## Error Handling

### Error Codes
```cpp
enum HashMasterError {
    HASHMASTER_OK = 0,              // Success
    HASHMASTER_ERROR_NULL_POINTER = -1,      // Null parameter
    HASHMASTER_ERROR_INVALID_CONFIG = -2,    // Invalid configuration
    HASHMASTER_ERROR_INIT_FAILED = -3,       // Initialization failure
    HASHMASTER_ERROR_KEY_NOT_FOUND = -4,     // Key doesn't exist
    HASHMASTER_ERROR_KEY_EXISTS = -5,        // Duplicate key
    HASHMASTER_ERROR_NO_SPACE = -6,          // Storage full
    HASHMASTER_ERROR_FILE_ERROR = -7,        // File I/O error
    HASHMASTER_ERROR_MEMORY_ERROR = -8,      // Memory allocation failure
    HASHMASTER_ERROR_LOCK_ERROR = -9,        // Threading error
    HASHMASTER_ERROR_CORRUPTION = -10,       // Data corruption detected
    HASHMASTER_ERROR_VERSION_MISMATCH = -11  // File version mismatch
};
```

### Error Handling Patterns
```cpp
// Initialize with error checking
HashMaster hashMaster(config);
int result = hashMaster.init();
if (result != HASHMASTER_OK) {
    switch (result) {
        case HASHMASTER_ERROR_FILE_ERROR:
            // Handle file access issues
            break;
        case HASHMASTER_ERROR_MEMORY_ERROR:
            // Handle memory allocation failure
            break;
        default:
            // Handle other errors
            break;
    }
}

// Safe record operations
result = hashMaster.put(primary_key, secondary_key, data, len);
if (result == HASHMASTER_ERROR_KEY_EXISTS) {
    // Handle duplicate key - use update instead
} else if (result == HASHMASTER_ERROR_NO_SPACE) {
    // Handle storage full - cleanup or resize
}
```

## Advanced Features

### Configuration Recovery
```cpp
// Load existing database without original parameters
HashMasterConfig recovered_config = get_config_from_hashmaster("old_database");

// Create new HashMaster with recovered configuration
HashMaster hashMaster(recovered_config);
hashMaster.init();  // Opens existing files
```

### File Integrity Validation
```cpp
// Automatic validation on initialization
// Magic number: 0x484D5354 ("HMST")
// Version checking and compatibility validation

// Manual integrity check
bool is_valid = hashMaster.validate_integrity();
```

### Batch Operations
```cpp
// Efficient batch loading
hashMaster.begin_batch();
for (const auto& record : records) {
    hashMaster.put(record.primary, record.secondary, record.data, record.len);
}
hashMaster.end_batch();  // Flush to storage
```

### Statistical Analysis
```cpp
// Comprehensive performance analysis
HashTableStats primary_stats = hashMaster.get_primary_stats();
HashTableStats secondary_stats = hashMaster.get_secondary_stats();

// Calculate efficiency metrics
double primary_efficiency = (double)primary_stats.used_slots / primary_stats.total_slots;
double collision_rate = (double)primary_stats.collision_count / primary_stats.used_slots;

// Optimization recommendations
if (primary_efficiency > 0.85) {
    // Recommend resize
} else if (primary_stats.max_chain_length > 5) {
    // Recommend better hash function or more buckets
}
```

## Best Practices

### 1. **Proper Configuration**
```cpp
// Use realistic sizing with growth buffer
HashMasterConfig config;
config._max_record_count = expected_count * 1.3;
config._max_record_size = max_expected_size;
config._hash_count = next_prime(config._max_record_count / 0.7);
config._filename = "my_database";
config._use_lock = true;  // Enable for multi-threaded access
```

### 2. **Error Checking**
```cpp
// Always check return values
int result = hashMaster.put(primary, secondary, data, len);
if (result != HASHMASTER_OK) {
    handle_error(result);
}

// Validate pointers before use
char* data = hashMaster.get_by_primary(key);
if (data != nullptr) {
    // Process data
}
```

### 3. **Memory Management**
```cpp
// HashMaster manages memory automatically
// No manual cleanup required for returned pointers
// Memory is valid until next modification operation
```

### 4. **Performance Monitoring**
```cpp
// Regular health checks
if (hashMaster.get_record_count() % 10000 == 0) {
    HashTableStats stats = hashMaster.get_primary_stats();
    if (stats.load_factor > 0.8 || stats.max_chain_length > 5) {
        log_performance_warning(stats);
    }
}
```

### 5. **Key Design**
```cpp
// Use consistent key formats
std::string format_primary_key(const std::string& symbol, const std::string& exchange) {
    return symbol + "." + exchange;  // e.g., "AAPL.NASDAQ"
}

// Avoid very long keys for better performance
// Keep primary keys shorter than secondary keys if possible
```

## Integration Examples

### Financial Data Processing
```cpp
#include "HashMaster.h"
#include "BinaryRecord.h"

// 1. Configure for equity master data
HashMasterConfig config;
config._max_record_count = 100000;       // 100K equities
config._max_record_size = 512;           // 512 bytes per record
config._hash_count = 150007;             // Prime number near 100K/0.67
config._primary_field_len = 32;          // RIC code length
config._secondary_field_len = 16;        // Symbol code length
config._filename = "equity_master";
config._use_lock = true;

// 2. Initialize HashMaster
HashMaster hashMaster(config);
if (hashMaster.init() != HASHMASTER_OK) {
    return -1;
}
hashMaster.clear();

// 3. Load master data
// Primary key: RIC (e.g., "AAPL.O")
// Secondary key: Symbol (e.g., "AAPL")
std::string ric = "AAPL.O";
std::string symbol = "AAPL";
BinaryRecord record = create_equity_record("AAPL.O", "Apple Inc", ...);
char* record_data = record.getBuffer();
int record_size = record.getSize();

hashMaster.put(ric.c_str(), symbol.c_str(), record_data, record_size);

// 4. Real-time lookups
char* apple_data = hashMaster.get_by_primary("AAPL.O");
char* apple_by_symbol = hashMaster.get_by_secondary("AAPL");
```

### High-Frequency Trading System
```cpp
// Configure for tick data storage
HashMasterConfig tick_config;
tick_config._max_record_count = 1000000;  // 1M ticks
tick_config._max_record_size = 128;       // Compact tick record
tick_config._hash_count = 1500007;        // Large prime
tick_config._primary_field_len = 24;      // Timestamp-based key
tick_config._secondary_field_len = 16;    // Symbol key
tick_config._filename = "tick_data";
tick_config._use_lock = false;            // Single-threaded for speed

HashMaster tickStore(tick_config);
tickStore.init();

// Store tick with composite key
std::string tick_key = format_tick_key(symbol, timestamp, sequence);
tickStore.put(tick_key.c_str(), symbol.c_str(), tick_data, tick_size);
```

## Debugging and Diagnostics

### Using HashMaster Inspector
```bash
# Complete database analysis
./hashmaster_inspector --from-file equity_master --all

# Performance testing with 50K operations
./hashmaster_inspector --from-file equity_master --perf 50000

# Health check only
./hashmaster_inspector --from-file problematic_db --health --diagnose
```

### Using HashMaster Viewer
```bash
# View records with specification formatting
./hashmaster_viewer equity_master --spec config/equity_spec.txt EQUITY_MASTER --list 20

# Search specific records
./hashmaster_viewer equity_master --search-primary "AAPL.O"
./hashmaster_viewer equity_master --search-secondary "AAPL"

# Show database summary
./hashmaster_viewer equity_master --summary
```

### Statistics Interpretation

#### Load Factor Analysis
```cpp
HashTableStats primary = hashMaster.get_primary_stats();
HashTableStats secondary = hashMaster.get_secondary_stats();

// Primary table analysis (more critical - frequent lookups)
if (primary.load_factor > 0.85) {
    // Consider increasing hash_count
}
if (primary.max_chain_length > 3) {
    // Hash distribution may need improvement
}

// Secondary table analysis
if (secondary.collision_count > primary.collision_count * 1.5) {
    // Secondary keys may have poor distribution
}
```

#### Performance Indicators
```cpp
// Calculate operational efficiency
double insert_efficiency = successful_inserts / total_insert_attempts;
double lookup_success_rate = successful_lookups / total_lookup_attempts;

// Memory utilization
double memory_efficiency = (primary.used_slots + secondary.used_slots) / 
                          (primary.total_slots + secondary.total_slots);
```

## Troubleshooting

### Common Issues

#### 1. **Initialization Failures**
```bash
# Symptoms: init() returns error
./hashmaster_inspector --from-file failed_db --health

# Common causes:
# - File permission issues
# - Insufficient disk space
# - Corrupted header
# - Version mismatch
```

#### 2. **Performance Degradation**
```bash
# Symptoms: Slow operations
./hashmaster_inspector --from-file slow_db --diagnose --perf 10000

# Analysis points:
# - Load factor > 85%
# - High collision rates
# - Long hash chains
# - Fragmented record storage
```

#### 3. **Memory Issues**
```bash
# Symptoms: Memory allocation errors
# Check configuration:
./hashmaster_inspector --from-file memory_db --config

# Solutions:
# - Reduce max_record_count
# - Reduce max_record_size
# - Check available system memory
# - Verify mmap limits
```

#### 4. **Key Conflicts**
```cpp
// Duplicate key errors during insertion
int result = hashMaster.put(primary, secondary, data, len);
if (result == HASHMASTER_ERROR_KEY_EXISTS) {
    // Either update existing record or generate unique key
    result = hashMaster.del(primary);  // Delete old
    result = hashMaster.put(primary, secondary, new_data, len);  // Insert new
}
```

#### 5. **File Corruption**
```bash
# Symptoms: Magic number mismatch, read errors
# Recovery steps:
# 1. Backup existing files
# 2. Try recovery with inspector
./hashmaster_inspector --from-file corrupt_db --health

# 3. If recovery fails, recreate from backup data
```

### Maintenance Operations

#### Database Compaction
```cpp
// Currently manual process - copy to new database
HashMaster old_db(old_config);
HashMaster new_db(new_config);

// Iterate through all records and copy active ones
// This removes deleted record slots and optimizes storage
```

#### Configuration Migration
```cpp
// Load old configuration
HashMasterConfig old_config = get_config_from_hashmaster("old_db");

// Create new configuration with updated parameters
HashMasterConfig new_config = old_config;
new_config._max_record_count *= 2;  // Double capacity
new_config._hash_count = next_prime(new_config._max_record_count / 0.7);

// Create new database and migrate data
HashMaster new_db(new_config);
migrate_data(old_db, new_db);
```

## Performance Optimization

### Hash Table Tuning
- **Optimal Load Factor**: 50-75% for best performance
- **Hash Count**: Use prime numbers for better distribution
- **Key Distribution**: Ensure keys are well-distributed across hash space

### Memory Access Patterns
- **Sequential Access**: Optimize for cache locality
- **Record Size**: Keep records small for better cache utilization
- **Field Access**: Place frequently accessed fields early in record

### Threading Considerations
- **Single-Threaded**: Disable locking for maximum speed
- **Read-Heavy**: Use read-write locks for concurrent access
- **Write-Heavy**: Consider partitioning across multiple databases

## See Also

- [Master.md](Master.md) - Abstract base class interface
- [MemoryMaster.md](MemoryMaster.md) - Memory-based implementation for simulation
- [HashTable.md](HashTable.md) - Low-level hash table implementation
- [BinaryRecord.md](BinaryRecord.md) - Binary record processing system
- [README.md](README.md) - Project overview and setup guide