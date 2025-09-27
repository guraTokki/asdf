#ifndef EVENTTIMER_H
#define EVENTTIMER_H

#include "EventBase.h"
#include <event2/event.h>
#include <chrono>
#include <functional>

/**
 * EventTimer 클래스 - libevent 기반 타이머 이벤트 처리
 * 
 * EventBase를 상속받아 타이머 기능을 제공하는 클래스입니다.
 * 일회성 타이머와 주기적 타이머를 모두 지원합니다.
 * 
 * 주요 기능:
 * - 일회성 타이머: startOnce() 사용
 * - 주기적 타이머: startPeriodic() 사용
 * - 타이머 정지: stop() 사용
 * - 콜백 기반 이벤트 처리
 * 
 * 사용 예시:
 * EventTimer timer(base);
 * timer.setTimeoutCallback([](char* data, int size) {
 *     std::cout << "Timer fired!" << std::endl;
 * });
 * timer.startOnce(std::chrono::seconds(5));  // 5초 후 한 번 실행
 * timer.startPeriodic(std::chrono::milliseconds(1000));  // 1초마다 반복
 */
class EventTimer : public EventBase {
private:
    struct event* _timer_event;     // libevent 타이머 이벤트
    bool _is_periodic;              // 주기적 타이머 여부
    std::chrono::milliseconds _interval;  // 타이머 간격
    bool _is_running;               // 타이머 실행 상태

public:
    /**
     * EventTimer 생성자
     * @param base libevent의 event_base
     * @param base_owned event_base 소유권 (true면 소멸 시 해제)
     */
    EventTimer(struct event_base* base, bool base_owned = false);
    
    /**
     * EventTimer 소멸자
     */
    virtual ~EventTimer();
    
    /**
     * 일회성 타이머 시작
     * @param timeout 타이머 시간 (chrono duration 지원)
     * @return 성공 시 true, 실패 시 false
     */
    template<typename Duration>
    bool startOnce(const Duration& timeout);
    
    /**
     * 주기적 타이머 시작
     * @param interval 반복 간격 (chrono duration 지원)
     * @return 성공 시 true, 실패 시 false
     */
    template<typename Duration>
    bool startPeriodic(const Duration& interval);
    
    /**
     * 타이머 정지
     */
    void stop();
    
    /**
     * 타이머 실행 상태 확인
     * @return 실행 중이면 true, 정지 상태면 false
     */
    bool isRunning() const { return _is_running; }
    
    /**
     * 타이머가 주기적인지 확인
     * @return 주기적이면 true, 일회성이면 false
     */
    bool isPeriodic() const { return _is_periodic; }
    
    /**
     * 현재 설정된 간격 조회
     * @return 타이머 간격 (밀리초)
     */
    std::chrono::milliseconds getInterval() const { return _interval; }
    
private:
    /**
     * libevent 타이머 콜백 (내부용)
     * @param fd 사용하지 않음 (타이머는 fd 없음)
     * @param events 이벤트 타입
     * @param ctx EventTimer 인스턴스 포인터
     */
    static void static_timer_cb(evutil_socket_t fd, short events, void* ctx);
    
    /**
     * 타이머 이벤트 설정 (내부용)
     * @param timeout_ms 타이머 시간 (밀리초)
     * @param is_periodic 주기적 여부
     * @return 성공 시 true, 실패 시 false
     */
    bool setupTimer(long timeout_ms, bool is_periodic);
};

// 템플릿 구현 (헤더에 포함)
template<typename Duration>
bool EventTimer::startOnce(const Duration& timeout) {
    auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    return setupTimer(timeout_ms, false);
}

template<typename Duration>
bool EventTimer::startPeriodic(const Duration& interval) {
    auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(interval).count();
    return setupTimer(interval_ms, true);
}

#endif // EVENTTIMER_H