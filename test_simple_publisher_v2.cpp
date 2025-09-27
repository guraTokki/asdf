#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <signal.h>
#include <getopt.h>
#include <atomic>
#include <vector>

#include <event2/event.h>
#include "pubsub/Common.h"
#include "pubsub/SimplePublisherV2.h"
#include "pubsub/FileSequenceStorage.h"

using namespace SimplePubSub;

// Global variables for signal handling
static std::atomic<bool> g_running{true};
static SimplePublisherV2* g_publisher = nullptr;
static struct event_base* g_event_base = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down gracefully..." << std::endl;
    g_running = false;
    if (g_event_base) {
        event_base_loopbreak(g_event_base);
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -u <socket_path>  : Unix socket path (default: /tmp/test_pubsub_v2.sock)" << std::endl;
    std::cout << "  -t <ip:port>      : TCP address (e.g., 127.0.0.1:9999)" << std::endl;
    std::cout << "  --name <name>     : Publisher name (default: TestPublisherV2)" << std::endl;
    std::cout << "  --id <id>         : Publisher ID (default: 1)" << std::endl;
    std::cout << "  --auto-publish <ms> : Auto publish interval in ms (0 = disabled, default: 0)" << std::endl;
    std::cout << "  --recovery-threads <n> : Number of recovery threads (default: 2)" << std::endl;
    std::cout << "  --storage-dir <dir>    : Storage directory (default: ./test_storage)" << std::endl;
    std::cout << "  -h                : Show this help" << std::endl;
}

// Auto publish sample data for testing
void auto_publish_thread(SimplePublisherV2* publisher, int interval_ms) {
    int counter = 0;
    std::vector<DataTopic> topics = {TOPIC1, TOPIC2, MISC};

    std::cout << "Auto-publish thread started (interval: " << interval_ms << "ms)" << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));

        if (!g_running || !publisher) {
            break;
        }

        // Rotate through different topics
        DataTopic topic = topics[counter % topics.size()];
        std::string message = "Auto message " + std::to_string(counter) + " for " + topic_to_string(topic);

        publisher->publish(topic, message.c_str(), message.length());

        std::cout << "Published: [" << topic_to_string(topic) << "] " << message << std::endl;
        counter++;
    }

    std::cout << "Auto-publish thread stopped" << std::endl;
}

// Interactive command handler
void interactive_mode(SimplePublisherV2* publisher) {
    std::cout << "\n=== Interactive Mode ===" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  p1 <message>  : Publish to TOPIC1" << std::endl;
    std::cout << "  p2 <message>  : Publish to TOPIC2" << std::endl;
    std::cout << "  pm <message>  : Publish to MISC" << std::endl;
    std::cout << "  quit          : Exit" << std::endl;
    std::cout << "Enter command: ";

    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << "Enter command: ";
            continue;
        }

        if (line == "quit" || line == "q") {
            g_running = false;
            break;
        }

        // Parse command
        size_t space_pos = line.find(' ');
        std::string cmd = line.substr(0, space_pos);
        std::string message = (space_pos != std::string::npos) ? line.substr(space_pos + 1) : "";

        DataTopic topic;
        bool valid_cmd = true;

        if (cmd == "p1") {
            topic = TOPIC1;
        } else if (cmd == "p2") {
            topic = TOPIC2;
        } else if (cmd == "pm") {
            topic = MISC;
        } else {
            std::cout << "Unknown command: " << cmd << std::endl;
            valid_cmd = false;
        }

        if (valid_cmd) {
            if (message.empty()) {
                message = "Interactive message from " + cmd;
            }

            publisher->publish(topic, message.c_str(), message.length());
            std::cout << "Published: [" << topic_to_string(topic) << "] " << message << std::endl;
        }

        if (g_running) {
            std::cout << "Enter command: ";
        }
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string unix_path = "/tmp/test_pubsub_v2.sock";
    std::string tcp_address = "";
    uint16_t tcp_port = 0;
    std::string publisher_name = "TestPublisherV2";
    uint32_t publisher_id = 1;
    int auto_publish_interval = 0; // 0 = disabled
    size_t recovery_threads = 2;
    std::string storage_dir = "./test_storage";
    bool use_tcp = false;

    // Parse command line arguments
    int opt;
    static struct option long_options[] = {
        {"name", required_argument, 0, 'n'},
        {"id", required_argument, 0, 'i'},
        {"auto-publish", required_argument, 0, 'a'},
        {"recovery-threads", required_argument, 0, 'r'},
        {"storage-dir", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "u:t:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'u':
                unix_path = optarg;
                break;
            case 't': {
                std::string addr_port = optarg;
                size_t colon_pos = addr_port.find(':');
                if (colon_pos != std::string::npos) {
                    tcp_address = addr_port.substr(0, colon_pos);
                    tcp_port = static_cast<uint16_t>(std::stoi(addr_port.substr(colon_pos + 1)));
                    use_tcp = true;
                } else {
                    std::cerr << "Invalid TCP address format. Use ip:port" << std::endl;
                    return 1;
                }
                break;
            }
            case 'n':
                publisher_name = optarg;
                break;
            case 'i':
                publisher_id = static_cast<uint32_t>(std::stoi(optarg));
                break;
            case 'a':
                auto_publish_interval = std::stoi(optarg);
                break;
            case 'r':
                recovery_threads = static_cast<size_t>(std::stoi(optarg));
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

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "=== SimplePublisherV2 Test ===" << std::endl;
    std::cout << "Publisher ID: " << publisher_id << std::endl;
    std::cout << "Publisher Name: " << publisher_name << std::endl;

    if (use_tcp) {
        std::cout << "TCP Address: " << tcp_address << ":" << tcp_port << std::endl;
    } else {
        std::cout << "Unix Socket: " << unix_path << std::endl;
    }

    std::cout << "Recovery Threads: " << recovery_threads << std::endl;
    std::cout << "Storage Directory: " << storage_dir << std::endl;

    if (auto_publish_interval > 0) {
        std::cout << "Auto-publish interval: " << auto_publish_interval << "ms" << std::endl;
    }

    // Create libevent base
    g_event_base = event_base_new();
    if (!g_event_base) {
        std::cerr << "Failed to create event base" << std::endl;
        return 1;
    }

    try {
        // Create publisher
        SimplePublisherV2 publisher(g_event_base);
        g_publisher = &publisher;

        // Configure publisher
        publisher.set_publisher_id(publisher_id);
        publisher.set_publisher_name(publisher_name);

        // Set up storage
        // FileSequenceStorage storage(storage_dir);
        // publisher.set_sequence_storage(&storage);

        // Configure network address
        if (use_tcp) {
            publisher.set_address(TCP_SOCKET, tcp_address, tcp_port);
        } else {
            publisher.set_address(UNIX_SOCKET, unix_path);
        }
        publisher.init_database("/tmp/test_pubsub_v2.db");

        // Initialize sequence storage (HashMaster by default)
        std::cout << "Initializing sequence storage..." << std::endl;
        if (!publisher.init_sequence_storage(SimplePubSub::StorageType::HASHMASTER_STORAGE)) {
            std::cerr << "Failed to initialize sequence storage" << std::endl;
            return 1;
        }

        // Start publisher
        std::cout << "Starting publisher..." << std::endl;
        if (!publisher.start(recovery_threads)) {
            std::cerr << "Failed to start publisher" << std::endl;
            return 1;
        }

        std::cout << "Publisher started successfully!" << std::endl;
        std::cout << "Waiting for clients to connect..." << std::endl;

        // Start auto-publish thread if enabled
        std::thread auto_pub_thread;
        if (auto_publish_interval > 0) {
            auto_pub_thread = std::thread(auto_publish_thread, &publisher, auto_publish_interval);
        }

        // Start interactive mode in a separate thread if auto-publish is disabled
        std::thread interactive_thread;
        if (auto_publish_interval == 0) {
            interactive_thread = std::thread(interactive_mode, &publisher);
        }

        // Run event loop
        std::cout << "Starting event loop..." << std::endl;
        while (g_running) {
            // Run event loop with timeout to allow periodic checks
            struct timeval tv = {1, 0}; // 1 second timeout
            int result = event_base_loopexit(g_event_base, &tv);
            if (result != 0) {
                std::cerr << "Failed to set event loop timeout" << std::endl;
                break;
            }

            result = event_base_dispatch(g_event_base);
            if (result < 0) {
                std::cerr << "Event loop error" << std::endl;
                break;
            }

            // Check if we should continue
            if (!g_running) {
                break;
            }
        }

        std::cout << "Event loop ended" << std::endl;

        // Clean up threads
        g_running = false;

        if (auto_pub_thread.joinable()) {
            auto_pub_thread.join();
        }

        if (interactive_thread.joinable()) {
            // Send a newline to wake up getline
            std::cout << "\nShutting down..." << std::endl;
            interactive_thread.detach(); // Don't wait for stdin
        }

        // Stop publisher
        std::cout << "Stopping publisher..." << std::endl;
        publisher.stop();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    // Clean up
    g_publisher = nullptr;
    event_base_free(g_event_base);
    g_event_base = nullptr;

    std::cout << "Test completed successfully" << std::endl;
    return 0;
}