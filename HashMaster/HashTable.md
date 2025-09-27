# HashTable

A high-performance, memory-mapped hash table implementation with collision handling, thread safety, and comprehensive diagnostics.

## Overview

The `HashTable` class provides a persistent, file-based hash table that maps keys to data indices. It uses memory mapping for efficient file I/O and supports both character string and binary keys with configurable field lengths.

## Architecture

### Core Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Hash Index    │    │   Data Index    │    │   Statistics    │
│     Table       │────│     Table       │────│   & Metrics     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│{filename}.hash  │    │{filename}.data  │    │  Runtime Stats  │
│    index        │    │     index       │    │  & Validation   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Memory Layout

#### Hash Index Table Structure
```cpp
struct HashIndexTable {
    int _first_free_slot;          // First available slot index
    int _magic_number;             // File validation (0x48415348)
    int _version;                  // Version number
    int _hash_count;               // Number of hash buckets
    int _data_count;               // Number of data entries
    int _field_len;                // Key field length
    int _is_char_key;             // Key type flag
    int _reserved[3];             // Future expansion
    HashEntry _hash_entries[];    // Hash bucket array
};
```

#### Data Index Entry Structure
```cpp
struct DataIndexEntry {
    int occupied;          // 0: empty, 1: occupied
    int nextIndex;         // Next in collision chain
    int nextEmpty;         // Next in free list
    int dataIndex;         // Associated data index
    char value[];          // Variable-length key
};
```

## Key Features

### 1. **Dual File Storage**
- **Hash Index File** (`{filename}.hashindex`): Hash buckets and metadata
- **Data Index File** (`{filename}.dataindex`): Key storage and collision chains

### 2. **Memory Mapping**
- Zero-copy file access using `mmap()`
- Persistent storage with automatic synchronization
- Efficient memory usage for large datasets

### 3. **Collision Handling**
- **Separate Chaining**: Each bucket maintains a linked list
- **Free List Management**: Efficient slot reuse
- **Chain Length Monitoring**: Performance optimization

### 4. **Thread Safety**
- **pthread read-write locks**: Concurrent read access
- **Write exclusivity**: Atomic write operations  
- **Lock-free mode**: Single-threaded optimization

### 5. **Hash Functions**
- **DJB2 Hash**: Default high-quality hash function
- **DJB2 String Hash**: Optimized for string keys
- **Custom Hash Functions**: Pluggable hash function support

## API Reference

### Constructor
```cpp
HashTable(int hash_count, int field_len, int data_count, 
          bool use_lock = true, const char* filename = "hashtable", 
          bool is_char = false);
```

**Parameters:**
- `hash_count`: Number of hash buckets
- `field_len`: Maximum key length in bytes
- `data_count`: Maximum number of data entries
- `use_lock`: Enable thread safety (default: true)
- `filename`: Base filename for storage files
- `is_char`: True for string keys, false for binary keys

### Core Operations

#### Initialization
```cpp
int init();              // Initialize hash table
int clear();             // Clear all entries
bool is_initialized();   // Check initialization status
```

#### Data Operations
```cpp
// Primary operations
int put(const char* key, int dataIndex);  // Insert key-value mapping
int get(const char* key);                 // Retrieve data index
int del(const char* key);                 // Delete entry
int add(const char* key, int dataIndex);  // Insert with duplicate check

// Numeric key convenience wrappers
int put(short key, int dataIndex);
int put(int key, int dataIndex);
// ... similar for get, del, add
```

#### Statistics and Monitoring
```cpp
HashTableStats get_statistics();  // Get comprehensive statistics
void display_hashtable();         // Print table contents
void display_statistics();        // Print statistics
```

### Statistics Structure
```cpp
struct HashTableStats {
    int total_slots;           // Total data slots
    int used_slots;            // Occupied slots
    int free_slots;            // Available slots
    int collision_count;       // Number of collisions
    double load_factor;        // Usage percentage
    int max_chain_length;      // Longest collision chain
    int min_chain_length;      // Shortest collision chain
    double avg_chain_length;   // Average chain length
};
```

## Performance Characteristics

### Time Complexity
- **Average Case**: O(1) for all operations
- **Worst Case**: O(n) when all keys hash to same bucket
- **Typical Performance**: 
  - Insert: ~1-2 μs per operation
  - Lookup: ~0.5-1 μs per operation
  - Delete: ~1-2 μs per operation

### Space Complexity
- **Hash Index**: `sizeof(HashIndexTable) + hash_count * sizeof(HashEntry)`
- **Data Index**: `data_count * (sizeof(DataIndexEntry) + field_len)`
- **Total Memory**: Linear in number of entries and key length

### Load Factor Guidelines
- **Optimal**: 50-75% load factor
- **Acceptable**: Up to 85% load factor
- **Poor Performance**: Above 90% load factor

## Configuration Guidelines

### Sizing Recommendations

#### Hash Count Selection
```cpp
// General rule: hash_count = data_count / target_load_factor
int optimal_hash_count = data_count / 0.75;  // Target 75% load factor

// For better distribution, use prime numbers close to optimal
int hash_count = next_prime(optimal_hash_count);
```

#### Field Length Considerations
```cpp
// For string keys (is_char = true)
int field_len = max_expected_key_length + 1;  // +1 for null terminator

// For binary keys (is_char = false)  
int field_len = exact_key_size;  // Exact byte count
```

#### Data Count Planning
```cpp
// Leave room for growth
int data_count = expected_records * 1.2;  // 20% buffer

// Account for collision overhead
int data_count = expected_records * 1.5;  // With collision buffer
```

## Error Handling

### Error Codes
```cpp
enum HashTableError {
    HASH_OK = 0,                    // Success
    HASH_ERROR_NULL_POINTER = -1,   // Null parameter
    HASH_ERROR_INVALID_PARAMETER = -2,  // Invalid parameter
    HASH_ERROR_KEY_NOT_FOUND = -3,  // Key doesn't exist
    HASH_ERROR_KEY_EXISTS = -4,     // Key already exists
    HASH_ERROR_NO_SPACE = -5,       // Table full
    HASH_ERROR_FILE_ERROR = -6,     // File I/O error
    HASH_ERROR_MEMORY_ERROR = -7,   // Memory allocation failure
    HASH_ERROR_LOCK_ERROR = -8      // Locking error
};
```

### Error Handling Patterns
```cpp
// Check return values
int result = hashTable.put("key", 42);
if (result != HASH_OK) {
    switch (result) {
        case HASH_ERROR_KEY_EXISTS:
            // Handle duplicate key
            break;
        case HASH_ERROR_NO_SPACE:
            // Handle table full
            break;
        default:
            // Handle other errors
            break;
    }
}

// Safe retrieval
int dataIndex = hashTable.get("key");
if (dataIndex >= 0) {
    // Key found, dataIndex is valid
} else {
    // Key not found or error occurred
}
```

## Advanced Features

### Custom Hash Functions
```cpp
// Define custom hash function
uint32_t custom_hash(const char* key, int len) {
    // Your hash implementation
    return hash_value;
}

// Set custom hash function
hashTable.setHashFunction(custom_hash);
```

### File Integrity Validation
```cpp
// Check file consistency
bool is_valid = hashTable.validate_file_integrity();

// Magic number verification automatically performed on init()
```

### Maintenance Operations
```cpp
// Defragment deleted entries
int result = hashTable.defragment();

// Resize hash table (future enhancement)
int result = hashTable.resize(new_hash_count, new_data_count);
```

## Best Practices

### 1. **Proper Initialization**
```cpp
// Always check initialization
HashTable table(1000, 32, 10000, true, "my_table", true);
if (table.init() != HASH_OK) {
    // Handle initialization failure
}
```

### 2. **Error Checking**
```cpp
// Check all return values
int result = table.put(key, value);
if (result != HASH_OK) {
    // Handle error appropriately
}
```

### 3. **Resource Management**
```cpp
// HashTable automatically manages resources
// No manual cleanup required
```

### 4. **Performance Monitoring**
```cpp
// Regular statistics monitoring
HashTableStats stats = table.get_statistics();
if (stats.load_factor > 0.8) {
    // Consider resizing or optimization
}

if (stats.max_chain_length > 5) {
    // Consider better hash function or larger table
}
```

### 5. **Thread Safety**
```cpp
// Enable locking for multi-threaded access
HashTable table(1000, 32, 10000, true);  // use_lock = true

// Disable locking for single-threaded optimization
HashTable table(1000, 32, 10000, false); // use_lock = false
```

## Debugging and Diagnostics

### Using hashtable_inspector Tool
```bash
# Complete analysis
./hashtable_inspector my_table 1000 10000 32 --all

# Health check only
./hashtable_inspector my_table 1000 10000 32 --health

# Performance diagnostics
./hashtable_inspector my_table 1000 10000 32 --diagnose
```

### Statistics Interpretation

#### Load Factor Analysis
- **< 50%**: Under-utilized, consider smaller table
- **50-75%**: Optimal performance range
- **75-85%**: Acceptable with monitoring
- **> 85%**: Performance degradation likely

#### Collision Analysis  
- **Max Chain Length < 3**: Excellent hash distribution
- **Max Chain Length 3-5**: Good performance
- **Max Chain Length > 5**: Consider optimization

#### Performance Indicators
```cpp
HashTableStats stats = table.get_statistics();

// Calculate collision rate
double collision_rate = (double)stats.collision_count / stats.used_slots * 100.0;

// Optimal: < 15% collision rate
// Acceptable: < 30% collision rate
// Poor: > 30% collision rate
```

## Integration Examples

### With HashMaster
```cpp
// HashTable is used internally by HashMaster
// No direct integration needed - HashMaster manages HashTables
```

### Standalone Usage
```cpp
#include "HashTable.h"

int main() {
    // Create hash table: 1000 buckets, 32-byte keys, 10000 entries
    HashTable table(1000, 32, 10000, true, "inventory", true);
    
    if (table.init() != HASH_OK) {
        return -1;
    }
    
    // Store product information (key -> inventory_index mapping)
    table.put("WIDGET_001", 0);    // Maps to inventory slot 0
    table.put("GADGET_042", 1);    // Maps to inventory slot 1
    
    // Retrieve product
    int inventory_index = table.get("WIDGET_001");
    if (inventory_index >= 0) {
        // Use inventory_index to access actual product data
    }
    
    // Monitor performance
    HashTableStats stats = table.get_statistics();
    printf("Load factor: %.1f%%\n", stats.load_factor);
    printf("Collisions: %d\n", stats.collision_count);
    
    return 0;
}
```

## Troubleshooting

### Common Issues

#### 1. **High Collision Rate**
```bash
# Symptoms: Poor performance, long chain lengths
./hashtable_inspector problem_table <params> --health

# Solutions:
# - Increase hash_count (more buckets)
# - Use better hash function
# - Check key distribution
```

#### 2. **File Corruption**
```bash
# Symptoms: Init failure, magic number mismatch
./hashtable_inspector corrupt_table <params> --config

# Solutions:
# - Delete .hashindex and .dataindex files
# - Recreate table from backup
# - Check filesystem integrity
```

#### 3. **Performance Degradation**
```bash
# Symptoms: Slow operations
./hashtable_inspector slow_table <params> --diagnose

# Common causes:
# - High load factor (> 85%)
# - Long collision chains
# - Poor hash distribution
```

#### 4. **Memory Issues**
```bash
# Symptoms: HASH_ERROR_MEMORY_ERROR
# Solutions:
# - Check available memory
# - Reduce data_count
# - Check file permissions
```

## See Also

- [HashMaster.md](HashMaster.md) - Dual-indexed hash table system
- [BinaryRecord.md](BinaryRecord.md) - Binary record processing
- [README.md](README.md) - Project overview and setup