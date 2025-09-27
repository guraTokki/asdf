#include "MemoryMaster.h"
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

    // Create memory master with configuration
    MemoryMasterConfig config;
    config._max_record_count = 1000;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 100;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._use_lock = true;
    config._log_level = LOG_INFO;

    MemoryMaster memory_master(config);

    // Initialize
    int result = memory_master.init();
    if (result != MASTER_OK) {
        std::cerr << "Failed to initialize MemoryMaster: " << result << std::endl;
        return;
    }

    std::cout << "MemoryMaster initialized successfully" << std::endl;

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

        result = memory_master.put(primary_key.c_str(), secondary_key.c_str(),
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

        char* data = memory_master.get_by_primary(primary_key.c_str());
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

        char* data = memory_master.get_by_secondary(secondary_key.c_str());
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
    memory_master.display_statistics();

    // Delete some records
    std::cout << "\nDeleting records..." << std::endl;
    for (size_t i = 0; i < 2; ++i) {
        std::string primary_key = tester.generate_primary_key(records[i]);
        result = memory_master.del(primary_key.c_str());

        if (result == MASTER_OK) {
            std::cout << "  Deleted: " << primary_key << std::endl;
        } else {
            std::cerr << "  Failed to delete: " << primary_key << ", error: " << result << std::endl;
        }
    }

    // Final statistics
    std::cout << "\nFinal statistics:" << std::endl;
    memory_master.display_statistics();
}

void demo_performance_test() {
    std::cout << "\n=== Performance Test Demo ===" << std::endl;

    // Create memory master with larger configuration
    MemoryMasterConfig config;
    config._max_record_count = 10000;
    config._max_record_size = sizeof(SampleRecord);
    config._hash_count = 1000;
    config._primary_field_len = 32;
    config._secondary_field_len = 16;
    config._use_lock = false; // Disable locking for performance test
    config._log_level = LOG_WARNING; // Reduce logging

    MemoryMaster memory_master(config);

    if (memory_master.init() != MASTER_OK) {
        std::cerr << "Failed to initialize MemoryMaster for performance test" << std::endl;
        return;
    }

    PerformanceTester tester;
    const int num_records = 5000;

    // Measure insertion performance
    auto start_time = std::chrono::high_resolution_clock::now();

    std::cout << "Inserting " << num_records << " records..." << std::endl;
    for (int i = 0; i < num_records; ++i) {
        SampleRecord record = tester.generate_random_record(i);
        std::string primary_key = tester.generate_primary_key(record);
        std::string secondary_key = tester.generate_secondary_key(record);

        memory_master.put(primary_key.c_str(), secondary_key.c_str(),
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

        char* data = memory_master.get_by_primary(primary_key.c_str());
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
    memory_master.display_statistics();
}

void demo_iterator() {
    std::cout << "\n=== Iterator Demo ===" << std::endl;

    MemoryMasterConfig config;
    config._max_record_count = 100;
    config._max_record_size = sizeof(SampleRecord);
    config._log_level = LOG_WARNING;

    MemoryMaster memory_master(config);
    memory_master.init();

    PerformanceTester tester;

    // Insert some test records
    std::cout << "Inserting test records for iteration..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        SampleRecord record = tester.generate_random_record(i);
        std::string primary_key = tester.generate_primary_key(record);
        std::string secondary_key = tester.generate_secondary_key(record);

        memory_master.put(primary_key.c_str(), secondary_key.c_str(),
                         reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
    }

    // Iterate through all records
    std::cout << "\nIterating through all records:" << std::endl;
    auto iterator = memory_master.create_iterator();
    int count = 0;

    while (iterator && iterator->has_next()) {
        char* data = iterator->next();
        if (data) {
            SampleRecord* record = reinterpret_cast<SampleRecord*>(data);
            std::cout << "  Record " << count << ": " << record->symbol
                      << "." << record->exchange << " Price: " << std::fixed
                      << std::setprecision(2) << record->price << std::endl;
            count++;
        }
    }

    std::cout << "Total records iterated: " << count << std::endl;
}

void demo_memory_comparison() {
    std::cout << "\n=== Memory Usage Comparison Demo ===" << std::endl;

    // Test different configurations
    std::vector<int> record_counts = {100, 1000, 5000, 10000};

    for (int count : record_counts) {
        MemoryMasterConfig config;
        config._max_record_count = count;
        config._max_record_size = sizeof(SampleRecord);
        config._hash_count = count / 10;
        config._log_level = LOG_ERROR; // Minimal logging

        MemoryMaster memory_master(config);
        memory_master.init();

        PerformanceTester tester;

        // Fill to 75% capacity
        int records_to_insert = count * 3 / 4;
        for (int i = 0; i < records_to_insert; ++i) {
            SampleRecord record = tester.generate_random_record(i);
            std::string primary_key = tester.generate_primary_key(record);
            std::string secondary_key = tester.generate_secondary_key(record);

            memory_master.put(primary_key.c_str(), secondary_key.c_str(),
                             reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
        }

        auto stats = memory_master.get_memory_statistics();

        std::cout << "Configuration: " << count << " max records, " << records_to_insert << " inserted" << std::endl;
        std::cout << "  Memory usage: " << stats.memory_usage_bytes << " bytes ("
                  << static_cast<double>(stats.memory_usage_bytes) / 1024 << " KB)" << std::endl;
        std::cout << "  Utilization: " << std::fixed << std::setprecision(1)
                  << stats.record_utilization * 100 << "%" << std::endl;
        std::cout << "  Primary load factor: " << std::fixed << std::setprecision(3)
                  << stats.load_factor_primary << std::endl;
        std::cout << "  Secondary load factor: " << std::fixed << std::setprecision(3)
                  << stats.load_factor_secondary << std::endl;
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "MemoryMaster Demo Application" << std::endl;
    std::cout << "=============================" << std::endl;

    try {
        // Run all demos
        demo_basic_operations();
        demo_performance_test();
        demo_iterator();
        demo_memory_comparison();

        std::cout << "\nAll demos completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}