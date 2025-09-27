#include "EventTimer.h"
#include <iostream>
#include <sys/time.h>

EventTimer::EventTimer(struct event_base* base, bool base_owned) 
    : EventBase(base, base_owned), _timer_event(nullptr), _is_periodic(false), 
      _interval(0), _is_running(false) {
}

EventTimer::~EventTimer() {
    stop();  // 타이머 정지 및 정리
}

void EventTimer::stop() {
    if (_timer_event) {
        // 이벤트 삭제 및 해제
        event_del(_timer_event);
        event_free(_timer_event);
        _timer_event = nullptr;
    }
    _is_running = false;
}

bool EventTimer::setupTimer(long timeout_ms, bool is_periodic) {
    // 기존 타이머가 있다면 정지
    stop();
    
    // 새로운 타이머 이벤트 생성
    _timer_event = event_new(getBase(), -1, 0, static_timer_cb, this);
    if (!_timer_event) {
        std::cerr << "EventTimer: Failed to create timer event" << std::endl;
        return false;
    }
    
    // 타이머 설정 저장
    _is_periodic = is_periodic;
    _interval = std::chrono::milliseconds(timeout_ms);
    
    // timeval 구조체로 변환
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    // 타이머 이벤트 추가
    if (event_add(_timer_event, &tv) != 0) {
        std::cerr << "EventTimer: Failed to add timer event" << std::endl;
        event_free(_timer_event);
        _timer_event = nullptr;
        return false;
    }
    
    _is_running = true;
    
    std::cout << "EventTimer: " << (is_periodic ? "Periodic" : "One-shot") 
              << " timer started with " << timeout_ms << "ms timeout" << std::endl;
    
    return true;
}

void EventTimer::static_timer_cb(evutil_socket_t fd, short events, void* ctx) {
    (void)fd; (void)events;  // 미사용 매개변수 경고 제거
    
    EventTimer* self = static_cast<EventTimer*>(ctx);
    
    std::cout << "EventTimer: Timer fired!" << std::endl;
    
    // 사용자 콜백 호출 (timeout 콜백 사용)
    self->call_timeout_callback();
    
    // 주기적 타이머인 경우 다시 설정
    if (self->_is_periodic && self->_is_running) {
        // 동일한 간격으로 다시 스케줄링
        struct timeval tv;
        auto interval_ms = self->_interval.count();
        tv.tv_sec = interval_ms / 1000;
        tv.tv_usec = (interval_ms % 1000) * 1000;
        
        if (event_add(self->_timer_event, &tv) != 0) {
            std::cerr << "EventTimer: Failed to reschedule periodic timer" << std::endl;
            self->_is_running = false;
        }
    } else {
        // 일회성 타이머는 실행 완료
        self->_is_running = false;
    }
}