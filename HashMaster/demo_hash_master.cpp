#include "HashMaster.h"
#include "BinaryRecord.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <iomanip>

// Sample record structure for testing
struct SampleRecord {
    char symbol[16];
    char exchange[8];
    double price;
    long volume;
    int timestamp;

    SampleRecord() : price(0.0), volume(0), timestamp(0) {
        memset(symbol, 0, sizeof(symbol));
        memset(exchange, 0, sizeof(exchange));
    }

    SampleRecord(const std::string& sym, const std::string& ex, double p, long v, int t)
        : price(p), volume(v), timestamp(t) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        strncpy(exchange, ex.c_str(), sizeof(exchange) - 1);
    }
};

// Performance testing utilities
class PerformanceTester {
private:
    std::mt19937 rng;
    std::uniform_real_distribution<double> price_dist;
    std::uniform_int_distribution<long> volume_dist;
    std::uniform_int_distribution<int> timestamp_dist;

    std::vector<std::string> symbols;
    std::vector<std::string> exchanges;

public:
    PerformanceTester() : rng(42), price_dist(1.0, 1000.0), volume_dist(100, 1000000),
                         timestamp_dist(1000000, 9999999) {
        // Initialize test data
        symbols = {"AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "META", "NVDA", "AMD", "INTC", "ORCL"};
        exchanges = {"NASDAQ", "NYSE", "BATS", "EDGX"};
    }

    SampleRecord generate_random_record(int index) {
        std::string symbol = symbols[index % symbols.size()] + std::to_string(index / symbols.size());
        std::string exchange = exchanges[index % exchanges.size()];

        return SampleRecord(symbol, exchange, price_dist(rng), volume_dist(rng), timestamp_dist(rng));
    }

    std::string generate_primary_key(const SampleRecord& record) {
        return std::string(record.symbol) + "." + std::string(record.exchange);
    }

    std::string generate_secondary_key(const SampleRecord& record) {
        return std::string(record.symbol);
    }
};

// Demo functions
void demo_basic_operations() {
    std::cout << "=== Basic Operations Demo ===" << std::endl;

    // Create HashMaster with configuration
    HashMasterConfig config;
    config._max_record_count = 1000;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 100;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._use_lock = true;
    config._filename = "demo_hashmaster";
    config._log_level = LOG_INFO;

    // Remove existing files
    system("rm -f mmap/demo_hashmaster_*");

    HashMaster hash_master(config);

    // Initialize
    int result = hash_master.init();
    if (result != MASTER_OK) {
        std::cerr << "Failed to initialize HashMaster: " << result << std::endl;
        return;
    }

    std::cout << "HashMaster initialized successfully" << std::endl;

    // Clear any existing data
    hash_master.clear();

    // Create some sample records
    SampleRecord records[] = {
        SampleRecord("AAPL", "NASDAQ", 150.25, 1000000, 93000),
        SampleRecord("GOOGL", "NASDAQ", 2800.50, 500000, 93001),
        SampleRecord("MSFT", "NASDAQ", 310.75, 750000, 93002),
        SampleRecord("TSLA", "NASDAQ", 245.30, 2000000, 93003),
        SampleRecord("AMZN", "NASDAQ", 3200.80, 300000, 93004)
    };

    PerformanceTester tester;

    // Insert records
    std::cout << "\nInserting sample records..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = tester.generate_primary_key(records[i]);
        std::string secondary_key = tester.generate_secondary_key(records[i]);

        result = hash_master.put(primary_key.c_str(), secondary_key.c_str(),
                                reinterpret_cast<const char*>(&records[i]), sizeof(SampleRecord));

        if (result == MASTER_OK) {
            std::cout << "  Inserted: " << primary_key << " -> " << secondary_key << std::endl;
        } else {
            std::cerr << "  Failed to insert: " << primary_key << ", error: " << result << std::endl;
        }
    }

    // Retrieve records by primary key
    std::cout << "\nRetrieving records by primary key..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = tester.generate_primary_key(records[i]);

        char* data = hash_master.get_by_primary(primary_key.c_str());
        if (data) {
            SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
            std::cout << "  Found: " << primary_key << " -> Price: " << retrieved->price
                      << ", Volume: " << retrieved->volume << std::endl;
        } else {
            std::cout << "  Not found: " << primary_key << std::endl;
        }
    }

    // Retrieve records by secondary key
    std::cout << "\nRetrieving records by secondary key..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string secondary_key = tester.generate_secondary_key(records[i]);

        char* data = hash_master.get_by_secondary(secondary_key.c_str());
        if (data) {
            SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
            std::cout << "  Found: " << secondary_key << " -> Exchange: " << retrieved->exchange
                      << ", Price: " << retrieved->price << std::endl;
        } else {
            std::cout << "  Not found: " << secondary_key << std::endl;
        }
    }

    // Display statistics
    std::cout << "\nStatistics after insertions:" << std::endl;
    hash_master.display_statistics();

    // Delete some records
    std::cout << "\nDeleting records..." << std::endl;
    for (size_t i = 0; i < 2; ++i) {
        std::string primary_key = tester.generate_primary_key(records[i]);
        result = hash_master.del(primary_key.c_str());

        if (result == MASTER_OK) {
            std::cout << "  Deleted: " << primary_key << std::endl;
        } else {
            std::cerr << "  Failed to delete: " << primary_key << ", error: " << result << std::endl;
        }
    }

    // Final statistics
    std::cout << "\nFinal statistics:" << std::endl;
    hash_master.display_statistics();
}

void demo_performance_test() {
    std::cout << "\n=== Performance Test Demo ===" << std::endl;

    // Create HashMaster with larger configuration
    HashMasterConfig config;
    config._max_record_count = 10000;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 1000;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._use_lock = false; // Disable locking for performance test
    config._filename = "demo_perf_hashmaster";
    config._log_level = LOG_WARNING; // Reduce logging

    // Remove existing files
    system("rm -f mmap/demo_perf_hashmaster_*");

    HashMaster hash_master(config);

    if (hash_master.init() != MASTER_OK) {
        std::cerr << "Failed to initialize HashMaster for performance test" << std::endl;
        return;
    }

    hash_master.clear();

    PerformanceTester tester;
    const int num_records = 1000; // Reduced for file-based storage

    // Measure insertion performance
    auto start_time = std::chrono::high_resolution_clock::now();

    std::cout << "Inserting " << num_records << " records..." << std::endl;
    for (int i = 0; i < num_records; ++i) {
        SampleRecord record = tester.generate_random_record(i);
        std::string primary_key = tester.generate_primary_key(record);
        std::string secondary_key = tester.generate_secondary_key(record);

        hash_master.put(primary_key.c_str(), secondary_key.c_str(),
                       reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    std::cout << "Insertion completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Average insertion time: " << static_cast<double>(duration.count()) / num_records
              << " microseconds per record" << std::endl;
    std::cout << "Insertion rate: " << static_cast<double>(num_records) / duration.count() * 1000000
              << " records per second" << std::endl;

    // Measure lookup performance
    start_time = std::chrono::high_resolution_clock::now();
    int found_count = 0;

    std::cout << "\nPerforming " << num_records << " primary key lookups..." << std::endl;
    for (int i = 0; i < num_records; ++i) {
        SampleRecord record = tester.generate_random_record(i);
        std::string primary_key = tester.generate_primary_key(record);

        char* data = hash_master.get_by_primary(primary_key.c_str());
        if (data) {
            found_count++;
        }
    }

    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    std::cout << "Lookup completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << found_count << " out of " << num_records << " records" << std::endl;
    std::cout << "Average lookup time: " << static_cast<double>(duration.count()) / num_records
              << " microseconds per lookup" << std::endl;
    std::cout << "Lookup rate: " << static_cast<double>(num_records) / duration.count() * 1000000
              << " lookups per second" << std::endl;

    // Display final statistics
    std::cout << "\nPerformance test statistics:" << std::endl;
    hash_master.display_statistics();
}

void demo_persistence() {
    std::cout << "\n=== Persistence Demo ===" << std::endl;

    // Configuration for persistence test
    HashMasterConfig config;
    config._max_record_count = 100;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 50;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._filename = "demo_persist_hashmaster";
    config._log_level = LOG_INFO;

    PerformanceTester tester;

    // Phase 1: Create and populate database
    {
        std::cout << "Phase 1: Creating and populating database..." << std::endl;

        // Remove existing files
        system("rm -f mmap/demo_persist_hashmaster_*");

        HashMaster hash_master(config);
        hash_master.init();
        hash_master.clear();

        // Insert test data
        for (int i = 0; i < 10; ++i) {
            SampleRecord record = tester.generate_random_record(i);
            std::string primary_key = tester.generate_primary_key(record);
            std::string secondary_key = tester.generate_secondary_key(record);

            int result = hash_master.put(primary_key.c_str(), secondary_key.c_str(),
                                        reinterpret_cast<const char*>(&record), sizeof(SampleRecord));

            if (result == MASTER_OK) {
                std::cout << "  Stored: " << primary_key << std::endl;
            }
        }

        std::cout << "  Database populated with " << hash_master.get_record_count() << " records" << std::endl;
        // hash_master destructor will sync data to files
    }

    // Phase 2: Reload and verify data
    {
        std::cout << "\nPhase 2: Reloading database from files..." << std::endl;

        HashMaster hash_master(config);
        hash_master.init(); // Should load existing data

        std::cout << "  Database reloaded with " << hash_master.get_record_count() << " records" << std::endl;

        // Verify data by reading some records
        for (int i = 0; i < 5; ++i) {
            SampleRecord record = tester.generate_random_record(i);
            std::string primary_key = tester.generate_primary_key(record);

            char* data = hash_master.get_by_primary(primary_key.c_str());
            if (data) {
                SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
                std::cout << "  Verified: " << primary_key << " -> Price: " << retrieved->price << std::endl;
            } else {
                std::cout << "  Missing: " << primary_key << std::endl;
            }
        }

        hash_master.display_statistics();
    }
}

void demo_primary_key_only() {
    std::cout << "\n=== Primary Key Only Demo ===" << std::endl;

    // Create HashMaster with configuration for primary-key-only mode
    HashMasterConfig config;
    config._max_record_count = 500;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 50;
    config._primary_field_len = 32;
    config._secondary_field_len = 0;  // Disable secondary indexing
    config._use_lock = true;
    config._filename = "demo_primary_only";
    config._log_level = LOG_INFO;

    // Remove existing files
    system("rm -f mmap/demo_primary_only_*");

    HashMaster hash_master(config);

    // Initialize
    int result = hash_master.init();
    if (result != MASTER_OK) {
        std::cerr << "Failed to initialize HashMaster: " << result << std::endl;
        return;
    }

    std::cout << "HashMaster initialized for primary-key-only operations" << std::endl;
    std::cout << "Configuration: secondary_field_len = " << config._secondary_field_len
              << " (secondary indexing " << (config.use_secondary_index() ? "enabled" : "disabled") << ")" << std::endl;

    // Clear any existing data
    hash_master.clear();

    // Create some sample records with unique primary keys
    SampleRecord records[] = {
        SampleRecord("TRADE_001", "NYSE", 100.50, 1500, 1001),
        SampleRecord("TRADE_002", "NASDAQ", 250.75, 2000, 1002),
        SampleRecord("TRADE_003", "BATS", 75.25, 1200, 1003),
        SampleRecord("TRADE_004", "EDGX", 180.90, 3000, 1004),
        SampleRecord("TRADE_005", "NYSE", 320.45, 1800, 1005),
        SampleRecord("ORDER_001", "NASDAQ", 95.60, 500, 1006),
        SampleRecord("ORDER_002", "NYSE", 145.30, 2500, 1007)
    };

    PerformanceTester tester;

    // Insert records using only primary keys (no secondary key indexing)
    std::cout << "\nInserting records with primary keys only..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = std::string(records[i].symbol);  // Use symbol as primary key

        // In primary-key-only mode (secondary_field_len = 0), secondary key can be anything
        result = hash_master.put(primary_key.c_str(), nullptr,
                                reinterpret_cast<const char*>(&records[i]), sizeof(SampleRecord));

        if (result == MASTER_OK) {
            std::cout << "  Inserted: " << primary_key << " -> Price: " << records[i].price
                      << ", Volume: " << records[i].volume << std::endl;
        } else {
            std::cerr << "  Failed to insert: " << primary_key << ", error: " << result << std::endl;
        }
    }

    // Retrieve records by primary key only
    std::cout << "\nRetrieving records by primary key..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = std::string(records[i].symbol);

        char* data = hash_master.get_by_primary(primary_key.c_str());
        if (data) {
            SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
            std::cout << "  Found: " << primary_key << " -> Exchange: " << retrieved->exchange
                      << ", Price: " << retrieved->price << ", Volume: " << retrieved->volume << std::endl;
        } else {
            std::cout << "  Not found: " << primary_key << std::endl;
        }
    }

    // Show that secondary key lookup is not available (should return null or error)
    std::cout << "\nTesting secondary key lookup (should not work)..." << std::endl;
    char* data = hash_master.get_by_secondary("NYSE");
    if (data) {
        std::cout << "  WARNING: Secondary key lookup unexpectedly succeeded" << std::endl;
    } else {
        std::cout << "  Expected: Secondary key lookup not available (primary-only mode)" << std::endl;
    }

    // Performance test with primary keys only
    std::cout << "\nPrimary-key-only performance test..." << std::endl;
    const int num_records = 200;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_records; ++i) {
        SampleRecord record = tester.generate_random_record(i);
        std::string primary_key = "PK_" + std::to_string(i);

        hash_master.put(primary_key.c_str(), nullptr,
                       reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    std::cout << "Primary-only insertion: " << duration.count() << " microseconds for "
              << num_records << " records" << std::endl;
    std::cout << "Average: " << static_cast<double>(duration.count()) / num_records
              << " microseconds per record" << std::endl;

    // Display final statistics
    std::cout << "\nPrimary-key-only statistics:" << std::endl;
    hash_master.display_statistics();

    // Delete some records using primary key
    std::cout << "\nDeleting records by primary key..." << std::endl;
    for (int i = 0; i < 3; ++i) {
        std::string primary_key = std::string(records[i].symbol);
        result = hash_master.del(primary_key.c_str());

        if (result == MASTER_OK) {
            std::cout << "  Deleted: " << primary_key << std::endl;
        } else {
            std::cerr << "  Failed to delete: " << primary_key << ", error: " << result << std::endl;
        }
    }

    std::cout << "\nFinal statistics after deletions:" << std::endl;
    hash_master.display_statistics();
}

void demo_file_info() {
    std::cout << "\n=== File Information Demo ===" << std::endl;

    // Show generated files
    std::cout << "Generated HashMaster files:" << std::endl;
    system("ls -la mmap/demo_*hashmaster* 2>/dev/null | head -20");

    // Show file sizes
    std::cout << "\nFile sizes:" << std::endl;
    system("du -h mmap/demo_*hashmaster* 2>/dev/null");
}

int main() {
    std::cout << "HashMaster Demo Application" << std::endl;
    std::cout << "===========================" << std::endl;

    try {
        // Run all demos
        demo_basic_operations();
        demo_performance_test();
        demo_persistence();
        demo_primary_key_only();
        demo_file_info();

        std::cout << "\nAll demos completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}