# SimplePublisherV2 아키텍처 문서

## 개요

SimplePublisherV2는 SimpleSubscriber와 대칭적인 구조로 설계된 새로운 Publisher 구현체입니다. EventBase를 기반으로 한 이벤트 드리븐 아키텍처를 사용하며, 복구 처리를 위한 클라이언트 스레드 이동 방식을 도입했습니다.

## 핵심 설계 원칙

1. **대칭성**: SimpleSubscriber와 동일한 구조와 패턴 사용
2. **이벤트 드리븐**: EventBase를 통한 비동기 이벤트 처리
3. **스레드 분리**: 메인 스레드와 복구 스레드의 명확한 역할 분담
4. **클라이언트 이동**: 복구 시 클라이언트를 별도 스레드로 이동 후 복원

## 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────────────┐
│                    SimplePublisherV2                        │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   Main Thread   │  │ Recovery Thread │  │  Data Store  │ │
│  │                 │  │                 │  │              │ │
│  │ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌──────────┐ │ │
│  │ │EventBase    │ │  │ │Recovery     │ │  │ │DB_SAM    │ │ │
│  │ │- Accept     │ │  │ │Worker       │ │  │ │- Message │ │ │
│  │ │- Read       │ │  │ │- Task Queue │ │  │ │  Storage │ │ │
│  │ │- Write      │ │  │ │- Client     │ │  │ │          │ │ │
│  │ │             │ │  │ │  Recovery   │ │  │ │          │ │ │
│  │ └─────────────┘ │  │ └─────────────┘ │  │ └──────────┘ │ │
│  │                 │  │                 │  │              │ │
│  │ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌──────────┐ │ │
│  │ │Main Clients │ │  │ │Recovery     │ │  │ │Sequence  │ │ │
│  │ │- ONLINE     │ │  │ │Clients      │ │  │ │Storage   │ │ │
│  │ │- CONNECTED  │ │  │ │- RECOVERING │ │  │ │- Topic   │ │ │
│  │ │             │ │  │ │             │ │  │ │  Sequence│ │ │
│  │ └─────────────┘ │  │ └─────────────┘ │  │ └──────────┘ │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## 클래스 구조

### SimplePublisherV2 클래스

```cpp
class SimplePublisherV2 {
private:
    // 기본 정보
    uint32_t _publisher_id;
    std::string _publisher_name;

    // 네트워크 설정
    std::string _unix_path;
    std::string _tcp_address;
    uint16_t _tcp_port;
    bool _use_unix;

    // EventBase 및 리스너 관련
    struct event_base* _main_base;
    evconnlistener* _listener;

    // 클라이언트 관리 (통합된 맵 - Gap-Free Recovery 지원)
    std::mutex _clients_mu;
    std::map<uint32_t, std::shared_ptr<ClientInfo>> _clients;

    // Recovery Worker 스레드 풀
    std::vector<RecoveryWorker*> _workers;
    std::atomic<uint32_t> _rr_counter{0};  // Round-robin counter

    // Main thread 복귀 처리
    int _main_notify_pipe[2];
    event *_main_notify_event{nullptr};
    std::mutex _main_return_mu;
    std::queue<std::shared_ptr<ClientInfo>> _main_return_q;

    // 시퀀스 관리
    PublisherSequenceRecord* _publisher_sequence_record;
    SequenceStorage* _sequence_storage;

    // 데이터 저장
    MessageDB _db;

public:
    // 설정 메소드
    void set_address(SocketType socket_type, std::string address, int port=0);
    void set_publisher_id(uint32_t id) { _publisher_id = id; }
    void set_publisher_name(const std::string &name) { _publisher_name = name; }

    // 서버 시작/종료
    bool start(size_t recovery_thread_count = 2);
    void stop();

    // 메시지 발행 (Gap-Free Recovery 지원)
    void publish(DataTopic topic, const char* data, size_t size);

    // 메시지 처리 핸들러
    void handle_subscription_request(std::shared_ptr<ClientInfo> ci, const SubscriptionRequest* request);
    void handle_recovery_request(std::shared_ptr<ClientInfo> ci, const RecoveryRequest* request);

    // 복구 스레드 복귀 처리
    void enqueue_return_client(std::shared_ptr<ClientInfo> ci);
};
```

## 상태 전이도

### Publisher 상태

```
PUBLISHER_STARTING → PUBLISHER_LISTENING → PUBLISHER_ONLINE
        ↓                    ↓                    ↓
   PUBLISHER_ERROR ←─── PUBLISHER_ERROR ←─── PUBLISHER_ERROR
```

### 클라이언트 상태 전이

```
CONNECTED → ONLINE → RECOVERING → ONLINE
    ↓         ↓         ↓
OFFLINE ←─── OFFLINE ←─── OFFLINE
```

### 클라이언트 스레드 이동

```
Main Thread (ONLINE) ←→ Recovery Thread (RECOVERING)
        ↓                        ↓
   Normal Messages          Recovery Messages
   Real-time Publishing     Historical Data
```

## 데이터 흐름

### 1. 서버 시작 흐름

```
1. SimplePublisherV2 생성
   ↓
2. EventBase 초기화
   ↓
3. Protocol 설정 (PubSubTopicProtocol)
   ↓
4. DB_SAM 초기화 및 열기
   ↓
5. 복구 스레드 풀 시작 (3개 스레드)
   ↓
6. Socket Listen 시작
   ↓
7. PUBLISHER_LISTENING 상태
```

### 2. 클라이언트 연결 흐름

```
1. 클라이언트 연결 요청
   ↓
2. handle_accept() 호출
   ↓
3. ClientInfo 생성
   ↓
4. _main_thread_clients에 추가
   ↓
5. CLIENT_CONNECTED 상태
```

### 3. 구독 요청 처리 흐름

```
1. SubscriptionRequest 수신
   ↓
2. handle_subscription_request() 호출
   ↓
3. 클라이언트 정보 업데이트
   - topic_mask 설정
   - status = CLIENT_ONLINE
   ↓
4. SubscriptionResponse 전송
   ↓
5. 클라이언트가 실시간 메시지 수신 가능
```

### 4. Gap-Free Recovery 메시지 발행 흐름

```
1. publish() 호출
   ↓
2. 시퀀스 번호 업데이트
   - global_seq++
   - topic_seq++
   ↓
3. TopicMessage 생성
   ↓
4. MessageDB에 메시지 저장
   ↓
5. SequenceStorage에 시퀀스 저장
   ↓
6. 클라이언트 상태별 처리:
   - ONLINE 클라이언트 → 즉시 전송
   - RECOVERING 클라이언트 → pending_messages 큐에 저장
   ↓
7. Gap-Free Recovery 보장:
   - Recovery 완료 후 pending 메시지들이 순서대로 전송됨
```

### 5. Gap-Free Recovery 요청 흐름

```
1. RecoveryRequest 수신
   ↓
2. handle_recovery_request() 호출
   ↓
3. 현재 global_sequence 캡처 (target_seq)
   ↓
4. RecoveryResponse 전송 (start_seq ~ target_seq)
   ↓
5. 클라이언트 status = RECOVERING
   ↓
6. Recovery worker에게 작업 전달
   ↓
7. Recovery 진행 중:
   - 새로운 publish() → pending_messages에 저장
   - Worker가 DB에서 과거 데이터 전송
   ↓
8. Recovery 완료:
   - RecoveryComplete 전송
   - pending_messages 순서대로 flush
   - 클라이언트 status = ONLINE
```

### 5. 복구 요청 처리 흐름

```
1. RecoveryRequest 수신
   ↓
2. handle_recovery_request() 호출
   ↓
3. 클라이언트를 메인 스레드에서 복구 스레드로 이동
   - move_client_to_recovery()
   - status = CLIENT_RECOVERING
   ↓
4. RecoveryTask 생성 및 큐에 추가
   ↓
5. 복구 워커 스레드가 작업 처리
   ↓
6. process_client_recovery() 실행
   - DB에서 메시지 조회
   - 순차적으로 클라이언트에 전송
   ↓
7. 복구 완료 후 클라이언트를 메인 스레드로 이동
   - move_client_to_main()
   - status = CLIENT_ONLINE
```

## 스레드 모델

### 메인 스레드
- **역할**: 클라이언트 연결, 구독 요청, 실시간 메시지 발행
- **처리 대상**: ONLINE 상태의 클라이언트들
- **이벤트**: Accept, Read, Write, Disconnect, Error

### 복구 스레드 풀 (3개 스레드)
- **역할**: 복구 요청 처리, 과거 메시지 전송
- **처리 대상**: RECOVERING 상태의 클라이언트들
- **작업 큐**: RecoveryTask 큐를 통한 작업 분배

### 클라이언트별 복구 스레드
- **역할**: 개별 클라이언트의 복구 처리
- **생명주기**: 복구 시작 → 메시지 전송 → 복구 완료 → 종료

## 메시지 프로토콜

### 수신 메시지
- `MAGIC_SUBSCRIBE`: 구독 요청
- `MAGIC_RECOVERY_REQ`: 복구 요청

### 송신 메시지
- `MAGIC_SUB_OK`: 구독 응답
- `MAGIC_TOPIC_MSG`: 토픽 메시지
- `MAGIC_RECOVERY_RES`: 복구 응답
- `MAGIC_RECOVERY_CMP`: 복구 완료

## 동기화 및 스레드 안전성

### 뮤텍스 보호 영역
- `_main_clients_mutex`: 메인 스레드 클라이언트 맵
- `_recovery_clients_mutex`: 복구 스레드 클라이언트 맵
- `_recovery_queue_mutex`: 복구 작업 큐

### 원자적 변수
- `_recovery_threads_running`: 복구 스레드 실행 상태
- `_shutdown`: 전체 종료 플래그
- `thread_running`: 개별 클라이언트 스레드 상태

## 성능 특성

### 장점
1. **메인 스레드 성능 유지**: 복구 처리가 메인 스레드를 방해하지 않음
2. **독립적인 복구**: 각 클라이언트의 복구가 서로 영향을 주지 않음
3. **스케일러블**: 복구 스레드 풀로 동시 복구 처리 가능
4. **메모리 효율성**: 클라이언트 이동으로 중복 저장 방지

### 고려사항
1. **복잡성**: 클라이언트 이동으로 인한 복잡성 증가
2. **리소스 관리**: 복구 스레드의 생성/소멸 관리 필요
3. **동기화 오버헤드**: 뮤텍스와 원자적 변수 사용

## 확장 가능성

### 향후 개선 사항
1. **TCP 소켓 지원**: 현재 Unix Domain Socket 중심
2. **로드 밸런싱**: 복구 스레드 간 작업 분배 최적화
3. **모니터링**: 클라이언트 상태 및 성능 메트릭
4. **설정 가능성**: 스레드 수, 큐 크기 등 동적 조정

### 플러그인 아키텍처
- Protocol 인터페이스를 통한 다양한 프로토콜 지원
- SequenceStorage 인터페이스를 통한 다양한 저장소 지원
- EventBase 팩토리 패턴을 통한 다양한 소켓 타입 지원

## 결론

SimplePublisherV2는 기존 SimplePublisher의 복잡성을 줄이면서도 복구 기능을 별도 스레드에서 효율적으로 처리할 수 있는 새로운 아키텍처를 제공합니다. 클라이언트 이동 방식을 통해 메인 스레드의 성능을 유지하면서도 복구 기능의 독립성을 보장하는 것이 핵심 특징입니다.
