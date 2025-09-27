# SimplePublisherV2 API 참조 문서

## 개요

SimplePublisherV2는 고성능 Pub/Sub 시스템을 위한 새로운 Publisher 구현체입니다. 이 문서는 API 사용법, 예제, 그리고 모범 사례를 제공합니다.

## 헤더 파일

```cpp
#include "pubsub/SimplePublisherV2.h"
```

## 클래스 정의

### SimplePublisherV2

```cpp
class SimplePublisherV2 {
public:
    // 생성자/소멸자
    SimplePublisherV2(struct event_base* shared_event_base);
    ~SimplePublisherV2();
    
    // 설정 메서드
    void set_address(SocketType socket_type, std::string address, int port=0);
    void set_sequence_storage(SequenceStorage* sequence_storage);
    
    // 서버 제어
    bool start();
    void stop();
    
    // 메시지 발행
    void publish(DataTopic topic, const char* data, size_t size);
    
    // 상태 조회
    PublisherStatus get_status() const;
    void set_status(PublisherStatus status);
    
private:
    // 내부 구현...
};
```

## 열거형

### PublisherStatus

```cpp
enum PublisherStatus {
    PUBLISHER_STARTING,    // 시작 중
    PUBLISHER_LISTENING,   // 클라이언트 연결 대기
    PUBLISHER_ONLINE,      // 정상 운영
    PUBLISHER_ERROR        // 오류 상태
};
```

### SocketType

```cpp
enum SocketType {
    UNIX_SOCKET,    // Unix Domain Socket
    TCP_SOCKET      // TCP Socket
};
```

### DataTopic

```cpp
enum DataTopic : uint32_t {
    TOPIC1 = 1,         // 토픽 1
    TOPIC2 = 2,         // 토픽 2
    MISC = 4,           // 기타 토픽
    ALL_TOPICS = 7      // 모든 토픽
};
```

## API 메서드 상세

### 생성자

```cpp
SimplePublisherV2(struct event_base* shared_event_base);
```

**매개변수:**
- `shared_event_base`: libevent의 메인 이벤트 루프

**설명:**
- SimplePublisherV2 인스턴스를 생성합니다.
- 내부적으로 Protocol, DB_SAM, PublisherSequenceRecord를 초기화합니다.

**예제:**
```cpp
struct event_base* base = event_base_new();
SimplePublisherV2 publisher(base);
```

### set_address()

```cpp
void set_address(SocketType socket_type, std::string address, int port=0);
```

**매개변수:**
- `socket_type`: 소켓 타입 (UNIX_SOCKET 또는 TCP_SOCKET)
- `address`: 주소 (Unix: 파일 경로, TCP: IP 주소)
- `port`: 포트 번호 (TCP 소켓일 때만 사용)

**설명:**
- Publisher가 사용할 네트워크 주소를 설정합니다.

**예제:**
```cpp
// Unix Domain Socket
publisher.set_address(UNIX_SOCKET, "/tmp/pubsub.sock");

// TCP Socket
publisher.set_address(TCP_SOCKET, "127.0.0.1", 8080);
```

### set_sequence_storage()

```cpp
void set_sequence_storage(SequenceStorage* sequence_storage);
```

**매개변수:**
- `sequence_storage`: 시퀀스 저장소 인스턴스

**설명:**
- 시퀀스 정보를 저장할 저장소를 설정합니다.

**예제:**
```cpp
FileSequenceStorage storage("./sequences");
publisher.set_sequence_storage(&storage);
```

### start()

```cpp
bool start();
```

**반환값:**
- `true`: 시작 성공
- `false`: 시작 실패

**설명:**
- Publisher 서버를 시작합니다.
- EventBase 설정, 복구 스레드 풀 시작, 소켓 리스닝을 수행합니다.

**예제:**
```cpp
if (!publisher.start()) {
    std::cerr << "Failed to start publisher" << std::endl;
    return -1;
}
```

### stop()

```cpp
void stop();
```

**설명:**
- Publisher 서버를 중지합니다.
- 모든 클라이언트 연결을 정리하고 복구 스레드를 종료합니다.

**예제:**
```cpp
publisher.stop();
```

### publish()

```cpp
void publish(DataTopic topic, const char* data, size_t size);
```

**매개변수:**
- `topic`: 메시지 토픽
- `data`: 메시지 데이터
- `size`: 데이터 크기

**설명:**
- 지정된 토픽으로 메시지를 발행합니다.
- 시퀀스 번호를 업데이트하고 DB에 저장한 후 모든 ONLINE 클라이언트에 전송합니다.

**예제:**
```cpp
std::string message = "Hello World";
publisher.publish(TOPIC1, message.c_str(), message.length());
```

### get_status()

```cpp
PublisherStatus get_status() const;
```

**반환값:**
- 현재 Publisher 상태

**예제:**
```cpp
if (publisher.get_status() == PUBLISHER_ONLINE) {
    std::cout << "Publisher is running normally" << std::endl;
}
```

## 사용 예제

### 기본 사용법

```cpp
#include <iostream>
#include <event2/event.h>
#include "pubsub/SimplePublisherV2.h"

int main() {
    // 이벤트 루프 생성
    struct event_base* base = event_base_new();
    
    // Publisher 생성 및 설정
    SimplePublisherV2 publisher(base);
    publisher.set_address(UNIX_SOCKET, "/tmp/pubsub.sock");
    
    // 서버 시작
    if (!publisher.start()) {
        std::cerr << "Failed to start publisher" << std::endl;
        return -1;
    }
    
    // 메시지 발행
    for (int i = 0; i < 10; ++i) {
        std::string message = "Message " + std::to_string(i);
        publisher.publish(TOPIC1, message.c_str(), message.length());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 정리
    publisher.stop();
    event_base_free(base);
    
    return 0;
}
```

### TCP 소켓 사용

```cpp
#include <iostream>
#include <event2/event.h>
#include "pubsub/SimplePublisherV2.h"

int main() {
    struct event_base* base = event_base_new();
    
    SimplePublisherV2 publisher(base);
    publisher.set_address(TCP_SOCKET, "0.0.0.0", 8080);
    
    if (!publisher.start()) {
        std::cerr << "Failed to start TCP publisher" << std::endl;
        return -1;
    }
    
    // 이벤트 루프 실행
    event_base_dispatch(base);
    
    publisher.stop();
    event_base_free(base);
    
    return 0;
}
```

### 시퀀스 저장소 사용

```cpp
#include <iostream>
#include <event2/event.h>
#include "pubsub/SimplePublisherV2.h"
#include "pubsub/FileSequenceStorage.h"

int main() {
    struct event_base* base = event_base_new();
    
    // 시퀀스 저장소 설정
    FileSequenceStorage storage("./publisher_sequences");
    storage.initialize();
    
    SimplePublisherV2 publisher(base);
    publisher.set_address(UNIX_SOCKET, "/tmp/pubsub.sock");
    publisher.set_sequence_storage(&storage);
    
    if (!publisher.start()) {
        std::cerr << "Failed to start publisher" << std::endl;
        return -1;
    }
    
    // 이벤트 루프 실행
    event_base_dispatch(base);
    
    publisher.stop();
    storage.cleanup();
    event_base_free(base);
    
    return 0;
}
```

### 다중 토픽 메시지 발행

```cpp
#include <iostream>
#include <event2/event.h>
#include "pubsub/SimplePublisherV2.h"

void publish_market_data(SimplePublisherV2& publisher) {
    // 체결 데이터 (TOPIC1)
    std::string trade_data = "AAPL,150.25,1000,2024-01-01T10:30:00";
    publisher.publish(TOPIC1, trade_data.c_str(), trade_data.length());
    
    // 호가 데이터 (TOPIC2)
    std::string quote_data = "AAPL,150.20,150.30,500,300";
    publisher.publish(TOPIC2, quote_data.c_str(), quote_data.length());
    
    // 기타 데이터 (MISC)
    std::string misc_data = "Market Status: OPEN";
    publisher.publish(MISC, misc_data.c_str(), misc_data.length());
}

int main() {
    struct event_base* base = event_base_new();
    
    SimplePublisherV2 publisher(base);
    publisher.set_address(UNIX_SOCKET, "/tmp/market_data.sock");
    
    if (!publisher.start()) {
        std::cerr << "Failed to start market data publisher" << std::endl;
        return -1;
    }
    
    // 주기적으로 시장 데이터 발행
    for (int i = 0; i < 100; ++i) {
        publish_market_data(publisher);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    publisher.stop();
    event_base_free(base);
    
    return 0;
}
```

## 모범 사례

### 1. 리소스 관리

```cpp
// 올바른 리소스 관리
struct event_base* base = event_base_new();
SimplePublisherV2 publisher(base);

// 사용 후 정리
publisher.stop();
event_base_free(base);
```

### 2. 에러 처리

```cpp
// 적절한 에러 처리
if (!publisher.start()) {
    std::cerr << "Failed to start publisher: " << strerror(errno) << std::endl;
    return -1;
}

// 상태 확인
if (publisher.get_status() != PUBLISHER_ONLINE) {
    std::cerr << "Publisher is not in online state" << std::endl;
}
```

### 3. 메시지 크기 제한

```cpp
// 메시지 크기 검증
const size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB

if (data_size > MAX_MESSAGE_SIZE) {
    std::cerr << "Message too large: " << data_size << " bytes" << std::endl;
    return;
}

publisher.publish(topic, data, data_size);
```

### 4. 스레드 안전성

```cpp
// 멀티스레드 환경에서의 안전한 사용
std::mutex publish_mutex;

void thread_safe_publish(SimplePublisherV2& publisher, 
                        DataTopic topic, 
                        const char* data, 
                        size_t size) {
    std::lock_guard<std::mutex> lock(publish_mutex);
    publisher.publish(topic, data, size);
}
```

## 성능 튜닝

### 1. 복구 스레드 수 조정

```cpp
// 복구 스레드 수는 기본적으로 3개
// 많은 클라이언트가 있는 경우 증가 고려
static constexpr int MAX_RECOVERY_THREADS = 3;
```

### 2. 메시지 배치 처리

```cpp
// 여러 메시지를 배치로 발행
void publish_batch(SimplePublisherV2& publisher, 
                  const std::vector<std::pair<DataTopic, std::string>>& messages) {
    for (const auto& [topic, data] : messages) {
        publisher.publish(topic, data.c_str(), data.length());
    }
}
```

### 3. 메모리 풀 사용

```cpp
// 메시지 버퍼 재사용을 위한 메모리 풀
class MessageBufferPool {
    std::queue<std::vector<char>> available_buffers;
    std::mutex pool_mutex;
    
public:
    std::vector<char> acquire() {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (available_buffers.empty()) {
            return std::vector<char>(1024);
        }
        auto buffer = std::move(available_buffers.front());
        available_buffers.pop();
        return buffer;
    }
    
    void release(std::vector<char> buffer) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        buffer.clear();
        available_buffers.push(std::move(buffer));
    }
};
```

## 문제 해결

### 일반적인 문제들

1. **서버 시작 실패**
   - 포트가 이미 사용 중인지 확인
   - 권한 문제 (Unix 소켓 파일 경로)
   - 이벤트 루프 초기화 확인

2. **메시지 전송 실패**
   - 클라이언트 연결 상태 확인
   - 네트워크 연결 상태 확인
   - 메시지 크기 제한 확인

3. **복구 처리 지연**
   - 복구 스레드 수 증가 고려
   - DB 성능 확인
   - 네트워크 대역폭 확인

### 디버깅 팁

```cpp
// 상세한 로그 출력
#define DEBUG_PUBLISHER

#ifdef DEBUG_PUBLISHER
    #define PUB_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
    #define PUB_DEBUG(msg)
#endif

// 사용 예제
PUB_DEBUG("Publishing message to topic " << topic);
publisher.publish(topic, data, size);
```

이 API 참조 문서는 SimplePublisherV2를 효과적으로 사용하는 데 필요한 모든 정보를 제공합니다.
