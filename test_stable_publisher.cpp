#include <iostream>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <event.h>
#include <chrono>
#include <thread>
#include <atomic>
#include "pubsub/StablePublisher.h"

using namespace SimplePubSub;

// Global variables
std::atomic<bool> running(true);
StablePublisher* g_publisher = nullptr;

void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
    running = false;
    
    if (g_publisher) {
        g_publisher->stop();
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -u PATH          Unix socket path (default: /tmp/stable_test.sock)\n"
              << "  -t HOST          TCP host address (default: 127.0.0.1)\n"
              << "  -p PORT          TCP port (default: 9999)\n"
              << "  --threaded       Use multi-threaded mode (default: single-threaded)\n"
              << "  --publish-interval MS  Publish interval in milliseconds (default: 2000)\n"
              << "  --help           Show this help message\n"
              << std::endl;
}

void publisher_stats_thread(StablePublisher* publisher) {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!running) break;
        
        std::cout << "StablePublisher Stats: "
                  << "Messages=" << publisher->messages_published()
                  << " Clients=" << publisher->client_count()
                  << " Connected=" << publisher->clients_connected()
                  << " Mode=" << (publisher->thread_mode() == StablePublisher::MULTI_THREADED ? "MULTI" : "SINGLE")
                  << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string unix_socket = "/tmp/stable_test.sock";
    std::string tcp_host = "127.0.0.1";
    int tcp_port = 9999;
    StablePublisher::ThreadMode mode = StablePublisher::SINGLE_THREADED;
    int publish_interval = 2000;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-u" && i + 1 < argc) {
            unix_socket = argv[++i];
        } else if (arg == "-t" && i + 1 < argc) {
            tcp_host = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            tcp_port = std::atoi(argv[++i]);
        } else if (arg == "--threaded") {
            mode = StablePublisher::MULTI_THREADED;
        } else if (arg == "--publish-interval" && i + 1 < argc) {
            publish_interval = std::atoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create main event base
    struct event_base* main_base = event_base_new();
    if (!main_base) {
        std::cerr << "Failed to create main event base" << std::endl;
        return 1;
    }
    
    try {
        std::cout << "Starting StablePublisher in " 
                  << (mode == StablePublisher::MULTI_THREADED ? "MULTI_THREADED" : "SINGLE_THREADED")
                  << " mode" << std::endl;
        
        // Create publisher
        StablePublisher publisher(main_base, "TestStablePublisher", 400, mode);
        g_publisher = &publisher;
        
        // Start Unix socket listener
        std::cout << "Starting Unix socket listener at: " << unix_socket << std::endl;
        if (!publisher.start_unix(unix_socket)) {
            std::cerr << "Failed to start Unix socket listener" << std::endl;
            return 1;
        }
        
        // Start TCP listener
        std::cout << "Starting TCP listener at: " << tcp_host << ":" << tcp_port << std::endl;
        if (!publisher.start_tcp(tcp_host, tcp_port)) {
            std::cerr << "Failed to start TCP listener" << std::endl;
            return 1;
        }
        
        std::cout << "StablePublisher started successfully!" << std::endl;
        std::cout << "Publishing messages every " << publish_interval << "ms" << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        // Start stats thread
        std::thread stats_thread(publisher_stats_thread, &publisher);
        
        // Message publishing loop
        auto last_publish = std::chrono::steady_clock::now();
        DataTopic topics[] = {TOPIC1, TOPIC2, MISC};
        int topic_index = 0;
        uint32_t message_count = 0;
        
        while (running) {
            // Set timeout for event loop
            struct timeval timeout = {0, 100000}; // 100ms
            int result = event_base_loopexit(main_base, &timeout);
            if (result != 0) {
                std::cerr << "Failed to set event loop timeout" << std::endl;
                break;
            }
            
            // Run event loop
            result = event_base_dispatch(main_base);
            if (result == -1) {
                std::cerr << "Event loop error" << std::endl;
                break;
            }
            
            // Check if it's time to publish
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_publish);
            
            if (elapsed.count() >= publish_interval && publisher.client_count() > 0) {
                DataTopic topic = topics[topic_index];
                std::string message = "StablePublisher test message #" + std::to_string(message_count) + 
                                      " topic=" + topic_to_string(topic);
                
                if (publisher.publish(topic, message.c_str(), message.length())) {
                    message_count++;
                    topic_index = (topic_index + 1) % 3;
                    last_publish = now;
                }
            }
            
            if (!running) break;
        }
        
        std::cout << "\nShutting down StablePublisher..." << std::endl;
        
        // Stop the publisher
        publisher.stop();
        
        // Wait for stats thread to finish
        running = false;
        if (stats_thread.joinable()) {
            stats_thread.join();
        }
        
        g_publisher = nullptr;
        
        std::cout << "Final Stats: Messages=" << publisher.messages_published()
                  << " Total Connected=" << publisher.clients_connected() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        event_base_free(main_base);
        return 1;
    }
    
    // Clean up
    event_base_free(main_base);
    
    std::cout << "StablePublisher test completed" << std::endl;
    return 0;
}