# SimplePublisherV2 상태 전이도 및 데이터 흐름

## 1. Publisher 상태 전이도

```mermaid
stateDiagram-v2
    [*] --> PUBLISHER_STARTING : 생성
    PUBLISHER_STARTING --> PUBLISHER_LISTENING : start() 성공
    PUBLISHER_LISTENING --> PUBLISHER_ONLINE : 첫 클라이언트 연결
    PUBLISHER_ONLINE --> PUBLISHER_ONLINE : 정상 운영
    PUBLISHER_LISTENING --> PUBLISHER_ERROR : start() 실패
    PUBLISHER_ONLINE --> PUBLISHER_ERROR : 심각한 오류
    PUBLISHER_ERROR --> [*] : stop()
```

## 2. 클라이언트 상태 전이도

```mermaid
stateDiagram-v2
    [*] --> CLIENT_CONNECTED : 연결 수립
    CLIENT_CONNECTED --> CLIENT_ONLINE : 구독 승인
    CLIENT_ONLINE --> CLIENT_RECOVERING : 복구 요청
    CLIENT_RECOVERING --> CLIENT_ONLINE : 복구 완료
    CLIENT_CONNECTED --> CLIENT_OFFLINE : 연결 끊김
    CLIENT_ONLINE --> CLIENT_OFFLINE : 연결 끊김
    CLIENT_RECOVERING --> CLIENT_OFFLINE : 연결 끊김
    CLIENT_OFFLINE --> [*] : 정리
```

## 3. 클라이언트 스레드 이동 다이어그램

```mermaid
graph TD
    A[메인 스레드<br/>ONLINE 클라이언트] --> B[복구 요청 수신]
    B --> C[클라이언트를 복구 스레드로 이동]
    C --> D[복구 스레드<br/>RECOVERING 클라이언트]
    D --> E[DB에서 메시지 조회]
    E --> F[순차적으로 메시지 전송]
    F --> G[복구 완료]
    G --> H[클라이언트를 메인 스레드로 이동]
    H --> A
    
    style A fill:#e1f5fe
    style D fill:#fff3e0
    style C fill:#f3e5f5
    style H fill:#f3e5f5
```

## 4. 메시지 발행 데이터 흐름

```mermaid
sequenceDiagram
    participant App as Application
    participant Pub as SimplePublisherV2
    participant DB as DB_SAM
    participant Seq as SequenceStorage
    participant Clients as Online Clients
    
    App->>Pub: publish(topic, data, size)
    Pub->>Pub: global_seq++, topic_seq++
    Pub->>Pub: TopicMessage 생성
    Pub->>DB: 메시지 저장
    DB-->>Pub: 저장 완료
    Pub->>Seq: 시퀀스 저장
    Seq-->>Pub: 저장 완료
    loop 각 ONLINE 클라이언트
        Pub->>Clients: 메시지 전송
    end
    Pub-->>App: 발행 완료
```

## 5. 복구 요청 처리 데이터 흐름

```mermaid
sequenceDiagram
    participant Client as Subscriber
    participant Main as Main Thread
    participant Queue as Recovery Queue
    participant Worker as Recovery Worker
    participant Recovery as Recovery Thread
    participant DB as DB_SAM
    
    Client->>Main: RecoveryRequest
    Main->>Main: move_client_to_recovery()
    Main->>Queue: RecoveryTask 추가
    Queue->>Worker: 작업 할당
    Worker->>Recovery: 클라이언트별 스레드 생성
    Recovery->>Client: RecoveryResponse
    loop 각 시퀀스 번호
        Recovery->>DB: 메시지 조회
        DB-->>Recovery: 메시지 데이터
        Recovery->>Client: 메시지 전송
    end
    Recovery->>Client: RecoveryComplete
    Recovery->>Main: move_client_to_main()
```

## 6. 전체 시스템 아키텍처

```mermaid
graph TB
    subgraph "SimplePublisherV2"
        subgraph "Main Thread"
            MT[EventBase]
            MC[Main Clients<br/>ONLINE]
            MP[Message Publishing]
        end
        
        subgraph "Recovery Thread Pool"
            RT1[Recovery Worker 1]
            RT2[Recovery Worker 2]
            RT3[Recovery Worker 3]
            RQ[Recovery Queue]
        end
        
        subgraph "Recovery Threads"
            RC1[Client 1 Recovery]
            RC2[Client 2 Recovery]
            RC3[Client 3 Recovery]
        end
        
        subgraph "Data Storage"
            DB[DB_SAM<br/>Message Storage]
            SS[SequenceStorage<br/>Sequence Info]
        end
    end
    
    subgraph "External"
        S1[Subscriber 1]
        S2[Subscriber 2]
        S3[Subscriber 3]
    end
    
    S1 --> MT
    S2 --> MT
    S3 --> MT
    
    MT --> MC
    MC --> MP
    MP --> DB
    MP --> SS
    
    MT --> RQ
    RQ --> RT1
    RQ --> RT2
    RQ --> RT3
    
    RT1 --> RC1
    RT2 --> RC2
    RT3 --> RC3
    
    RC1 --> DB
    RC2 --> DB
    RC3 --> DB
    
    RC1 --> S1
    RC2 --> S2
    RC3 --> S3
    
    style MT fill:#e3f2fd
    style MC fill:#e8f5e8
    style RQ fill:#fff3e0
    style DB fill:#fce4ec
    style SS fill:#f3e5f5
```

## 7. 스레드 간 통신

```mermaid
graph LR
    subgraph "Main Thread"
        A[Client Management]
        B[Message Publishing]
        C[Event Handling]
    end
    
    subgraph "Recovery Thread Pool"
        D[Task Queue]
        E[Worker Threads]
    end
    
    subgraph "Recovery Threads"
        F[Client Recovery]
        G[Message Retrieval]
        H[Message Sending]
    end
    
    A -->|move_client_to_recovery| D
    D -->|RecoveryTask| E
    E -->|create_thread| F
    F -->|DB query| G
    G -->|send_message| H
    H -->|move_client_to_main| A
    
    style A fill:#e1f5fe
    style D fill:#fff3e0
    style F fill:#f3e5f5
```

## 8. 메모리 레이아웃

```mermaid
graph TD
    subgraph "SimplePublisherV2 Memory Layout"
        subgraph "Main Thread Data"
            A[_main_thread_clients<br/>std::map&lt;uint32_t, ClientInfo&gt;]
            B[_main_clients_mutex<br/>std::mutex]
        end
        
        subgraph "Recovery Thread Data"
            C[_recovery_clients<br/>std::map&lt;uint32_t, unique_ptr&lt;ClientInfo&gt;&gt;]
            D[_client_threads<br/>std::map&lt;uint32_t, ClientThreadInfo&gt;]
            E[_recovery_clients_mutex<br/>std::mutex]
        end
        
        subgraph "Shared Data"
            F[_recovery_queue<br/>std::queue&lt;RecoveryTask&gt;]
            G[_recovery_queue_mutex<br/>std::mutex]
            H[_recovery_queue_cv<br/>std::condition_variable]
        end
        
        subgraph "Atomic Variables"
            I[_recovery_threads_running<br/>std::atomic&lt;bool&gt;]
            J[_shutdown<br/>std::atomic&lt;bool&gt;]
        end
    end
    
    style A fill:#e8f5e8
    style C fill:#fff3e0
    style F fill:#e1f5fe
    style I fill:#fce4ec
```

## 9. 성능 메트릭

### 처리량 (Throughput)
- **메인 스레드**: 실시간 메시지 발행 (초당 수천 건)
- **복구 스레드**: 과거 메시지 전송 (초당 수백 건)

### 지연시간 (Latency)
- **실시간 메시지**: < 1ms (메인 스레드)
- **복구 메시지**: 1-10ms (복구 스레드)

### 메모리 사용량
- **클라이언트당**: ~1KB (ClientInfo)
- **복구 스레드당**: ~8MB (스택 + 힙)
- **메시지 큐**: 동적 (복구 요청 수에 따라)

## 10. 장애 처리 시나리오

```mermaid
graph TD
    A[정상 운영] --> B{장애 발생}
    B -->|네트워크 오류| C[클라이언트 연결 끊김]
    B -->|DB 오류| D[메시지 저장 실패]
    B -->|메모리 부족| E[복구 스레드 생성 실패]
    
    C --> F[클라이언트 정리]
    D --> G[경고 로그 + 계속 진행]
    E --> H[복구 요청 거부]
    
    F --> I[정상 복구]
    G --> I
    H --> I
    
    style A fill:#e8f5e8
    style B fill:#fff3e0
    style C fill:#ffebee
    style D fill:#ffebee
    style E fill:#ffebee
    style I fill:#e8f5e8
```

이 다이어그램들은 SimplePublisherV2의 복잡한 상태 전이와 데이터 흐름을 시각적으로 보여주며, 시스템의 동작 방식을 이해하는 데 도움이 됩니다.
