#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <chrono>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <unistd.h>

#include "../common/db_sam.h"
#include "../HashMaster/BinaryRecord.h"

using namespace std;

// Forward declarations
void printUsage(const char* program_name);
void printDatabaseInfo(const DB_SAM& db);
void listMessages(const DB_SAM& db, uint32_t start_seq, uint32_t end_seq, bool show_data);
void searchMessages(const DB_SAM& db, const string& search_term);
void exportMessages(const DB_SAM& db, const string& output_file, uint32_t start_seq, uint32_t end_seq);
void dumpMessage(const DB_SAM& db, uint32_t seq, bool use_binary_record, const string& spec_path, const string& record_type);
void verifyDatabase(const DB_SAM& db);
string formatTimestamp(uint64_t timestamp_ns);
string formatSize(uint64_t size);
bool isValidSpecPath(const string& path);

// Global variables for BinaryRecord support
unique_ptr<SpecFileParser> g_spec_parser = nullptr;
bool g_spec_loaded = false;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    string db_path = argv[1];
    string command = (argc > 2) ? argv[2] : "info";

    // Check if database exists
    string idx_file = db_path + ".idx";
    string data_file = db_path + ".data";
    if (access(idx_file.c_str(), F_OK) != 0 || access(data_file.c_str(), F_OK) != 0) {
        cerr << "Error: Database files not found at " << db_path << endl;
        cerr << "Expected files: " << db_path << ".idx, " << db_path << ".data" << endl;
        return 1;
    }

    try {
        // Open database
        DB_SAM db(db_path);
        if (!db.open()) {
            cerr << "Error: Failed to open database at " << db_path << endl;
            return 1;
        }

        cout << "=== DB_SAM Viewer ===" << endl;
        cout << "Database: " << db_path << endl;
        cout << "Command: " << command << endl << endl;

        // Parse command
        if (command == "info" || command == "i") {
            printDatabaseInfo(db);

        } else if (command == "list" || command == "l") {
            uint32_t start_seq = 1;
            uint32_t end_seq = min(10u, db.count());
            bool show_data = false;

            // Parse additional arguments
            for (int i = 3; i < argc; i++) {
                string arg = argv[i];
                if (arg == "--data" || arg == "-d") {
                    show_data = true;
                } else if (arg.find("--start=") == 0) {
                    start_seq = stoul(arg.substr(8));
                } else if (arg.find("--end=") == 0) {
                    end_seq = stoul(arg.substr(6));
                } else if (arg.find("--count=") == 0) {
                    uint32_t count = stoul(arg.substr(8));
                    end_seq = start_seq + count - 1;
                }
            }

            listMessages(db, start_seq, end_seq, show_data);

        } else if (command == "dump" || command == "d") {
            if (argc < 4) {
                cerr << "Error: dump command requires sequence number" << endl;
                cerr << "Usage: " << argv[0] << " <db_path> dump <seq> [--spec=<path>] [--type=<record_type>]" << endl;
                return 1;
            }

            uint32_t seq = stoul(argv[3]);
            bool use_binary_record = false;
            string spec_path = "";
            string record_type = "";

            // Parse additional arguments
            for (int i = 4; i < argc; i++) {
                string arg = argv[i];
                if (arg.find("--spec=") == 0) {
                    spec_path = arg.substr(7);
                    use_binary_record = true;
                } else if (arg.find("--type=") == 0) {
                    record_type = arg.substr(7);
                }
            }

            dumpMessage(db, seq, use_binary_record, spec_path, record_type);

        } else if (command == "search" || command == "s") {
            if (argc < 4) {
                cerr << "Error: search command requires search term" << endl;
                cerr << "Usage: " << argv[0] << " <db_path> search <term>" << endl;
                return 1;
            }

            string search_term = argv[3];
            searchMessages(db, search_term);

        } else if (command == "export" || command == "e") {
            if (argc < 4) {
                cerr << "Error: export command requires output file" << endl;
                cerr << "Usage: " << argv[0] << " <db_path> export <output_file> [--start=<seq>] [--end=<seq>]" << endl;
                return 1;
            }

            string output_file = argv[3];
            uint32_t start_seq = 1;
            uint32_t end_seq = db.count();

            // Parse additional arguments
            for (int i = 4; i < argc; i++) {
                string arg = argv[i];
                if (arg.find("--start=") == 0) {
                    start_seq = stoul(arg.substr(8));
                } else if (arg.find("--end=") == 0) {
                    end_seq = stoul(arg.substr(6));
                }
            }

            exportMessages(db, output_file, start_seq, end_seq);

        } else if (command == "verify" || command == "v") {
            verifyDatabase(db);

        } else {
            cerr << "Error: Unknown command '" << command << "'" << endl;
            printUsage(argv[0]);
            return 1;
        }

        db.close();

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}

void printUsage(const char* program_name) {
    cout << "Usage: " << program_name << " <db_path> [command] [options]" << endl;
    cout << endl;
    cout << "Commands:" << endl;
    cout << "  info, i                          - Show database information (default)" << endl;
    cout << "  list, l [options]                - List messages with Seq, Size, Offset, Timestamp" << endl;
    cout << "    --start=<seq>                  - Start sequence number (default: 1)" << endl;
    cout << "    --end=<seq>                    - End sequence number (default: 10)" << endl;
    cout << "    --count=<n>                    - Number of messages to show" << endl;
    cout << "    --data, -d                     - Show message data (first 32 bytes in hex + ASCII)" << endl;
    cout << "  dump, d <seq> [options]          - Dump specific message" << endl;
    cout << "    --spec=<path>                  - Use BinaryRecord with spec file/directory" << endl;
    cout << "    --type=<record_type>           - Record type for BinaryRecord parsing" << endl;
    cout << "  search, s <term>                 - Search messages containing term" << endl;
    cout << "  export, e <file> [options]       - Export messages to file" << endl;
    cout << "    --start=<seq>                  - Start sequence number" << endl;
    cout << "    --end=<seq>                    - End sequence number" << endl;
    cout << "  verify, v                        - Verify database integrity" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  " << program_name << " /tmp/test.db info" << endl;
    cout << "  " << program_name << " /tmp/test.db list --start=1 --count=20 --data" << endl;
    cout << "  " << program_name << " /tmp/test.db dump 1 --spec=config/SPECs --type=TRADE_DATA" << endl;
    cout << "  " << program_name << " /tmp/test.db search \"error\"" << endl;
    cout << "  " << program_name << " /tmp/test.db export output.txt --start=1 --end=100" << endl;
}

void printDatabaseInfo(const DB_SAM& db) {
    cout << "=== Database Information ===" << endl;
    cout << "Base Path: " << db.get_base_path() << endl;
    cout << "Index File: " << db.get_index_file_path() << endl;
    cout << "Data File: " << db.get_data_file_path() << endl;
    cout << "Message Count: " << db.count() << endl;
    cout << "Next Sequence: " << db.get_next_sequence() << endl;
    cout << "Max Sequence: " << db.max_seq() << endl;

    int64_t data_size = db.get_data_file_size();
    int64_t index_size = db.get_index_file_size();

    if (data_size >= 0) {
        cout << "Data File Size: " << formatSize(data_size) << " (" << data_size << " bytes)" << endl;
    }
    if (index_size >= 0) {
        cout << "Index File Size: " << formatSize(index_size) << " (" << index_size << " bytes)" << endl;
    }

    cout << "Database Open: " << (db.isOpen() ? "Yes" : "No") << endl;
}

void listMessages(const DB_SAM& db, uint32_t start_seq, uint32_t end_seq, bool show_data) {
    cout << "=== Message List (seq " << start_seq << " to " << end_seq << ") ===" << endl;

    uint32_t max_seq = db.max_seq();
    if (end_seq > max_seq) {
        end_seq = max_seq;
    }

    cout << "Seq\tSize\tOffset\t\tTimestamp\t\tData (first 32 bytes)" << endl;
    cout << "---\t----\t------\t\t---------\t\t--------------------" << endl;

    for (uint32_t seq = start_seq; seq <= end_seq; seq++) {
        SAM_INDEX index;
        string data;

        if (db.get(seq, data)) {
            // Get index info manually
            uint32_t buffer_size = data.size() + 100;
            vector<char> buffer(buffer_size);

            if (db.get(seq, index, buffer.data(), &buffer_size)) {
                cout << seq << "\t"
                     << index._size << "\t"
                     << index._seek << "\t\t"
                     << formatTimestamp(index._timestamp) << "\t";

                if (show_data) {
                    // Show binary data in hex + ASCII format (first 32 bytes)
                    const char* raw_data = data.c_str();
                    size_t display_size = min(data.size(), size_t(32));

                    // Hex part
                    for (size_t i = 0; i < display_size; i++) {
                        cout << setfill('0') << setw(2) << hex << static_cast<unsigned int>(static_cast<unsigned char>(raw_data[i]));
                        if (i < display_size - 1) cout << " ";
                    }

                    // Padding for alignment
                    if (display_size < 32) {
                        for (size_t i = display_size; i < 32; i++) {
                            cout << "   ";
                        }
                    }

                    cout << "  ";

                    // ASCII part
                    for (size_t i = 0; i < display_size; i++) {
                        char c = raw_data[i];
                        cout << (isprint(c) ? c : '.');
                    }

                    if (data.size() > 32) {
                        cout << "...";
                    }

                    cout << dec; // Reset to decimal
                }
                cout << endl;
            }
        } else {
            cout << seq << "\t[NOT FOUND]" << endl;
        }
    }
}

void searchMessages(const DB_SAM& db, const string& search_term) {
    cout << "=== Searching for: \"" << search_term << "\" ===" << endl;

    uint32_t found_count = 0;
    uint32_t max_seq = db.max_seq();

    for (uint32_t seq = 1; seq <= max_seq; seq++) {
        string data;
        if (db.get(seq, data)) {
            if (data.find(search_term) != string::npos) {
                SAM_INDEX index;
                uint32_t buffer_size = data.size() + 100;
                vector<char> buffer(buffer_size);

                if (db.get(seq, index, buffer.data(), &buffer_size)) {
                    cout << "Seq " << seq << " [" << formatTimestamp(index._timestamp) << "]: ";
                    if (data.size() > 80) {
                        cout << data.substr(0, 80) << "..." << endl;
                    } else {
                        cout << data << endl;
                    }
                    found_count++;
                }
            }
        }
    }

    cout << "Found " << found_count << " messages containing \"" << search_term << "\"" << endl;
}

void exportMessages(const DB_SAM& db, const string& output_file, uint32_t start_seq, uint32_t end_seq) {
    cout << "=== Exporting messages to: " << output_file << " ===" << endl;

    ofstream out(output_file);
    if (!out.is_open()) {
        cerr << "Error: Cannot open output file " << output_file << endl;
        return;
    }

    uint32_t max_seq = db.max_seq();
    if (end_seq > max_seq) {
        end_seq = max_seq;
    }

    uint32_t exported_count = 0;

    // Write header
    out << "# DB_SAM Export" << endl;
    out << "# Database: " << db.get_base_path() << endl;
    out << "# Export range: " << start_seq << " to " << end_seq << endl;
    out << "# Export time: " << formatTimestamp(chrono::duration_cast<chrono::nanoseconds>(
           chrono::system_clock::now().time_since_epoch()).count()) << endl;
    out << endl;

    for (uint32_t seq = start_seq; seq <= end_seq; seq++) {
        SAM_INDEX index;
        string data;

        if (db.get(seq, data)) {
            uint32_t buffer_size = data.size() + 100;
            vector<char> buffer(buffer_size);

            if (db.get(seq, index, buffer.data(), &buffer_size)) {
                out << "SEQ=" << seq << endl;
                out << "SIZE=" << index._size << endl;
                out << "TIMESTAMP=" << formatTimestamp(index._timestamp) << endl;
                out << "DATA=" << data << endl;
                out << "---" << endl;
                exported_count++;
            }
        }
    }

    out.close();
    cout << "Exported " << exported_count << " messages to " << output_file << endl;
}

void dumpMessage(const DB_SAM& db, uint32_t seq, bool use_binary_record, const string& spec_path, const string& record_type) {
    cout << "=== Dumping Message Sequence " << seq << " ===" << endl;

    SAM_INDEX index;
    string data;

    if (!db.get(seq, data)) {
        cerr << "Error: Message sequence " << seq << " not found" << endl;
        return;
    }

    // Get detailed index info
    uint32_t buffer_size = data.size() + 100;
    vector<char> buffer(buffer_size);

    if (!db.get(seq, index, buffer.data(), &buffer_size)) {
        cerr << "Error: Failed to get index info for sequence " << seq << endl;
        return;
    }

    // Print basic info
    cout << "Sequence: " << index._seq << endl;
    cout << "Size: " << index._size << " bytes" << endl;
    cout << "Timestamp: " << formatTimestamp(index._timestamp) << endl;
    cout << "Seek Position: " << index._seek << endl;
    cout << endl;

    // Print raw data
    cout << "=== Raw Data ===" << endl;
    cout << "String: " << data << endl;
    cout << endl;

    cout << "=== Hex Dump ===" << endl;
    const char* raw_data = data.c_str();
    for (size_t i = 0; i < data.size(); i += 16) {
        cout << setfill('0') << setw(8) << hex << i << ": ";

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < data.size()) {
                cout << setfill('0') << setw(2) << hex << (unsigned char)raw_data[i + j] << " ";
            } else {
                cout << "   ";
            }
        }

        cout << " ";

        // ASCII representation
        for (size_t j = 0; j < 16 && i + j < data.size(); j++) {
            char c = raw_data[i + j];
            cout << (isprint(c) ? c : '.');
        }

        cout << endl;
    }
    cout << dec << endl;

    // BinaryRecord parsing if requested
    if (use_binary_record && !spec_path.empty()) {
        cout << "=== BinaryRecord Parsing ===" << endl;

        // Initialize spec parser if not already done
        if (!g_spec_loaded) {
            g_spec_parser = make_unique<SpecFileParser>();

            if (isValidSpecPath(spec_path)) {
                struct stat path_stat;
                if (stat(spec_path.c_str(), &path_stat) == 0) {
                    bool load_success = false;
                    if (S_ISDIR(path_stat.st_mode)) {
                        load_success = g_spec_parser->loadFromYamlDirectory(spec_path);
                    } else {
                        load_success = g_spec_parser->loadFromFile(spec_path);
                    }

                    if (load_success) {
                        g_spec_loaded = true;
                        cout << "Loaded spec from: " << spec_path << endl;
                    } else {
                        cerr << "Warning: Failed to load spec from " << spec_path << endl;
                    }
                }
            } else {
                cerr << "Warning: Invalid spec path " << spec_path << endl;
            }
        }

        if (g_spec_loaded) {
            // Determine record type
            string actual_record_type = record_type;
            if (actual_record_type.empty()) {
                auto types = g_spec_parser->getRecordTypes();
                if (!types.empty()) {
                    actual_record_type = types[0];
                    cout << "Using first available record type: " << actual_record_type << endl;
                } else {
                    cerr << "No record types found in spec" << endl;
                    return;
                }
            }

            // Get layout and parse
            auto layout = g_spec_parser->getLayout(actual_record_type);
            if (layout) {
                cout << "Using record type: " << actual_record_type << endl;
                cout << "Record layout size: " << layout->getRecordSize() << " bytes" << endl;
                cout << endl;

                // Create BinaryRecord and parse
                BinaryRecord record(layout);
                if (record.copyFrom(raw_data, min((int)data.size(), layout->getRecordSize()))) {
                    cout << "=== Parsed Fields ===" << endl;

                    for (const auto& field : layout->getFields()) {
                        string value = record.getValue(field.name);
                        cout << setw(20) << left << field.name << ": " << value;
                        if (field.isKey) {
                            cout << " [KEY]";
                        }
                        cout << endl;
                    }

                    cout << endl;
                    cout << "Primary Key: " << record.getPrimaryKey() << endl;
                } else {
                    cerr << "Error: Failed to parse message with BinaryRecord" << endl;
                }
            } else {
                cerr << "Error: Record type '" << actual_record_type << "' not found in spec" << endl;
                cout << "Available record types: ";
                auto types = g_spec_parser->getRecordTypes();
                for (const auto& type : types) {
                    cout << type << " ";
                }
                cout << endl;
            }
        }
    }
}

void verifyDatabase(const DB_SAM& db) {
    cout << "=== Database Verification ===" << endl;

    bool integrity_ok = db.verify_integrity();
    cout << "Integrity Check: " << (integrity_ok ? "PASSED" : "FAILED") << endl;

    // Additional verification
    uint32_t count = db.count();
    uint32_t max_seq = db.max_seq();
    uint32_t next_seq = db.get_next_sequence();

    cout << "Count: " << count << endl;
    cout << "Max Sequence: " << max_seq << endl;
    cout << "Next Sequence: " << next_seq << endl;

    if (next_seq != max_seq + 1) {
        cout << "Warning: Next sequence (" << next_seq << ") != Max sequence + 1 (" << max_seq + 1 << ")" << endl;
    }

    // Try to read all messages
    uint32_t readable_count = 0;
    for (uint32_t seq = 1; seq <= max_seq; seq++) {
        string data;
        if (db.get(seq, data)) {
            readable_count++;
        }
    }

    cout << "Readable Messages: " << readable_count << "/" << count << endl;

    if (readable_count == count) {
        cout << "Verification Result: ALL CHECKS PASSED" << endl;
    } else {
        cout << "Verification Result: SOME ISSUES FOUND" << endl;
    }
}

string formatTimestamp(uint64_t timestamp_ns) {
    auto seconds = timestamp_ns / 1000000000ULL;
    auto nanoseconds = timestamp_ns % 1000000000ULL;

    time_t time_sec = static_cast<time_t>(seconds);
    struct tm* tm_info = localtime(&time_sec);

    ostringstream oss;
    oss << put_time(tm_info, "%Y-%m-%d %H:%M:%S");
    oss << "." << setfill('0') << setw(9) << nanoseconds;

    return oss.str();
}

string formatSize(uint64_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size_double = static_cast<double>(size);

    while (size_double >= 1024.0 && unit_index < 4) {
        size_double /= 1024.0;
        unit_index++;
    }

    ostringstream oss;
    oss << fixed << setprecision(1) << size_double << " " << units[unit_index];
    return oss.str();
}

bool isValidSpecPath(const string& path) {
    struct stat path_stat;
    return (stat(path.c_str(), &path_stat) == 0);
}