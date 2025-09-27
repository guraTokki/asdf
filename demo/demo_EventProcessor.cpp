
#include "Command.h"

int main() {
    EventProcessor processor;

    // 워커 스레드 1: 이벤트 던지기
    std::thread t1([&]() {
        for (int i = 0; i < 5; i++) {
            processor.throwEvent(std::make_unique<PrintCommand>("From thread 1: " + std::to_string(i)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // 워커 스레드 2: 이벤트 던지기
    std::thread t2([&]() {
        for (int i = 0; i < 5; i++) {
            processor.throwEvent(std::make_unique<PrintCommand>("From thread 2: " + std::to_string(i)));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    // 메인 스레드: 이벤트 처리 루프
    processor.run();

    t1.join();
    t2.join();
    return 0;
}