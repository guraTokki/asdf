#include "../HashMaster/HashMaster.h"
#include "../HashMaster/BinaryRecord.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <memory>
#include <sys/stat.h>

class HashMasterViewer {
private:
    HashMaster* hashMaster;
    bool ownHashMaster;
    HashMasterConfig config;
    std::shared_ptr<RecordLayout> recordLayout;
    SpecFileParser specParser;
    
public:
    HashMasterViewer() : hashMaster(nullptr), ownHashMaster(false) {}
    
    ~HashMasterViewer() {
        if (ownHashMaster && hashMaster) {
            delete hashMaster;
        }
    }
    
    bool loadHashMasterFromFile(const std::string& filename) {
        try {
            // Load configuration from existing HashMaster files
            config = get_config_from_hashmaster(filename.c_str());
            
            if (!config.validate()) {
                std::cerr << "Invalid configuration loaded from file: " << filename << std::endl;
                return false;
            }
            
            hashMaster = new HashMaster(config);
            ownHashMaster = true;
            
            int result = hashMaster->init();
            if (result != HASH_OK) {
                std::cerr << "Failed to initialize HashMaster from existing files: " << result << std::endl;
                return false;
            }
            
            std::cout << "✓ HashMaster loaded from existing files: " << filename << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception loading HashMaster from file: " << e.what() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "Unknown exception loading HashMaster from file" << std::endl;
            return false;
        }
    }
    
    bool loadSpecLayout(const std::string& specPath, const std::string& recordType) {
        // Check if it's a directory (YAML specs) or file (legacy TSV)
        struct stat path_stat;
        if (stat(specPath.c_str(), &path_stat) != 0) {
            std::cerr << "Spec path does not exist: " << specPath << std::endl;
            return false;
        }

        bool loadSuccess = false;
        if (S_ISDIR(path_stat.st_mode)) {
            // Load from YAML directory
            std::cout << "Loading YAML specs from directory: " << specPath << std::endl;
            loadSuccess = specParser.loadFromYamlDirectory(specPath);
        } else {
            // Load from TSV file (legacy)
            std::cout << "Loading legacy TSV spec file: " << specPath << std::endl;
            loadSuccess = specParser.loadFromFile(specPath);
        }

        if (!loadSuccess) {
            std::cerr << "Failed to load spec from: " << specPath << std::endl;
            return false;
        }
        
        recordLayout = specParser.getLayout(recordType);
        if (!recordLayout) {
            std::cerr << "Record type not found in spec: " << recordType << std::endl;
            
            // List available record types
            auto types = specParser.getRecordTypes();
            if (!types.empty()) {
                std::cerr << "Available record types:" << std::endl;
                for (const auto& type : types) {
                    std::cerr << "  - " << type << std::endl;
                }
            }
            return false;
        }
        
        std::cout << "✓ Loaded spec layout for: " << recordType << std::endl;
        std::cout << "  Record size: " << recordLayout->getRecordSize() << " bytes" << std::endl;
        std::cout << "  Field count: " << recordLayout->getFields().size() << std::endl;
        return true;
    }
    
    void printSummary() {
        if (!hashMaster) {
            std::cerr << "HashMaster not loaded" << std::endl;
            return;
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "        HashMaster Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        
        auto stats = hashMaster->get_statistics();
        
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Base filename: " << config._filename << std::endl;
        std::cout << "  Max records: " << config._max_record_count << std::endl;
        std::cout << "  Record size: " << config._max_record_size << " bytes" << std::endl;
        std::cout << "  Hash buckets: " << config._hash_count << std::endl;
        
        std::cout << "\nRecord Statistics:" << std::endl;
        std::cout << "  Total records: " << stats.total_records << std::endl;
        std::cout << "  Used records: " << stats.used_records << std::endl;
        std::cout << "  Free records: " << stats.free_records << std::endl;
        std::cout << "  Utilization: " << std::fixed << std::setprecision(1) 
                  << stats.record_utilization << "%" << std::endl;
        
        if (recordLayout) {
            std::cout << "\nRecord Layout:" << std::endl;
            std::cout << "  Layout type: " << recordLayout->getRecordType() << std::endl;
            std::cout << "  Expected size: " << recordLayout->getRecordSize() << " bytes" << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    void listAllRecords(int limit = 100) {
        if (!hashMaster) {
            std::cerr << "HashMaster not loaded" << std::endl;
            return;
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "         All Records List" << std::endl;
        std::cout << "========================================" << std::endl;
        
        int recordCount = 0;
        int maxRecords = config._max_record_count;
        
        if (recordLayout) {
            printHeaderRow();
            std::cout << std::string(120, '-') << std::endl;
        }
        
        // Use HashMaster's iterator approach by checking each record entry directly
        for (int i = 0; i < maxRecords && recordCount < limit; i++) {
            try {
                // Get record entry directly to check if it's occupied
                // Note: get_record_by_seq expects 1-based index, we use 0-based
                char* recordData = hashMaster->get_record_by_seq(i + 1);
                
                if (recordData) {
                    // Check if the record is actually occupied by checking the first few bytes
                    bool hasData = false;
                    for (int j = 0; j < std::min(20, config._max_record_size); j++) {
                        if (recordData[j] != 0) {
                            hasData = true;
                            break;
                        }
                    }
                    
                    if (hasData) {
                        recordCount++;
                        
                        if (recordLayout) {
                            printRecordRow(recordCount, recordData, config._max_record_size);
                        } else {
                            printRawRecord(recordCount, i, recordData, config._max_record_size);
                        }
                        
                        if (recordCount >= limit) {
                            std::cout << "\n... (showing first " << limit << " records)" << std::endl;
                            break;
                        }
                    }
                }
            } catch (...) {
                // Skip invalid records
                continue;
            }
        }
        
        if (recordCount == 0) {
            std::cout << "No records found in HashMaster" << std::endl;
        } else {
            std::cout << "\nTotal records displayed: " << recordCount << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    void searchRecord(const std::string& key, bool isPrimary = true) {
        if (!hashMaster) {
            std::cerr << "HashMaster not loaded" << std::endl;
            return;
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "         Record Search" << std::endl;
        std::cout << "========================================" << std::endl;
        
        std::cout << "Searching for " << (isPrimary ? "primary" : "secondary") 
                  << " key: '" << key << "'" << std::endl;
        
        char* recordData = isPrimary ? 
            hashMaster->get_by_primary(key.c_str()) : 
            hashMaster->get_by_secondary(key.c_str());
        
        if (recordData) {
            std::cout << "✓ Record found!" << std::endl;
            
            if (recordLayout) {
                printDetailedRecord(key, recordData, config._max_record_size);
            } else {
                printRawRecordData(key, recordData, config._max_record_size);
            }
        } else {
            std::cout << "❌ Record not found for key: " << key << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    void showFieldLayout() {
        if (!recordLayout) {
            std::cerr << "No spec layout loaded. Use --spec option to load layout." << std::endl;
            return;
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "         Field Layout" << std::endl;
        std::cout << "========================================" << std::endl;
        
        std::cout << "Record Type: " << recordLayout->getRecordType() << std::endl;
        std::cout << "Total Size: " << recordLayout->getRecordSize() << " bytes" << std::endl;
        std::cout << std::endl;
        
        std::cout << std::left << std::setw(4) << "No" 
                  << std::setw(20) << "Field Name"
                  << std::setw(15) << "Type"
                  << std::setw(8) << "Offset"
                  << std::setw(8) << "Length"
                  << std::setw(6) << "Key"
                  << std::setw(30) << "Description" << std::endl;
        std::cout << std::string(90, '-') << std::endl;
        
        const auto& fields = recordLayout->getFields();
        for (size_t i = 0; i < fields.size(); i++) {
            const auto& field = fields[i];
            
            std::cout << std::left << std::setw(4) << (i + 1)
                      << std::setw(20) << field.name
                      << std::setw(15) << fieldTypeToString(field.type)
                      << std::setw(8) << field.offset
                      << std::setw(8) << field.length
                      << std::setw(6) << (field.isKey ? "Y" : "N")
                      << std::setw(30) << "..." << std::endl;  // Description truncated for display
        }
        
        std::cout << std::endl;
    }
    
private:
    void printHeaderRow() {
        if (!recordLayout) return;
        
        const auto& fields = recordLayout->getFields();
        std::cout << std::left << std::setw(6) << "No";
        
        // Print first few important fields as columns
        int fieldCount = 0;
        for (const auto& field : fields) {
            if (fieldCount >= 8) break;  // Limit columns for readability
            
            std::cout << std::setw(12) << field.name.substr(0, 11);
            fieldCount++;
        }
        std::cout << std::endl;
    }
    
    void printRecordRow(int recordNo, char* recordData, int recordSize) {
        if (!recordLayout || !recordData) return;
        
        BinaryRecord record(recordLayout, recordData);
        const auto& fields = recordLayout->getFields();
        
        std::cout << std::left << std::setw(6) << recordNo;
        
        // Print first few important fields as columns
        int fieldCount = 0;
        for (const auto& field : fields) {
            if (fieldCount >= 8) break;  // Limit columns for readability
            
            std::string value = record.getValue(field.name);
            if (value.length() > 11) {
                value = value.substr(0, 11);
            }
            std::cout << std::setw(12) << value;
            fieldCount++;
        }
        std::cout << std::endl;
    }
    
    void printDetailedRecord(const std::string& key, char* recordData, int recordSize) {
        if (!recordLayout || !recordData) return;
        
        BinaryRecord record(recordLayout, recordData);
        const auto& fields = recordLayout->getFields();
        
        std::cout << "\nRecord Details for key: " << key << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        
        for (const auto& field : fields) {
            std::string value = record.getValue(field.name);
            
            std::cout << std::left << std::setw(20) << field.name << ": ";
            
            // Format based on field type
            if (field.type == FieldType::CHAR && value.empty()) {
                std::cout << "(empty)";
            } else if (field.length > 40) {
                // Truncate very long fields
                if (value.length() > 40) {
                    std::cout << value.substr(0, 37) << "...";
                } else {
                    std::cout << value;
                }
            } else {
                std::cout << value;
            }
            
            if (field.isKey) {
                std::cout << " [KEY]";
            }
            
            std::cout << std::endl;
        }
    }
    
    void printRawRecord(int recordNo, int index, char* recordData, int recordSize) {
        std::cout << "Record " << recordNo << " (index " << index << "): ";
        
        // Print first 60 characters of raw data
        int printLen = std::min(60, recordSize);
        for (int i = 0; i < printLen; i++) {
            char c = recordData[i];
            if (c == '\0') break;
            if (std::isprint(c)) {
                std::cout << c;
            } else {
                std::cout << '.';
            }
        }
        if (recordSize > 60) {
            std::cout << "...";
        }
        std::cout << std::endl;
    }
    
    void printRawRecordData(const std::string& key, char* recordData, int recordSize) {
        std::cout << "\nRaw record data for key: " << key << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        
        // Print in hex + ASCII format
        int bytesPerRow = 16;
        for (int offset = 0; offset < recordSize; offset += bytesPerRow) {
            // Print offset
            std::cout << std::setw(4) << std::setfill('0') << std::hex << offset << ": ";
            
            // Print hex bytes
            for (int i = 0; i < bytesPerRow && (offset + i) < recordSize; i++) {
                std::cout << std::setw(2) << std::setfill('0') << std::hex 
                          << (unsigned char)recordData[offset + i] << " ";
            }
            
            // Pad if needed
            for (int i = offset + bytesPerRow; i < offset + bytesPerRow && i >= recordSize; i++) {
                std::cout << "   ";
            }
            
            // Print ASCII
            std::cout << " | ";
            for (int i = 0; i < bytesPerRow && (offset + i) < recordSize; i++) {
                char c = recordData[offset + i];
                std::cout << (std::isprint(c) ? c : '.');
            }
            std::cout << std::endl;
        }
        
        std::cout << std::dec << std::setfill(' ');  // Reset formatting
    }
};

void printUsage(const char* program) {
    std::cout << "HashMaster Viewer - View and search HashMaster records" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program << " <hashmaster_file> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Parameters:" << std::endl;
    std::cout << "  hashmaster_file      Base filename for HashMaster files" << std::endl;
    std::cout << "                       (loads config from <filename>_records.dat header)" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --spec <path> <type> Load spec layout from YAML directory or TSV file for record type" << std::endl;
    std::cout << "  --summary            Show HashMaster summary (default if no other options)" << std::endl;
    std::cout << "  --list [N]           List all records (limit to N records, default: 100)" << std::endl;
    std::cout << "  --search-primary <key>  Search by primary key" << std::endl;
    std::cout << "  --search-secondary <key> Search by secondary key" << std::endl;
    std::cout << "  --fields             Show field layout (requires --spec)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  # Show summary" << std::endl;
    std::cout << "  " << program << " equity_master" << std::endl;
    std::cout << std::endl;
    std::cout << "  # List records with YAML spec layout" << std::endl;
    std::cout << "  " << program << " equity_master --spec config/SPECs MMP_EQUITY_MASTER --list 50" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Search for specific record using YAML specs" << std::endl;
    std::cout << "  " << program << " equity_master --spec config/SPECs MMP_EQUITY_MASTER --search-primary \"AAPL.O\"" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Show field layout from YAML specs" << std::endl;
    std::cout << "  " << program << " equity_master --spec config/SPECs MMP_EQUITY_MASTER --fields" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Legacy: Use TSV spec file (backward compatibility)" << std::endl;
    std::cout << "  " << program << " equity_master --spec config/spec_sample2.txt MMP_EQUITY_MASTER --list 50" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Check for help first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::string filename = argv[1];
    std::string specPath;
    std::string recordType;
    bool showSummary = false;
    bool listRecords = false;
    bool showFields = false;
    int listLimit = 100;
    std::string searchPrimaryKey;
    std::string searchSecondaryKey;
    
    // Parse arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--spec") {
            if (i + 2 >= argc) {
                std::cerr << "Error: --spec requires path and record type arguments" << std::endl;
                return 1;
            }
            specPath = argv[++i];
            recordType = argv[++i];
        } else if (arg == "--summary") {
            showSummary = true;
        } else if (arg == "--list") {
            listRecords = true;
            // Check if next argument is a number (limit)
            if (i + 1 < argc && std::isdigit(argv[i + 1][0])) {
                listLimit = std::atoi(argv[++i]);
            }
        } else if (arg == "--search-primary") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --search-primary requires a key argument" << std::endl;
                return 1;
            }
            searchPrimaryKey = argv[++i];
        } else if (arg == "--search-secondary") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --search-secondary requires a key argument" << std::endl;
                return 1;
            }
            searchSecondaryKey = argv[++i];
        } else if (arg == "--fields") {
            showFields = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Default to summary if no specific action requested
    if (!listRecords && searchPrimaryKey.empty() && searchSecondaryKey.empty() && !showFields) {
        showSummary = true;
    }
    
    HashMasterViewer viewer;
    
    // Load HashMaster
    if (!viewer.loadHashMasterFromFile(filename)) {
        std::cerr << "Failed to load HashMaster: " << filename << std::endl;
        return 1;
    }
    
    // Load spec layout if specified
    if (!specPath.empty() && !recordType.empty()) {
        if (!viewer.loadSpecLayout(specPath, recordType)) {
            std::cerr << "Failed to load spec layout" << std::endl;
            return 1;
        }
    }
    
    // Execute requested actions
    if (showSummary) {
        viewer.printSummary();
    }
    
    if (showFields) {
        viewer.showFieldLayout();
    }
    
    if (listRecords) {
        viewer.listAllRecords(listLimit);
    }
    
    if (!searchPrimaryKey.empty()) {
        viewer.searchRecord(searchPrimaryKey, true);
    }
    
    if (!searchSecondaryKey.empty()) {
        viewer.searchRecord(searchSecondaryKey, false);
    }
    
    return 0;
}