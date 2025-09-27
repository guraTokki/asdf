# ASDF (All Source Data Feed)

고성능 C++ 이벤트 기반 Pub/Sub 시스템

## 개요

ASDF는 시장 데이터 분배를 위한 고성능 Publisher-Subscriber 시스템입니다. gap-free recovery 메커니즘을 통해 안정적인 데이터 스트리밍을 제공하며, libevent 기반의 이벤트 드리븐 아키텍처를 사용합니다.

## 주요 특징

- **Gap-Free Recovery**: 클라이언트 연결 중단 시 누락된 메시지 없는 완전한 복구
- **고성능**: 이벤트 드리븐 아키텍처로 높은 처리량과 낮은 지연시간
- **다중 연결 지원**: Unix Domain Socket과 TCP 소켓 동시 지원
- **스레드 안전**: 복구 처리를 위한 별도 스레드 풀
- **확장성**: 플러그인 가능한 시퀀스 저장소와 메시지 DB

## 빠른 시작

### 빌드

```bash
# 의존성: libevent
sudo apt-get install libevent-dev

# 빌드
mkdir build && cd build
cmake ..
make
```

### 기본 사용법

**Publisher 시작:**
```bash
# Unix 소켓으로 시작 (기본)
./test_simple_publisher_v2 -u /tmp/pubsub.sock

# TCP 소켓으로 시작
./test_simple_publisher_v2 -t 127.0.0.1:9999

# 자동 메시지 발행 (1초 간격)
./test_simple_publisher_v2 --auto-publish 1000
```

**Subscriber 연결:**
```bash
# 다른 터미널에서
./test_subscriber
```

### C++ API 사용 예제

```cpp
#include <event2/event.h>
#include "pubsub/SimplePublisherV2.h"

int main() {
    // 이벤트 베이스 생성
    struct event_base* base = event_base_new();

    // Publisher 생성 및 설정
    SimplePublisherV2 publisher(base);
    publisher.set_publisher_id(1);
    publisher.set_publisher_name("MyPublisher");

    // Unix 소켓 서버 시작
    publisher.set_address(UNIX_SOCKET, "/tmp/pubsub.sock");
    if (!publisher.start()) {
        std::cerr << "Failed to start publisher" << std::endl;
        return 1;
    }

    // 메시지 발행
    std::string message = "Hello World";
    publisher.publish(TOPIC1, message.c_str(), message.length());

    // 이벤트 루프 실행
    event_base_dispatch(base);

    // 정리
    publisher.stop();
    event_base_free(base);
    return 0;
}
```

## 아키텍처

### 핵심 컴포넌트

- **SimplePublisherV2**: 메인 Publisher 구현체 (Gap-Free Recovery 지원)
- **SimpleSubscriber**: 구독자 클라이언트
- **MessageDB**: 메시지 영속성 저장소
- **SequenceStorage**: 시퀀스 번호 관리
- **RecoveryWorker**: 복구 처리 전용 스레드 풀

### Gap-Free Recovery 플로우

```
1. 클라이언트 Recovery 요청 → status = RECOVERING
2. Recovery 진행 중 → 새 메시지들 pending queue에 저장
3. Recovery 완료 → pending 메시지들 순서대로 전송
4. 클라이언트 → status = ONLINE (실시간 메시지 수신)
```

## 테스트

### 단위 테스트
```bash
# Publisher 테스트
./test_simple_publisher_v2

# Subscriber 테스트
./test_subscriber

# 통합 테스트
./test_pubsub_v2_integration
```

### 성능 테스트
```bash
# 고빈도 메시지 발행
./test_simple_publisher_v2 --auto-publish 10

# 다중 클라이언트 연결
for i in {1..10}; do ./test_subscriber & done
```

## 문서

- [아키텍처 상세](SimplePublisherV2_Architecture.md)
- [API 레퍼런스](SimplePublisherV2_API_Reference.md)
- [상태 다이어그램](SimplePublisherV2_State_Diagrams.md)
- [개발 가이드](CLAUDE.md)

## 기술 스택

- **언어**: C++17
- **네트워킹**: libevent
- **빌드**: CMake
- **플랫폼**: Linux

## 성능 특성

- **처리량**: 초당 수만 건의 메시지 처리
- **지연시간**: 실시간 메시지 < 1ms
- **복구 성능**: 초당 수천 건의 과거 메시지 전송
- **메모리**: 클라이언트당 ~1KB

## 라이센스

이 프로젝트는 개발 및 테스트 목적으로 제공됩니다.