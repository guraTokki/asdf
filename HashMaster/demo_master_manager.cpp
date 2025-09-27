#include "MasterManager.h"
#include "BinaryRecord.h"
#include <iostream>
#include <chrono>
#include <cstring>

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

void demo_master_manager_basics() {
    std::cout << "=== MasterManager Basic Demo ===" << std::endl;

    // Create MasterManager
    MasterManager manager(LOG_INFO);

    // Load master configurations
    if (!manager.loadMasterConfigs("config/MASTERs")) {
        std::cerr << "Failed to load master configurations" << std::endl;
        return;
    }

    // Display summary
    manager.displayMasterSummary();

    // Get master names
    auto names = manager.getMasterNames();
    std::cout << "\nAvailable masters:" << std::endl;
    for (const auto& name : names) {
        std::cout << "  - " << name << std::endl;
    }

    // Get masters by type
    auto hash_masters = manager.getMasterNamesByType(MasterType::HASH_MASTER);
    auto memory_masters = manager.getMasterNamesByType(MasterType::MEMORY_MASTER);

    std::cout << "\nHashMaster instances: " << hash_masters.size() << std::endl;
    std::cout << "MemoryMaster instances: " << memory_masters.size() << std::endl;
}

void demo_master_operations(MasterManager& manager, const std::string& master_name) {
    std::cout << "\n=== Testing Master: " << master_name << " ===" << std::endl;

    // Display master info
    manager.displayMasterInfo(master_name);

    // Get master instance
    Master* master = manager.getMaster(master_name);
    if (!master) {
        std::cerr << "Failed to get master: " << master_name << std::endl;
        return;
    }

    std::cout << "\nMaster created and initialized successfully" << std::endl;

    // Clear existing data
    master->clear();

    // Create sample records
    SampleRecord records[] = {
        SampleRecord("7203", "T", 2850.0, 1000, 1001),
        SampleRecord("6758", "T", 1245.5, 2000, 1002),
        SampleRecord("9434", "T", 3200.0, 1500, 1003),
        SampleRecord("AAPL", "O", 150.25, 5000, 1004),
        SampleRecord("GOOGL", "O", 2800.50, 500, 1005)
    };

    // Insert records
    std::cout << "\nInserting records..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = std::string(records[i].symbol);
        // Create secondary key in 'symbol.exchange' format
        std::string secondary_key = std::string(records[i].symbol) + "." + std::string(records[i].exchange);

        int result = master->put(primary_key.c_str(), secondary_key.c_str(),
                                reinterpret_cast<const char*>(&records[i]), sizeof(SampleRecord));

        if (result == MASTER_OK) {
            std::cout << "  Inserted: " << primary_key << " -> " << secondary_key
                      << " (Price: " << records[i].price << ")" << std::endl;
        } else {
            std::cerr << "  Failed to insert: " << primary_key << ", error: " << result << std::endl;
        }
    }

    // Retrieve records by primary key
    std::cout << "\nRetrieving records by primary key..." << std::endl;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        std::string primary_key = std::string(records[i].symbol);

        char* data = master->get_by_primary(primary_key.c_str());
        if (data) {
            SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
            std::cout << "  Found: " << primary_key << " -> Exchange: " << retrieved->exchange
                      << ", Price: " << retrieved->price << ", Volume: " << retrieved->volume << std::endl;
        } else {
            std::cout << "  Not found: " << primary_key << std::endl;
        }
    }

    // Retrieve records by secondary key (if secondary indexing is enabled)
    const MasterInfo* info = manager.getMasterInfo(master_name);
    if (info && info->config.use_secondary_index()) {
        std::cout << "\nRetrieving records by secondary key..." << std::endl;
        std::vector<std::string> secondary_keys = {"7203.T", "AAPL.O", "GOOGL.O"};

        for (const auto& secondary_key : secondary_keys) {
            char* data = master->get_by_secondary(secondary_key.c_str());
            if (data) {
                SampleRecord* retrieved = reinterpret_cast<SampleRecord*>(data);
                std::cout << "  Found by secondary key " << secondary_key << ": " << retrieved->symbol
                          << " (Price: " << retrieved->price << ")" << std::endl;
            } else {
                std::cout << "  Not found by secondary key: " << secondary_key << std::endl;
            }
        }
    } else {
        std::cout << "\nSecondary indexing disabled for this master" << std::endl;
    }

    // Display statistics
    std::cout << "\nMaster statistics:" << std::endl;
    auto stats = master->get_statistics();
    std::cout << "  Total records: " << stats.total_records << std::endl;
    std::cout << "  Used records: " << stats.used_records << std::endl;
    std::cout << "  Free records: " << stats.free_records << std::endl;
    std::cout << "  Record utilization: " << stats.record_utilization << "%" << std::endl;

    // Test deletion and verify secondary hash table cleanup
    std::cout << "\nTesting deletion with secondary hash table cleanup..." << std::endl;

    // First, verify records exist in both hash tables before deletion
    std::cout << "\nBefore deletion - verifying records exist:" << std::endl;
    std::string test_primary = "AAPL";
    std::string test_secondary = "AAPL.O";

    char* data_by_primary = master->get_by_primary(test_primary.c_str());
    char* data_by_secondary = master->get_by_secondary(test_secondary.c_str());

    std::cout << "  Primary key '" << test_primary << "' found: " << (data_by_primary ? "YES" : "NO") << std::endl;
    std::cout << "  Secondary key '" << test_secondary << "' found: " << (data_by_secondary ? "YES" : "NO") << std::endl;

    if (data_by_primary && data_by_secondary) {
        SampleRecord* rec1 = reinterpret_cast<SampleRecord*>(data_by_primary);
        SampleRecord* rec2 = reinterpret_cast<SampleRecord*>(data_by_secondary);
        std::cout << "  Both lookups return same record: " << (rec1 == rec2 ? "YES" : "NO") << std::endl;
        std::cout << "  Record data: " << rec1->symbol << ", Price: " << rec1->price << std::endl;
    }

    // Now delete by primary key
    std::cout << "\nDeleting record by primary key '" << test_primary << "'..." << std::endl;
    int result = master->del(test_primary.c_str());

    if (result == MASTER_OK) {
        std::cout << "  Deletion successful" << std::endl;

        // Verify record is gone from both hash tables
        std::cout << "\nAfter deletion - verifying records are gone:" << std::endl;
        data_by_primary = master->get_by_primary(test_primary.c_str());
        data_by_secondary = master->get_by_secondary(test_secondary.c_str());

        std::cout << "  Primary key '" << test_primary << "' found: " << (data_by_primary ? "YES" : "NO") << std::endl;
        std::cout << "  Secondary key '" << test_secondary << "' found: " << (data_by_secondary ? "YES" : "NO") << std::endl;

        if (!data_by_primary && !data_by_secondary) {
            std::cout << "  ✓ SUCCESS: Record properly deleted from both hash tables!" << std::endl;
        } else {
            std::cout << "  ✗ ERROR: Record still exists in one or both hash tables!" << std::endl;
        }
    } else {
        std::cerr << "  Failed to delete: " << test_primary << ", error: " << result << std::endl;
    }

    // Delete another record for additional testing
    std::cout << "\nDeleting second record..." << std::endl;
    std::string second_primary = "7203";
    std::string second_secondary = "7203.T";

    result = master->del(second_primary.c_str());
    if (result == MASTER_OK) {
        std::cout << "  Deleted: " << second_primary << std::endl;

        // Verify cleanup
        data_by_primary = master->get_by_primary(second_primary.c_str());
        data_by_secondary = master->get_by_secondary(second_secondary.c_str());
        std::cout << "  Primary '" << second_primary << "' cleanup: " << (!data_by_primary ? "✓" : "✗") << std::endl;
        std::cout << "  Secondary '" << second_secondary << "' cleanup: " << (!data_by_secondary ? "✓" : "✗") << std::endl;
    } else {
        std::cerr << "  Failed to delete: " << second_primary << ", error: " << result << std::endl;
    }

    // Final statistics
    std::cout << "\nFinal statistics:" << std::endl;
    stats = master->get_statistics();
    std::cout << "  Total records: " << stats.total_records << std::endl;
    std::cout << "  Used records: " << stats.used_records << std::endl;
    std::cout << "  Free records: " << stats.free_records << std::endl;
    std::cout << "  Record utilization: " << stats.record_utilization << "%" << std::endl;
}

void demo_performance_comparison() {
    std::cout << "\n=== Performance Comparison Demo ===" << std::endl;

    MasterManager manager(LOG_WARNING); // Reduce log noise for performance test

    if (!manager.loadMasterConfigs("config/MASTERs")) {
        std::cerr << "Failed to load master configurations" << std::endl;
        return;
    }

    // Get masters by type
    auto hash_masters = manager.getMasterNamesByType(MasterType::HASH_MASTER);
    auto memory_masters = manager.getMasterNamesByType(MasterType::MEMORY_MASTER);

    const int num_records = 1000;

    // Test HashMaster performance
    if (!hash_masters.empty()) {
        std::cout << "\nTesting HashMaster performance..." << std::endl;
        Master* hash_master = manager.getMaster(hash_masters[0]);
        if (hash_master) {
            hash_master->clear();

            auto start_time = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < num_records; ++i) {
                SampleRecord record("SYM" + std::to_string(i), "EXCH", 100.0 + i, 1000 + i, 2000 + i);
                std::string primary_key = "SYM" + std::to_string(i);
                std::string secondary_key = primary_key + ".EXCH";
                hash_master->put(primary_key.c_str(), secondary_key.c_str(),
                               reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            std::cout << "HashMaster: " << num_records << " records inserted in " << duration.count()
                      << " microseconds" << std::endl;
            std::cout << "Average: " << static_cast<double>(duration.count()) / num_records
                      << " microseconds per record" << std::endl;
        }
    }

    // Test MemoryMaster performance
    if (!memory_masters.empty()) {
        std::cout << "\nTesting MemoryMaster performance..." << std::endl;
        Master* memory_master = manager.getMaster(memory_masters[0]);
        if (memory_master) {
            memory_master->clear();

            auto start_time = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < num_records; ++i) {
                SampleRecord record("SYM" + std::to_string(i), "EXCH", 100.0 + i, 1000 + i, 2000 + i);
                std::string primary_key = "SYM" + std::to_string(i);
                std::string secondary_key = primary_key + ".EXCH";
                memory_master->put(primary_key.c_str(), secondary_key.c_str(),
                                 reinterpret_cast<const char*>(&record), sizeof(SampleRecord));
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            std::cout << "MemoryMaster: " << num_records << " records inserted in " << duration.count()
                      << " microseconds" << std::endl;
            std::cout << "Average: " << static_cast<double>(duration.count()) / num_records
                      << " microseconds per record" << std::endl;
        }
    }
}

int main() {
    std::cout << "MasterManager Demo Application" << std::endl;
    std::cout << "==============================" << std::endl;

    try {
        // Create mmap directory for HashMaster files
        system("mkdir -p mmap");

        // Run demos
        demo_master_manager_basics();

        MasterManager manager(LOG_INFO);
        manager.loadMasterConfigs("config/MASTERs");

        // Test all configured masters
        auto names = manager.getMasterNames();
        for (const auto& name : names) {
            demo_master_operations(manager, name);
        }

        // Performance comparison
        demo_performance_comparison();

        // Final summary
        std::cout << "\n=== Final Summary ===" << std::endl;
        manager.displayAllMasterStats();

        std::cout << "\nAll demos completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}