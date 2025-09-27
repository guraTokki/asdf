/*
 * PubSub 통합 테스트 스위트
 * 
 * 이 파일은 SimplePubSub 시스템의 통합 테스트를 실행하는 프로그램입니다.
 * Publisher와 Subscriber 간의 연결, 구독, 실시간 메시지 발행, 토픽 필터링, 고주파수 메시지 처리 등을 테스트합니다.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cassert>
#include <signal.h>

#include <event2/event.h>
#include "common/Common.h"
#include "pubsub/SimplePublisher.h"
#include "pubsub/SimpleSubscriber.h" 
#include "pubsub/FileSequenceStorage.h"

using namespace SimplePubSub;

class PubSubTestSuite {
private:
    std::unique_ptr<SimplePublisher> publisher;
    std::unique_ptr<SimpleSubscriber> subscriber;
    std::unique_ptr<FileSequenceStorage> storage;
    struct event_base* event_base;
    std::string test_socket_path;
    std::string test_storage_dir;
    
    // Test results tracking
    std::atomic<int> messages_received{0};
    std::atomic<int> subscription_responses{0};
    std::atomic<int> recovery_responses{0};
    std::atomic<int> recovery_completes{0};
    std::atomic<bool> test_running{true};
    
    std::vector<std::pair<DataTopic, std::string>> received_messages;
    std::mutex received_messages_mutex;

public:
    PubSubTestSuite() 
        : test_socket_path("/tmp/test_pubsub_integration.sock")
        , test_storage_dir("./test_integration_storage") {
        
        event_base = event_base_new();
        if (!event_base) {
            throw std::runtime_error("Failed to create event base");
        }
    }
    
    ~PubSubTestSuite() {
        cleanup();
        if (event_base) {
            event_base_free(event_base);
        }
    }
    
    void cleanup() {
        test_running = false;
        subscriber.reset();
        publisher.reset();
        storage.reset();
        unlink(test_socket_path.c_str());
    }

    bool setupPublisher() {
        std::cout << "=== Setting up Publisher ===" << std::endl;
        
        // Create storage
        storage = std::make_unique<FileSequenceStorage>(test_storage_dir, "test_publisher");
        if (!storage->initialize()) {
            std::cerr << "Failed to initialize storage" << std::endl;
            return false;
        }
        
        // Create publisher
        publisher = std::make_unique<SimplePublisher>(event_base);
        publisher->set_sequence_storage(storage.get());
        
        // Start Unix listener
        if (!publisher->start_unix(test_socket_path)) {
            std::cerr << "Failed to start publisher" << std::endl;
            return false;
        }
        
        std::cout << "Publisher started on: " << test_socket_path << std::endl;
        return true;
    }
    
    bool setupSubscriber() {
        std::cout << "=== Setting up Subscriber ===" << std::endl;
        
        subscriber = std::make_unique<SimpleSubscriber>(event_base);
        
        // Set up topic callback
        subscriber->set_topic_callback([this](DataTopic topic, const char* data, size_t size) {
            std::lock_guard<std::mutex> lock(received_messages_mutex);
            std::string message(data, size);
            received_messages.push_back({topic, message});
            messages_received++;
            
            std::cout << "Received: " << topic_to_string(topic) << " - " << message << std::endl;
        });
        
        // Configure subscriber
        subscriber->set_address(UNIX_SOCKET, test_socket_path, 0);
        subscriber->set_subscription_mask(ALL_TOPICS);
        
        // Connect to publisher
        if (!subscriber->connect()) {
            std::cerr << "Failed to connect subscriber" << std::endl;
            return false;
        }
        
        std::cout << "Subscriber connected successfully" << std::endl;
        return true;
    }
    
    void runEventLoop(int duration_seconds) {
        std::cout << "=== Running event loop for " << duration_seconds << " seconds ===" << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        while (test_running) {
            struct timeval timeout = {0, 100000}; // 100ms timeout
            event_base_loopexit(event_base, &timeout);
            event_base_dispatch(event_base);
            
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= std::chrono::seconds(duration_seconds)) {
                break;
            }
        }
    }

    // Test Case 1: Basic Connection Test
    bool testConnection() {
        std::cout << "\n=== TEST 1: Connection Test ===" << std::endl;
        
        if (!setupPublisher()) return false;
        
        // Wait a bit for publisher to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (!setupSubscriber()) return false;
        
        // Run event loop to process connection
        runEventLoop(2);
        
        std::cout << "Connection test passed!" << std::endl;
        return true;
    }
    
    // Test Case 2: Subscription Flow Test
    bool testSubscription() {
        std::cout << "\n=== TEST 2: Subscription Flow Test ===" << std::endl;
        
        // Reset counters
        messages_received = 0;
        
        // Run event loop to allow subscription to complete
        runEventLoop(3);
        
        // Check if we received any recovery messages
        bool subscription_ok = messages_received >= 10; // Should get recovery messages
        
        std::cout << "Received " << messages_received << " messages during subscription" << std::endl;
        
        if (subscription_ok) {
            std::cout << "Subscription test passed!" << std::endl;
        } else {
            std::cout << "Subscription test failed!" << std::endl;
        }
        
        return subscription_ok;
    }
    
    // Test Case 3: Real-time Publishing Test
    bool testRealTimePublishing() {
        std::cout << "\n=== TEST 3: Real-time Publishing Test ===" << std::endl;
        
        int initial_count = messages_received.load();
        
        // Publish test messages
        std::vector<std::pair<DataTopic, std::string>> test_messages = {
            {TOPIC1, "Test message 1 for TOPIC1"},
            {TOPIC2, "Test message 2 for TOPIC2"}, 
            {MISC, "Test message 3 for MISC"}
        };
        
        for (const auto& [topic, message] : test_messages) {
            publisher->publish(topic, message.c_str(), message.size());
            std::cout << "Published: " << topic_to_string(topic) << " - " << message << std::endl;
        }
        
        // Run event loop to process messages
        runEventLoop(2);
        
        int final_count = messages_received.load();
        int new_messages = final_count - initial_count;
        
        std::cout << "Published " << test_messages.size() << " messages, received " << new_messages << " new messages" << std::endl;
        
        bool publishing_ok = new_messages >= static_cast<int>(test_messages.size());
        
        if (publishing_ok) {
            std::cout << "Real-time publishing test passed!" << std::endl;
        } else {
            std::cout << "Real-time publishing test failed!" << std::endl;
        }
        
        return publishing_ok;
    }
    
    // Test Case 4: Topic Filtering Test
    bool testTopicFiltering() {
        std::cout << "\n=== TEST 4: Topic Filtering Test ===" << std::endl;
        
        // Clear received messages
        {
            std::lock_guard<std::mutex> lock(received_messages_mutex);
            received_messages.clear();
        }
        
        // Publish messages to different topics
        publisher->publish(TOPIC1, "TOPIC1 message", 13);
        publisher->publish(TOPIC2, "TOPIC2 message", 13);
        publisher->publish(MISC, "MISC message", 12);
        
        // Run event loop
        runEventLoop(1);
        
        // Check received topics
        std::lock_guard<std::mutex> lock(received_messages_mutex);
        
        bool has_topic1 = false, has_topic2 = false, has_misc = false;
        
        for (const auto& [topic, message] : received_messages) {
            if (topic == TOPIC1) has_topic1 = true;
            if (topic == TOPIC2) has_topic2 = true;
            if (topic == MISC) has_misc = true;
        }
        
        bool filtering_ok = has_topic1 && has_topic2 && has_misc;
        
        std::cout << "Received topics - TOPIC1: " << has_topic1 
                  << ", TOPIC2: " << has_topic2 
                  << ", MISC: " << has_misc << std::endl;
        
        if (filtering_ok) {
            std::cout << "Topic filtering test passed!" << std::endl;
        } else {
            std::cout << "Topic filtering test failed!" << std::endl;
        }
        
        return filtering_ok;
    }
    
    // Test Case 5: High Frequency Publishing Test
    bool testHighFrequencyPublishing() {
        std::cout << "\n=== TEST 5: High Frequency Publishing Test ===" << std::endl;
        
        int initial_count = messages_received.load();
        const int NUM_MESSAGES = 50;
        
        // Publish many messages rapidly
        for (int i = 0; i < NUM_MESSAGES; i++) {
            DataTopic topic = static_cast<DataTopic>((i % 3) + 1); // TOPIC1, TOPIC2, MISC
            std::string message = "High freq message #" + std::to_string(i);
            
            publisher->publish(topic, message.c_str(), message.size());
            
            // Small delay to avoid overwhelming
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Run event loop to process all messages
        runEventLoop(3);
        
        int final_count = messages_received.load();
        int new_messages = final_count - initial_count;
        
        std::cout << "Published " << NUM_MESSAGES << " messages, received " << new_messages << " new messages" << std::endl;
        
        // Allow for some message loss due to rapid publishing
        bool high_freq_ok = new_messages >= (NUM_MESSAGES * 0.8); // 80% delivery rate
        
        if (high_freq_ok) {
            std::cout << "High frequency publishing test passed!" << std::endl;
        } else {
            std::cout << "High frequency publishing test failed!" << std::endl;
        }
        
        return high_freq_ok;
    }
    
    /**
     * 모든 테스트 케이스를 순차적으로 실행하는 메인 함수
     * 각 테스트의 성공/실패를 추적하고 최종 결과를 출력합니다.
     * @return 모든 테스트가 성공하면 true, 하나라도 실패하면 false
     */
    bool runAllTests() {
        std::cout << "===========================================" << std::endl;
        std::cout << "     PubSub Integration Test Suite" << std::endl;
        std::cout << "===========================================" << std::endl;
        
        // 실행할 테스트 케이스들의 목록 정의
        // 람다 함수를 사용하여 각 테스트 메소드를 래핑
        std::vector<std::pair<std::string, std::function<bool()>>> tests = {
            {"Connection", [this]() { return testConnection(); }},                      // 기본 연결 테스트
            {"Subscription", [this]() { return testSubscription(); }},                // 구독 플로우 테스트
            {"Real-time Publishing", [this]() { return testRealTimePublishing(); }},  // 실시간 발행 테스트
            {"Topic Filtering", [this]() { return testTopicFiltering(); }},           // 토픽 필터링 테스트
            {"High Frequency Publishing", [this]() { return testHighFrequencyPublishing(); }}  // 고주파수 발행 테스트
        };
        
        int passed = 0;              // 통과한 테스트 개수
        int total = tests.size();    // 전체 테스트 개수
        
        // 각 테스트를 순차적으로 실행
        for (auto& [name, test_func] : tests) {
            try {
                bool result = test_func();  // 테스트 함수 실행
                if (result) {
                    passed++;
                    std::cout << "✅ " << name << " PASSED" << std::endl;
                } else {
                    std::cout << "❌ " << name << " FAILED" << std::endl;
                }
            } catch (const std::exception& e) {
                // 예외 발생 시 처리
                std::cout << "❌ " << name << " EXCEPTION: " << e.what() << std::endl;
            }
            
            std::cout << std::endl;  // 테스트 간 구분을 위한 빈 줄
        }
        
        // 최종 테스트 결과 출력
        std::cout << "===========================================" << std::endl;
        std::cout << "Test Results: " << passed << "/" << total << " tests passed" << std::endl;
        
        if (passed == total) {
            std::cout << "🎉 ALL TESTS PASSED!" << std::endl;
            return true;
        } else {
            std::cout << "❌ Some tests failed" << std::endl;
            return false;
        }
    }
};

/**
 * 메인 함수
 * PubSubTestSuite를 생성하고 모든 테스트를 실행합니다.
 * 테스트 결과에 따라 적절한 exit code를 반환합니다.
 * 
 * @param argc 명령행 인수 개수 (현재 사용되지 않음)
 * @param argv 명령행 인수 배열 (현재 사용되지 않음)
 * @return 모든 테스트 성공 시 0, 실패 시 1
 */
int main(int argc, char* argv[]) {
    try {
        // 테스트 스위트 인스턴스 생성
        PubSubTestSuite test_suite;
        
        // 모든 통합 테스트 실행
        bool success = test_suite.runAllTests();
        
        // 테스트 결과에 따른 exit code 반환 (성공: 0, 실패: 1)
        return success ? 0 : 1;
        
    } catch (const std::exception& e) {
        // 테스트 스위트 실행 중 예외 발생 시 처리
        std::cerr << "Test suite exception: " << e.what() << std::endl;
        return 1;  // 예외 발생 시 실패 코드 반환
    }
}