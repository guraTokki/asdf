#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <signal.h>
#include <getopt.h>

#include <event2/event.h>
#include "common/Common.h"
#include "pubsub/AbstractPublisher.h"
#include "pubsub/ThreadedSimplePublisher.h"
#include "pubsub/NonThreadedSimplePublisher.h"
#include "pubsub/FileSequenceStorage.h"

using namespace SimplePubSub;

// Global variables
static bool g_running = true;
static std::unique_ptr<AbstractPublisher> g_publisher;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down gracefully..." << std::endl;
    g_running = false;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -u <socket_path>  : Unix socket path (default: /tmp/test_publisher.sock)" << std::endl;
    std::cout << "  -t <ip:port>      : TCP address (e.g., 127.0.0.1:9999)" << std::endl;
    std::cout << "  --both            : Start both Unix and TCP listeners" << std::endl;
    std::cout << "  --threaded        : Use threaded publisher (default: single-threaded)" << std::endl;
    std::cout << "  --name <name>     : Publisher name (default: TestPublisher)" << std::endl;
    std::cout << "  --id <id>         : Publisher ID (default: 100)" << std::endl;
    std::cout << "  --publish-interval <ms> : Auto publish interval in ms (default: 1000)" << std::endl;
    std::cout << "  --storage-dir <dir>     : Storage directory (default: ./test_storage)" << std::endl;
    std::cout << "  -h                : Show this help" << std::endl;
}

// Auto publish sample data for testing
void auto_publish_thread(int interval_ms) {
    int counter = 0;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        
        if (!g_running || !g_publisher) {
            break;
        }
        
        counter++;
        
        // Publish to different topics in round-robin fashion
        DataTopic topic = static_cast<DataTopic>((counter % 3) + 1); // TOPIC1, TOPIC2, MISC
        
        std::string sample_data = "Test message #" + std::to_string(counter) + 
                                 " for " + topic_to_string(topic);
        
        g_publisher->publish(topic, sample_data.c_str(), sample_data.length());
        
        std::cout << "Auto-published: " << sample_data << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // Default values
    std::string unix_socket_path = "/tmp/test_publisher.sock";
    std::string tcp_address = "";
    int tcp_port = 0;
    bool start_both = false;
    bool use_threaded = false;  // Default to single-threaded
    std::string publisher_name = "TestPublisher";
    uint32_t publisher_id = 100;
    int publish_interval = 1000; // 1 second
    std::string storage_dir = "./test_storage";
    
    // Parse command line options
    static struct option long_options[] = {
        {"both", no_argument, 0, 'b'},
        {"threaded", no_argument, 0, 'T'},
        {"name", required_argument, 0, 'n'},
        {"id", required_argument, 0, 'i'},
        {"publish-interval", required_argument, 0, 'p'},
        {"storage-dir", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int long_index = 0;
    
    while ((opt = getopt_long(argc, argv, "u:t:h", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'u':
                unix_socket_path = optarg;
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
            case 'b':
                start_both = true;
                break;
            case 'T':
                use_threaded = true;
                break;
            case 'n':
                publisher_name = optarg;
                break;
            case 'i':
                publisher_id = std::stoul(optarg);
                break;
            case 'p':
                publish_interval = std::stoi(optarg);
                break;
            case 's':
                storage_dir = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Create event base
    struct event_base* base = event_base_new();
    if (!base) {
        std::cerr << "Failed to create event base" << std::endl;
        return 1;
    }
    
    // Create file sequence storage
    auto storage = std::make_unique<FileSequenceStorage>(storage_dir, "publisher");
    if (!storage->initialize()) {
        std::cerr << "Failed to initialize sequence storage" << std::endl;
        return 1;
    }
    
    // Create publisher based on threading mode
    if (use_threaded) {
        std::cout << "Creating ThreadedSimplePublisher..." << std::endl;
        g_publisher = std::make_unique<ThreadedSimplePublisher>(base);
    } else {
        std::cout << "Creating NonThreadedSimplePublisher..." << std::endl;
        g_publisher = std::make_unique<NonThreadedSimplePublisher>(base);
    }
    
    if (!g_publisher) {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }
    
    // Set publisher configuration
    g_publisher->set_sequence_storage(storage.get());
    
    // Start listeners
    bool listener_started = false;
    if (start_both && !tcp_address.empty()) {
        if (g_publisher->start_both(unix_socket_path, tcp_address, tcp_port)) {
            std::cout << "Started both Unix (" << unix_socket_path << ") and TCP (" 
                      << tcp_address << ":" << tcp_port << ") listeners" << std::endl;
            listener_started = true;
        }
    } else if (!tcp_address.empty()) {
        if (g_publisher->start_tcp(tcp_address, tcp_port)) {
            std::cout << "Started TCP listener: " << tcp_address << ":" << tcp_port << std::endl;
            listener_started = true;
        }
    } else {
        if (g_publisher->start_unix(unix_socket_path)) {
            std::cout << "Started Unix listener: " << unix_socket_path << std::endl;
            listener_started = true;
        }
    }
    
    if (!listener_started) {
        std::cerr << "Failed to start any listener" << std::endl;
        return 1;
    }
    
    // Print configuration
    std::cout << "Publisher Configuration:" << std::endl;
    std::cout << "  Name: " << publisher_name << std::endl;
    std::cout << "  ID: " << publisher_id << std::endl;
    std::cout << "  Mode: " << (use_threaded ? "Multi-threaded" : "Single-threaded") << std::endl;
    std::cout << "  Storage: " << storage_dir << std::endl;
    std::cout << "  Auto-publish interval: " << publish_interval << "ms" << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Start auto-publish thread
    std::thread publish_thread(auto_publish_thread, publish_interval);
    
    std::cout << "Publisher started successfully. Use Ctrl+C to stop." << std::endl;
    
    // Main event loop
    while (g_running) {
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
        
        if (!g_running) {
            break;
        }
    }
    
    std::cout << "Shutting down publisher..." << std::endl;
    
    // Stop auto-publish thread
    if (publish_thread.joinable()) {
        publish_thread.join();
    }
    
    // Cleanup
    g_publisher.reset();
    storage.reset();
    event_base_free(base);
    
    std::cout << "Publisher shutdown complete." << std::endl;
    return 0;
}