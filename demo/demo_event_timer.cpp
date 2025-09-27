#include <iostream>
#include <memory>
#include <chrono>
#include <event2/event.h>
#include <eventBase/EventBase.h>
#include <eventBase/EventTimer.h>

int main() {
    struct event_base* base = event_base_new();
    if (!base) {
        std::cerr << "Failed to create event base" << std::endl;
        return 1;
    }

    // ========== Direct EventTimer Creation Demo ==========
    std::cout << "=== Direct EventTimer Creation Demo ===" << std::endl;
    
    // Create EventTimer instance
    EventTimer timer(base, false);  // base_owned = false (we manage the base)

    // Set timeout callback
    timer.setTimeoutCallback([](char* data, int size) {
        static int count = 0;
        count++;
        std::cout << "Direct timer callback triggered! Count: " << count << std::endl;
        
        // Stop after 3 triggers for periodic timer demo
        if (count >= 3) {
            std::cout << "Stopping periodic timer after 3 triggers" << std::endl;
        }
    });

    std::cout << "Starting one-shot timer (2 seconds)..." << std::endl;
    timer.startOnce(std::chrono::seconds(2));
    
    // Run event loop for one-shot timer
    event_base_dispatch(base);
    
    std::cout << "One-shot timer completed. Starting periodic timer (1 second interval)..." << std::endl;
    timer.startPeriodic(std::chrono::milliseconds(1000));
    
    // Run event loop with exit after a few iterations
    struct timeval timeout = {5, 0}; // 5 seconds total
    event_base_loopexit(base, &timeout);
    event_base_dispatch(base);
    
    std::cout << "Direct timer demo completed" << std::endl;

    // ========== Factory Pattern EventTimer Creation Demo ==========
    std::cout << "\n=== Factory Pattern EventTimer Creation Demo ===" << std::endl;

    // Test factory pattern
    std::unique_ptr<EventBase> factory_timer(createEventBase("timer", base, false));
    if (!factory_timer) {
        std::cerr << "Failed to create timer via factory" << std::endl;
        event_base_free(base);
        return 1;
    }

    // Set callback
    factory_timer->setTimeoutCallback([](char* data, int size) {
        std::cout << "Factory-created timer callback triggered!" << std::endl;
    });

    // Start timer (using EventTimer-specific methods requires casting)
    EventTimer* timer_ptr = dynamic_cast<EventTimer*>(factory_timer.get());
    if (timer_ptr) {
        std::cout << "Successfully created EventTimer via factory pattern" << std::endl;
        timer_ptr->startOnce(std::chrono::milliseconds(1500));
        
        // Run event loop with timeout
        struct timeval factory_timeout = {3, 0}; // 3 seconds
        event_base_loopexit(base, &factory_timeout);
        event_base_dispatch(base);
    } else {
        std::cerr << "Failed to cast to EventTimer" << std::endl;
        event_base_free(base);
        return 1;
    }

    std::cout << "Factory test completed successfully" << std::endl;
    
    // Clean up
    event_base_free(base);
    
    std::cout << "\nAll timer demos completed" << std::endl;
    return 0;
}