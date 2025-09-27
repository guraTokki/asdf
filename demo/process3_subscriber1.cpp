#include <iostream>
#include <string>
#include <vector>
#include <getopt.h>
#include <signal.h>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <algorithm>
#include <ctime>

#include <event2/event.h>
#include "../pubsub/Common.h"
#include "../pubsub/SimpleSubscriber.h"
#include "../pubsub/FileSequenceStorage.h"
#include "../eventBase/EventBase.h"

using namespace SimplePubSub;

// Global variables
static bool g_running = true;
static std::unique_ptr<SimpleSubscriber> g_subscriber;
static std::ofstream g_log_file;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down gracefully..." << std::endl;
    g_running = false;
}

// Parse topics string and return topic mask
uint32_t parse_topics(const std::string& topics_str) {
    if (topics_str == "all") {
        return static_cast<uint32_t>(DataTopic::ALL_TOPICS);
    }
    
    uint32_t mask = 0;
    std::string topic = "";
    std::string topics_lower = topics_str;
    
    // Convert to lowercase for case-insensitive comparison
    std::transform(topics_lower.begin(), topics_lower.end(), topics_lower.begin(), ::tolower);
    
    // Split by comma and parse each topic
    std::stringstream ss(topics_lower);
    while (std::getline(ss, topic, ',')) {
        // Trim whitespace
        topic.erase(0, topic.find_first_not_of(" \t"));
        topic.erase(topic.find_last_not_of(" \t") + 1);
        
        if (topic == "trade" || topic == "topic1") {
            mask |= static_cast<uint32_t>(DataTopic::TOPIC1);
        } else if (topic == "quote" || topic == "topic2") {
            mask |= static_cast<uint32_t>(DataTopic::TOPIC2);
        } else if (topic == "misc") {
            mask |= static_cast<uint32_t>(DataTopic::MISC);
        } else if (topic == "all") {
            mask |= static_cast<uint32_t>(DataTopic::ALL_TOPICS);
        } else {
            std::cerr << "Unknown topic: " << topic << std::endl;
            return 0;
        }
    }
    
    return mask;
}

// Topic data callback function
void topic_data_callback(DataTopic topic, const char* data, int size) {
    std::string topic_str = topic_to_string(topic);
    std::string message = "Received " + topic_str + " data (" + std::to_string(size) + " bytes)";
    
    // Log to console
    std::cout << "[" << std::time(nullptr) << "] " << message << std::endl;
    
    // Log to file if enabled
    if (g_log_file.is_open()) {
        g_log_file << "[" << std::time(nullptr) << "] " << message << std::endl;
        g_log_file.flush();
    }
    
    // Print first 64 bytes of data if available
    if (size > 0) {
        int print_size = std::min(size, 64);
        std::cout << "Data: ";
        for (int i = 0; i < print_size; ++i) {
            printf("%02x ", (unsigned char)data[i]);
        }
        if (size > 64) {
            std::cout << "... (truncated)";
        }
        std::cout << std::endl;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -u <socket_path>  : Connect via Unix socket (default: /tmp/t2ma_japan.sock)" << std::endl;
    std::cout << "  -t <ip:port>      : Connect via TCP (e.g., 127.0.0.1:9998)" << std::endl;
    std::cout << "  -l <log_file>     : Log messages to file" << std::endl;
    std::cout << "  --storage <type>  : Storage type: file, hashmaster (default: file)" << std::endl;
    std::cout << "  --clear-storage   : Clear sequence storage on startup" << std::endl;
    std::cout << "  --name <name>     : Subscriber name (default: process3_subscriber1)" << std::endl;
    std::cout << "  --id <id>         : Subscriber ID (default: 1001)" << std::endl;
    std::cout << "  --topics <topics> : Subscribe topics: trade,quote,misc,all or combinations (default: all)" << std::endl;
    std::cout << "                      Examples: trade / quote,misc / trade,quote,misc / all" << std::endl;
    std::cout << "  -h                : Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default values
    std::string socket_path = "/tmp/t2ma_japan.sock";
    std::string tcp_address = "";
    int tcp_port = 0;
    std::string log_file = "";
    std::string storage_type = "file";
    bool clear_storage = false;
    std::string subscriber_name = "process3_subscriber1";
    uint32_t subscriber_id = 1001;
    std::string topics_str = "all";
    
    // Parse command line options
    static struct option long_options[] = {
        {"storage", required_argument, 0, 's'},
        {"clear-storage", no_argument, 0, 'c'},
        {"name", required_argument, 0, 'n'},
        {"id", required_argument, 0, 'i'},
        {"topics", required_argument, 0, 'T'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int long_index = 0;
    
    while ((opt = getopt_long(argc, argv, "u:t:l:h", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'u':
                socket_path = optarg;
                break;
            case 't': {
                std::string tcp_str = optarg;
                size_t colon_pos = tcp_str.find(':');
                if (colon_pos != std::string::npos) {
                    tcp_address = tcp_str.substr(0, colon_pos);
                    tcp_port = std::stoi(tcp_str.substr(colon_pos + 1));
                } else {
                    std::cerr << "Invalid TCP address format. Use ip:port" << std::endl;
                    return 1;
                }
                break;
            }
            case 'l':
                log_file = optarg;
                break;
            case 's':
                storage_type = optarg;
                break;
            case 'c':
                clear_storage = true;
                break;
            case 'n':
                subscriber_name = optarg;
                break;
            case 'i':
                subscriber_id = std::stoul(optarg);
                break;
            case 'T':
                topics_str = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Parse topics
    uint32_t topic_mask = parse_topics(topics_str);
    if (topic_mask == 0) {
        std::cerr << "Invalid topics specified: " << topics_str << std::endl;
        return 1;
    }
    
    // Setup logging
    if (!log_file.empty()) {
        g_log_file.open(log_file, std::ios::app);
        if (!g_log_file.is_open()) {
            std::cerr << "Failed to open log file: " << log_file << std::endl;
            return 1;
        }
        std::cout << "Logging to file: " << log_file << std::endl;
    }
    
    // Create event base
    struct event_base* base = event_base_new();
    if (!base) {
        std::cerr << "Failed to create event base" << std::endl;
        return 1;
    }
    
    // Create sequence storage
    std::unique_ptr<SequenceStorage> storage;
    if (storage_type == "file") {
        auto file_storage = std::make_unique<FileSequenceStorage>("./sequence_data", subscriber_name);
        if (!file_storage->initialize()) {
            std::cerr << "Failed to initialize file sequence storage" << std::endl;
            return 1;
        }
        if (clear_storage) {
            std::cout << "Clearing sequence storage..." << std::endl;
            file_storage->clear();
        }
        storage = std::move(file_storage);
    } else if (storage_type == "hashmaster") {
        std::cerr << "HashMaster storage not implemented yet" << std::endl;
        return 1;
    } else {
        std::cerr << "Unknown storage type: " << storage_type << std::endl;
        return 1;
    }
    
    // Create subscriber (SimpleSubscriber will create its own EventBase wrapper)
    g_subscriber = std::make_unique<SimpleSubscriber>(base);
    if (!g_subscriber) {
        std::cerr << "Failed to create subscriber" << std::endl;
        return 1;
    }
    
    // Configure subscriber
    if (!tcp_address.empty()) {
        g_subscriber->set_address(TCP_SOCKET, tcp_address, tcp_port);
        std::cout << "Connecting to TCP: " << tcp_address << ":" << tcp_port << std::endl;
    } else {
        g_subscriber->set_address(UNIX_SOCKET, socket_path);
        std::cout << "Connecting to Unix socket: " << socket_path << std::endl;
    }
    
    g_subscriber->set_subscription_mask(topic_mask);
    g_subscriber->set_topic_callback(topic_data_callback);
    
    // Print configuration
    std::cout << "Subscriber Configuration:" << std::endl;
    std::cout << "  Name: " << subscriber_name << std::endl;
    std::cout << "  ID: " << subscriber_id << std::endl;
    std::cout << "  Topics: " << topics_str << " (mask: 0x" << std::hex << topic_mask << std::dec << ")" << std::endl;
    std::cout << "  Storage: " << storage_type << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Connect to server
    if (!g_subscriber->connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    std::cout << "Connected successfully, starting event loop..." << std::endl;
    
    // Main event loop
    while (g_running) {
        // Run event loop with timeout to allow periodic checks
        struct timeval timeout = {1, 0}; // 1 second timeout
        int result = event_base_loopexit(base, &timeout);
        if (result != 0) {
            std::cerr << "Failed to set event loop timeout" << std::endl;
            break;
        }
        
        result = event_base_dispatch(base);
        if (result == -1) {
            std::cerr << "Event loop error" << std::endl;
            break;
        }
        
        // Check if we should continue running
        if (!g_running) {
            break;
        }
    }
    
    std::cout << "Shutting down..." << std::endl;
    
    // Cleanup
    g_subscriber.reset();
    storage.reset();
    
    if (g_log_file.is_open()) {
        g_log_file.close();
    }
    
    event_base_free(base);
    
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}