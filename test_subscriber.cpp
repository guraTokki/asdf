#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <getopt.h>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

#include <event2/event.h>
#include "pubsub/Common.h"
#include "pubsub/SimpleSubscriber.h"
#include "pubsub/FileSequenceStorage.h"

using namespace SimplePubSub;

// Global variables
static bool g_running = true;
static std::unique_ptr<SimpleSubscriber> g_subscriber;
static int g_message_count = 0;

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
    g_message_count++;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::string topic_str = topic_to_string(topic);
    
    // Parse TopicMessage structure
    if (size >= sizeof(TopicMessage)) {
        const TopicMessage* msg = reinterpret_cast<const TopicMessage*>(data);
        
        std::cout << "[" << timestamp << "] Message #" << g_message_count 
                  << " - Topic: " << topic_str 
                  << ", Global Seq: " << msg->global_seq
                  << ", Topic Seq: " << msg->topic_seq
                  << ", Size: " << msg->data_size << " bytes" << std::endl;
        
        // Print message data if it's printable
        if (msg->data_size > 0 && msg->data_size < 256) {
            std::string message_data(msg->data, 
                std::min(static_cast<size_t>(msg->data_size), static_cast<size_t>(size - sizeof(TopicMessage))));
            std::cout << "  Data: \"" << message_data << "\"" << std::endl;
        }
    } else {
        std::cout << "[" << timestamp << "] Received " << topic_str 
                  << " data (" << size << " bytes)" << std::endl;
    }
}

// Test recovery by simulating disconnection and reconnection
void test_recovery_thread() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    if (!g_running || !g_subscriber) {
        return;
    }
    
    std::cout << "\n=== Testing Recovery Mechanism ===" << std::endl;
    std::cout << "Simulating disconnect and reconnect for recovery test..." << std::endl;
    
    // Trigger recovery by changing status
    g_subscriber->change_status(CLIENT_RECOVERY_NEEDED);
    
    // Send recovery request after a short delay
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_running) {
        std::cout << "Sending recovery request..." << std::endl;
        g_subscriber->send_recovery_request();
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -u <socket_path>  : Unix socket path (default: /tmp/test_publisher.sock)" << std::endl;
    std::cout << "  -t <ip:port>      : TCP address (e.g., 127.0.0.1:9999)" << std::endl;
    std::cout << "  --name <name>     : Subscriber name (default: TestSubscriber)" << std::endl;
    std::cout << "  --id <id>         : Subscriber ID (default: 1001)" << std::endl;
    std::cout << "  --topics <topics> : Subscribe topics: topic1,topic2,misc,all (default: all)" << std::endl;
    std::cout << "  --storage-dir <dir>     : Storage directory (default: ./test_storage)" << std::endl;
    std::cout << "  --test-recovery   : Test recovery mechanism after 5 seconds" << std::endl;
    std::cout << "  --clear-storage   : Clear sequence storage on startup" << std::endl;
    std::cout << "  -h                : Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default values
    std::string socket_path = "/tmp/test_publisher.sock";
    std::string tcp_address = "";
    int tcp_port = 0;
    std::string subscriber_name = "TestSubscriber";
    uint32_t subscriber_id = 1001;
    std::string topics_str = "all";
    std::string storage_dir = "./test_storage";
    bool test_recovery = false;
    bool clear_storage = false;
    
    // Parse command line options
    static struct option long_options[] = {
        {"name", required_argument, 0, 'n'},
        {"id", required_argument, 0, 'i'},
        {"topics", required_argument, 0, 'T'},
        {"storage-dir", required_argument, 0, 's'},
        {"test-recovery", no_argument, 0, 'r'},
        {"clear-storage", no_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int long_index = 0;
    
    while ((opt = getopt_long(argc, argv, "u:t:h", long_options, &long_index)) != -1) {
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
            case 'n':
                subscriber_name = optarg;
                break;
            case 'i':
                subscriber_id = std::stoul(optarg);
                break;
            case 'T':
                topics_str = optarg;
                break;
            case 's':
                storage_dir = optarg;
                break;
            case 'r':
                test_recovery = true;
                break;
            case 'c':
                clear_storage = true;
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
    
    // Create event base
    struct event_base* base = event_base_new();
    if (!base) {
        std::cerr << "Failed to create event base" << std::endl;
        return 1;
    }
    
    // Create sequence storage
    auto storage = std::make_unique<FileSequenceStorage>(storage_dir, subscriber_name);
    if (!storage->initialize()) {
        std::cerr << "Failed to initialize sequence storage" << std::endl;
        return 1;
    }
    
    if (clear_storage) {
        std::cout << "Clearing sequence storage..." << std::endl;
        storage->clear();
    }
    
    // Create subscriber
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
    std::cout << "  Storage: " << storage_dir << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Connect to publisher
    if (!g_subscriber->connect()) {
        std::cerr << "Failed to connect to publisher" << std::endl;
        return 1;
    }
    
    std::cout << "Connected successfully to publisher!" << std::endl;
    
    // Start recovery test thread if requested
    std::thread recovery_thread;
    if (test_recovery) {
        std::cout << "Recovery test will start in 5 seconds..." << std::endl;
        recovery_thread = std::thread(test_recovery_thread);
    }
    
    std::cout << "Subscriber started. Waiting for messages... (Use Ctrl+C to stop)" << std::endl;
    
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
    
    std::cout << "\nShutting down subscriber..." << std::endl;
    std::cout << "Total messages received: " << g_message_count << std::endl;
    
    // Join recovery test thread if it was started
    if (recovery_thread.joinable()) {
        recovery_thread.join();
    }
    
    // Cleanup
    g_subscriber.reset();
    storage.reset();
    event_base_free(base);
    
    std::cout << "Subscriber shutdown complete." << std::endl;
    return 0;
}