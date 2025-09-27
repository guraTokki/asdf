# T2MA (Trade & Market Access) System Architecture

## 📋 Overview

T2MA는 설정 기반의 금융 시장 데이터 처리 시스템으로, 플러그인 아키텍처를 통해 다양한 시장별 특화 모듈을 지원합니다. 이벤트 기반 아키텍처와 고성능 메시지 처리를 통해 실시간 금융 데이터 스트리밍을 제공합니다.

## 🏗️ Core Architecture

### 1. System Components Hierarchy

```
┌─────────────────────────────────────────────────────────┐
│                T2MA Application                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │            t2ma_with_system_config              │    │
│  │  • Signal Handling                             │    │
│  │  • Plugin Loading (Dynamic Library)           │    │
│  │  • Configuration Management                   │    │
│  │  • Event Loop Control                         │    │
│  └─────────────────────────────────────────────────┘    │
│                           │                             │
│  ┌─────────────────────────▼─────────────────────────┐    │
│  │                T2MASystem (Base)                 │    │
│  │  • Message Handler Registry                     │    │
│  │  • Scheduler Management                         │    │
│  │  • Publisher/Subscriber Management              │    │
│  │  • Master Data Management                       │    │
│  └─────────────────────────┬─────────────────────────┘    │
│                           │                             │
│  ┌─────────────────────────▼─────────────────────────┐    │
│  │            T2MA_JAPAN_EQUITY                     │    │
│  │  • Japan Market Specific Logic                  │    │
│  │  • Custom Message Handlers                      │    │
│  │  • Japan-specific Schedulers                    │    │
│  │  • Market Time Management                       │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

### 2. Configuration-Driven Architecture

T2MA는 YAML 설정 파일을 통해 완전히 설정 가능한 시스템입니다:

```yaml
# Core System Configuration
id: 1001
name: "T2MA_JAPAN_EQUITY"

# Plugin Configuration
plugin:
  module: "libT2MA.so"
  search_path: "./build"
  symbol: "create_t2ma_japan_equity"

# Message Handler Configuration
handlers:
  message_types:
    TREP_DATA:
      enabled: true
      symbol: "handle_trep_data_message"
    CONTROL:
      enabled: true
      symbol: "handle_control_message"

# Scheduler Configuration
schedulers:
  - name: "heartbeat_sender"
    enabled: true
    type: "interval"
    interval_sec: 5
    handler_symbol: "control_heartbeat_japan"
```

## 🔧 Core Components

### 1. T2MASystem (Base Class)

**Purpose**: 공통 시스템 기능을 제공하는 추상 기본 클래스

**Key Responsibilities**:
- **Message Handler Registry**: 동적 메시지 핸들러 등록 및 관리
- **Scheduler Management**: libevent 기반 스케줄러 시스템
- **Publisher/Subscriber**: SimplePubSub 기반 메시지 배포
- **Master Data Management**: HashMaster 기반 마스터 데이터 관리
- **Configuration Management**: YAML 기반 설정 로딩

**Key Methods**:
```cpp
class T2MASystem {
public:
    virtual bool initialize() = 0;
    virtual void regist_handlers() = 0;
    virtual void init_message_handlers() = 0;
    virtual void init_scheduler_handlers();

    // Scheduler Management
    void setup_schedulers();
    void cleanup_schedulers();

    // Message Processing
    void setup_message_handlers();
    void setup_command_handlers();
};
```

### 2. T2MA_JAPAN_EQUITY (Derived Class)

**Purpose**: 일본 주식 시장 전용 구현체

**Specializations**:
- **Japan Market Logic**: 일본 주식 시장 특화 로직
- **Custom Handlers**: 일본 시장 전용 메시지 핸들러
- **Market Time Management**: JST 시간대 기반 시장 상태 관리
- **Japan-specific Schedulers**: 일본 시장 전용 스케줄러

**Key Features**:
```cpp
class T2MA_JAPAN_EQUITY : public T2MASystem {
    // Japan-specific message handlers
    void handle_trep_data_message(const char* data, size_t size);
    void handle_control_message(const char* data, size_t size);
    void handle_japan_equity(const char* data, size_t size);

    // Japan-specific schedulers
    void control_heartbeat_japan();

    // Market configuration helpers
    std::string getJapanConfig(const std::string& key, const std::string& default_value = "");
    int getJapanConfigInt(const std::string& key, int default_value = 0);
};
```

### 3. Plugin Architecture

**Dynamic Loading**: 런타임에 시장별 모듈을 동적으로 로딩

```cpp
// Factory Function (exported from shared library)
extern "C" T2MASystem* create_t2ma_japan_equity(const T2MAConfig& config);

// Plugin Loading in main application
void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
auto factory = (T2MASystem*(*)(const T2MAConfig&))dlsym(handle, symbol_name.c_str());
auto system = std::unique_ptr<T2MASystem>(factory(config));
```

## 📊 Data Flow Architecture

### 1. Message Processing Flow

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Message Queue │────│   T2MASystem     │────│   Publisher     │
│   (/t2ma_mq)    │    │                  │    │   (SimplePubSub)│
└─────────────────┘    │ ┌──────────────┐ │    └─────────────────┘
                       │ │Message Type  │ │              │
┌─────────────────┐    │ │Dispatcher    │ │    ┌─────────────────┐
│   Control Input │────│ └──────────────┘ │────│   Subscribers   │
│   (Commands)    │    │                  │    │   (Market Data) │
└─────────────────┘    │ ┌──────────────┐ │    └─────────────────┘
                       │ │Handler       │ │
┌─────────────────┐    │ │Registry      │ │    ┌─────────────────┐
│   CSV Data      │────│ └──────────────┘ │────│   Master Data   │
│   (Master Data) │    │                  │    │   (HashMaster)  │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### 2. Handler Registration Flow

```cpp
// 1. Static Handler Registration (in regist_handlers)
REGISTER_MEMBER_HANDLER(handle_trep_data_message);
REGISTER_MEMBER_HANDLER(handle_control_message);

// 2. Dynamic Handler Mapping (from configuration)
for (const auto& handler_config : config.handlers_ext.message_types) {
    if (handler_config.enabled) {
        auto handler_it = handlers_.find(handler_config.symbol);
        msg_type_handlers_[msg_type] = handler_it->second;
    }
}

// 3. Scheduler Handler Registration
scheduler_handlers_["control_heartbeat_japan"] =
    [this]() { this->control_heartbeat_japan(); };
```

## ⏰ Scheduler System Architecture

### 1. Scheduler Types

**Interval Schedulers**: 주기적 실행
```yaml
- name: "heartbeat_sender"
  type: "interval"
  interval_sec: 5
  start_time: "immediate"
  end_time: "none"
```

**One-time Schedulers**: 특정 시간 실행
```yaml
- name: "daily_master_reload"
  type: "once"
  run_at: "09:00:00"
```

### 2. Event-Driven Scheduling

```cpp
// libevent 기반 스케줄러 구현
struct SchedulerData {
    T2MASystem* instance;
    T2MAConfig::SchedulerItem config;
    std::function<void()> handler;
    struct event* event_ptr;
};

// 스케줄러 콜백
void T2MASystem::scheduler_callback(evutil_socket_t fd, short what, void* arg) {
    SchedulerData* sched_data = static_cast<SchedulerData*>(arg);
    if (sched_data->instance->isWithinScheduleTime(sched_data->config)) {
        sched_data->handler();
    }
}
```

## 🔌 Pub/Sub Integration

### 1. Publisher Configuration

```cpp
// SimplePubSub Publisher 초기화
publisher_ = std::make_unique<SimplePublisherV2>(
    config_.pubsub.publisher.database_name,
    storage_strategy.get()
);

// Unix Socket + TCP 동시 지원
publisher_->start_publisher(
    config_.pubsub.publisher.unix_socket_path,
    config_.pubsub.publisher.tcp_host,
    config_.pubsub.publisher.tcp_port
);
```

### 2. Subscriber Management

```cpp
for (const auto& sub_config : config_.pubsub.subscribers) {
    if (sub_config.enabled) {
        auto subscriber = std::make_unique<SimpleSubscriber>(
            sub_config.client_id,
            sub_config.name,
            storage_strategy.get()
        );

        if (sub_config.type == "unix") {
            subscriber->connect_unix(sub_config.socket_path);
        } else if (sub_config.type == "tcp") {
            subscriber->connect_tcp(sub_config.host, sub_config.port);
        }

        subscribers_.push_back(std::move(subscriber));
    }
}
```

## 🏢 Master Data Management

### 1. HashMaster Integration

```cpp
// MasterManager 초기화
master_manager_ = std::make_unique<MasterManager>(LogLevel::INFO);
master_manager_->loadMasterConfigs(config_.files.master_file);
master_manager_->createMaster(config_.master);

// 활성 마스터 설정
active_master_ = master_manager_->getMaster(config_.master);
```

### 2. CSV Data Loading

```cpp
// Japan Equity CSV 데이터 로딩
if (config_.system.auto_load_csv) {
    std::cout << "CSV 파일에서 마스터 데이터 로딩: " << config_.files.csv_file << std::endl;
    loadCSVtoMaster(config_.files.csv_file);
}
```

## 🎯 Japan Equity Specific Features

### 1. Market Time Management

```cpp
void T2MA_JAPAN_EQUITY::control_heartbeat_japan() {
    auto tm_now = *std::localtime(&time_t_now);
    int current_time = tm_now.tm_hour * 100 + tm_now.tm_min;

    std::string market_status;
    if (current_time >= 830 && current_time < 900) {
        market_status = "🔵 PRE-MARKET (Orders accepted)";
    } else if (current_time >= 900 && current_time <= 1130) {
        market_status = "🟢 MORNING SESSION (Active Trading)";
    } else if (current_time > 1130 && current_time < 1230) {
        market_status = "🟡 LUNCH BREAK";
    } else if (current_time >= 1230 && current_time <= 1500) {
        market_status = "🟢 AFTERNOON SESSION (Active Trading)";
    } else if (current_time > 1500 && current_time <= 1700) {
        market_status = "🟠 AFTER HOURS (ToSTNeT Trading)";
    } else {
        market_status = "🔴 MARKET CLOSED";
    }
}
```

### 2. Japan-specific Configuration

```cpp
// Japan 시장 전용 설정 접근
std::string currency = getJapanConfig("japan_currency", "JPY");
int lot_size = getJapanConfigInt("japan_lot_size", 100);
int settlement_days = getJapanConfigInt("japan_settlement_days", 2);
```

## 🔄 Event Loop Architecture

### 1. Main Event Loop

```cpp
// 메인 이벤트 루프 (libevent 기반)
while (!g_shutdown_requested) {
    if (config.system.event_loop_mode == "EVLOOP_ONCE") {
        event_base_loop(event_base_, EVLOOP_ONCE);
    } else if (config.system.event_loop_mode == "EVLOOP_NONBLOCK") {
        event_base_loop(event_base_, EVLOOP_NONBLOCK);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
```

### 2. Signal Handling

```cpp
volatile sig_atomic_t g_shutdown_requested = 0;

void signal_handler(int sig) {
    g_shutdown_requested = 1;
}

// Signal 등록
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);
```

## 📈 Performance Characteristics

### 1. Memory Management
- **Zero-copy Message Processing**: 메시지 복사 최소화
- **Memory-mapped Files**: mmap 기반 데이터 저장
- **Object Pooling**: 메시지 객체 재사용

### 2. Concurrency Model
- **Single-threaded Event Loop**: libevent 기반 비동기 처리
- **Recovery Workers**: 별도 스레드에서 복구 작업 처리
- **Lock-free Publishing**: 무잠금 메시지 발행

### 3. Scalability Features
- **Dynamic Handler Loading**: 런타임 핸들러 등록
- **Configurable Threading**: 설정 기반 스레드 수 조정
- **Horizontal Scaling**: 다중 인스턴스 지원

## 🛠️ Build and Deployment

### 1. Build System

```bash
# CMake 기반 빌드
mkdir build && cd build
cmake ..
make

# T2MA 시스템 실행
./t2ma_with_system_config ./config/t2ma_japan_equity_config.yaml
```

### 2. Configuration Management

**Development**: Local configuration files
**Production**: External configuration management
**Testing**: Mock configurations for unit tests

## 🔍 Monitoring and Debugging

### 1. Built-in Monitoring

```cpp
// 통계 정보 수집
void T2MASystem::print_statistics() {
    std::cout << "📊 System Statistics:" << std::endl;
    std::cout << "   - Processed Messages: " << processed_count_ << std::endl;
    std::cout << "   - Master Updates: " << master_update_count_ << std::endl;
    std::cout << "   - Active Clients: " << publisher_->get_client_count() << std::endl;
}
```

### 2. Debug Features

- **Handler Debug**: 핸들러 등록 및 호출 추적
- **Scheduler Debug**: 스케줄러 실행 로깅
- **Configuration Debug**: 설정 로딩 과정 상세 로그

## 🚀 Extensibility

### 1. Adding New Markets

1. **새로운 클래스 생성**: `T2MA_[MARKET]_EQUITY`
2. **Factory Function 구현**: `create_t2ma_[market]_equity`
3. **Market-specific Handlers**: 시장별 메시지 핸들러
4. **Configuration**: YAML 설정 파일 생성

### 2. Custom Message Types

```cpp
// 새로운 메시지 타입 추가
enum class CustomMsgType : char {
    CUSTOM_DATA = 'C',
    SPECIAL_EVENT = 'S'
};

// 핸들러 등록
void regist_handlers() override {
    REGISTER_MEMBER_HANDLER(handle_custom_data);
    REGISTER_MEMBER_HANDLER(handle_special_event);
}
```

## 📝 Configuration Reference

### Complete Configuration Example

```yaml
# System Identity
id: 1001
name: "T2MA_JAPAN_EQUITY"

# Plugin Configuration
plugin:
  module: "libT2MA.so"
  search_path: "./build"
  symbol: "create_t2ma_japan_equity"

# Data Sources
files:
  spec_file: "./config/SPECs"
  master_file: "./config/MASTERs"
  csv_file: "./trep_data/O_JAPAN_EQUITY_M_20250813.csv"
  database_path: "./data/t2ma_japan_equity_master"

# Publisher Configuration
pubsub:
  publisher:
    database_name: "./data/t2ma_japan_equity_pubsub_db"
    unix_socket_path: "/tmp/t2ma_japan.sock"
    tcp_host: "127.0.0.1"
    tcp_port: 9998

# Message Handlers
handlers:
  message_types:
    TREP_DATA:
      enabled: true
      symbol: "handle_trep_data_message"
  control_commands:
    HEARTBEAT:
      enabled: true
      symbol: "control_heartbeat"

# Schedulers
schedulers:
  - name: "heartbeat_sender"
    enabled: true
    type: "interval"
    interval_sec: 5
    handler_symbol: "control_heartbeat_japan"
```

---

## 📚 Related Documentation

- [ASDF Pub/Sub Architecture](./SimplePublisherV2_Architecture.md)
- [HashMaster Documentation](./HashMaster/HashMaster.md)
- [SimplePubSub API Reference](./SimplePublisherV2_API_Reference.md)

---

*Generated with [Claude Code](https://claude.ai/code)*

*Co-Authored-By: Claude <noreply@anthropic.com>*