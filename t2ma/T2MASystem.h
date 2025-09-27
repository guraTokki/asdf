#ifndef T2MASYSTEM_H
#define T2MASYSTEM_H

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <mqueue.h>
#include <event2/event.h>

// Include components
#include "../common/MQReader.h"
#include "../pubsub/Common.h"
#include "../common/IPCHeader.h"
#include "../pubsub/SimplePublisherV2.h"
#include "../pubsub/SimpleSubscriber.h"
#include "../HashMaster/HashMaster.h"
#include "../HashMaster/BinaryRecord.h"
#include "../HashMaster/MasterManager.h"
#include "T2MAConfig.h"
#include "../pubsub/FileSequenceStorage.h"
#include "../pubsub/HashmasterSequenceStorage.h"
#include "../pubsub/SequenceStorage.h"

using namespace SimplePubSub;

#define REGISTER_MEMBER_HANDLER(fn) handlers_[#fn] = [this](const char* data, size_t size) { fn(data, size); };

// Forward declaration
class T2MASystem;

// Forward declaration for scheduler data
struct SchedulerData {
    T2MASystem* instance;
    T2MAConfig::SchedulerItem config;
    std::function<void()> handler;
    struct event* event_ptr;  // Store event pointer for rescheduling
};

/* get system date time YYYYMMDDhhmmss */
inline std::string getDateTime() {
    time_t  t_t;
    struct tm   t_tm;

    time(&t_t);
    localtime_r(&t_t, &t_tm);
    char datetime[14+1];
    sprintf(datetime, "%04d%02d%02d%02d%02d%02d", 
        t_tm.tm_year + 1900,
        t_tm.tm_mon+1,
        t_tm.tm_mday,
        t_tm.tm_hour,
        t_tm.tm_min,
        t_tm.tm_sec
    );
    return std::string(datetime);
}

inline std::string cvt_gmt2local_ymd2(char *ymd, char *hms, char *hms1, int gmt_second) {
    struct tm stime = {0};  // Initialize to zero
    struct tm t_tm;
    char time_temp[8+1];
    time_t user_time;
    
    int year;
    int mon, day;
    {
        char tmp[8+1];
        memcpy(tmp, ymd, 8);
        tmp[8] = 0x00;
        int iymd = atoi(tmp);
        year = iymd / 10000;
        mon = (iymd%10000)/100;
        day = iymd%100;

        stime.tm_year = year - 1900;
        stime.tm_mon = mon - 1;
        stime.tm_mday = day;

        int ihms;
        memcpy(tmp, hms, 6);
        tmp[6] = 0x00;
        ihms = atoi(tmp);
        
        stime.tm_hour = ihms/10000;
        stime.tm_min = (ihms%10000)/100;
        stime.tm_sec = ihms%100;
        stime.tm_isdst = -1;  // Let mktime() determine DST
    }

    user_time = mktime(&stime);
    user_time += gmt_second;
    localtime_r(&user_time, &t_tm);

    sprintf(time_temp, "%04d%02d%02d", t_tm.tm_year+1900, t_tm.tm_mon+1, t_tm.tm_mday);
    return std::string(time_temp);
}

inline std::string set_time(int hms, int gmt_second) {
    struct tm stime = {0};  // Initialize to zero
    struct tm t_tm;
    char time_temp[6+1];

    // Set a reasonable base date (today's date would be better, but using epoch)
    stime.tm_year = 70;  // 1970
    stime.tm_mon = 0;    // January
    stime.tm_mday = 1;   // 1st
    stime.tm_hour = hms/10000;
    stime.tm_min = (hms%10000)/100;
    stime.tm_sec = hms%100;
    stime.tm_isdst = -1;  // Let mktime() determine DST

    time_t user_time = mktime(&stime);
    user_time += gmt_second;
    localtime_r(&user_time, &t_tm);

    sprintf(time_temp, "%02d%02d%02d", t_tm.tm_hour, t_tm.tm_min, t_tm.tm_sec);
    return std::string(time_temp);
}

// CSV 파싱 유틸리티
class CsvParser {
public:
    static std::vector<std::string> parseLine(const std::string& line) {
        std::vector<std::string> result;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ',')) {
            result.push_back(field);
        }
        return result;
    }
};

// TREP 데이터 파싱 유틸리티
class TrepParser {
public:
    static std::map<std::string, std::string> parseLine(const std::string& line) {
        std::map<std::string, std::string> result;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ',')) {
            size_t eqPos = field.find('=');
            if (eqPos != std::string::npos) {
                std::string fid = field.substr(0, eqPos);
                std::string value = field.substr(eqPos + 1);
                
                // 따옴표 제거
                if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.length() - 2);
                }
                
                result[fid] = value;
            }
        }
        return result;
    }
};


// Config 기반 T2MA 메인 시스템 클래스
class T2MASystem {
protected:
    // libevent 이벤트 루프 (상속 클래스에서 접근 가능)
    struct event_base* event_base_;
    bool running_;
    
    // 컴포넌트들 (상속 클래스에서 접근 가능)
    std::unique_ptr<MQReader> mq_reader_;
    std::unique_ptr<SimplePublisherV2> publisher_;
    std::vector<std::unique_ptr<SimpleSubscriber>> subscribers_;
    std::unique_ptr<MasterManager> master_manager_;
    Master* active_master_;  // 현재 사용 중인 Master 인스턴스
    
    // 레이아웃 (스펙 파일에서 로드)
    std::shared_ptr<RecordLayout> masterLayout_;
    std::shared_ptr<RecordLayout> siseLayout_;
    std::shared_ptr<RecordLayout> hogaLayout_;
    
    // 통계 (상속 클래스에서 접근 가능)
    int processed_count_;
    int master_update_count_;
    int sise_count_;
    int hoga_count_;
    
    // Configuration (상속 클래스에서 접근 가능)
    T2MAConfig config_;
    
    // 메시지 처리 메소드 테이블
    using MessageHandler = std::function<void(const char*, size_t)>;
    std::unordered_map<std::string, MessageHandler> handlers_;              // 사용가능한 모든 핸들러
    std::unordered_map<char, MessageHandler> msg_type_handlers_;     // _msg_type별 핸들러
    std::unordered_map<std::string, MessageHandler> string_handlers_; // string별 핸들러

    // Scheduler management (moved from T2MA_JAPAN_EQUITY)
    std::vector<SchedulerData*> scheduled_data_;
    std::map<std::string, std::function<void()>> scheduler_handlers_;
    
public:
    T2MASystem(const T2MAConfig& config) :
        event_base_(nullptr), running_(false), config_(config),
        active_master_(nullptr), processed_count_(0), master_update_count_(0), sise_count_(0), hoga_count_(0) {
    }
    
    virtual ~T2MASystem() {
        cleanup();
    }

    virtual void regist_handlers() {}
    
    virtual bool initialize() {
        // libevent 초기화
        event_base_ = event_base_new();
        if (!event_base_) {
            std::cerr << "Failed to create event base" << std::endl;
            return false;
        }
        
        // 스펙 파일 로드
        if (!init_layouts()) {
            std::cerr << "Failed to initialize layouts" << std::endl;
            return false;
        }
        
        // HashMaster 기반 마스터 관리자 초기화
        if (!init_master_manager()) {
            std::cerr << "Failed to initialize Master Manager" << std::endl;
            return false;
        }
        
        // Publisher 초기화
        if (!init_publisher()) {
            std::cerr << "Failed to initialize Publisher" << std::endl;
            return false;
        }
        
        // MQ Reader 초기화
        if (!init_mq_reader()) {
            std::cerr << "Failed to initialize MQ Reader" << std::endl;
            return false;
        }
        
        // Subscribers 초기화
        if (!init_subscribers()) {
            std::cerr << "Failed to initialize Subscribers" << std::endl;
            return false;
        }

        // 메시지 핸들러 테이블 초기화
        init_message_handlers();

        // message type 별 handler, command 별 handler 설정
        setup_message_handlers();
        setup_command_handlers();

        // Initialize scheduler handlers
        init_scheduler_handlers();

        // Setup schedulers after event_base is ready
        setup_schedulers();

        std::cout << "T2MA System initialized successfully" << std::endl;
        return true;
    }
    
    bool init_layouts() {
        // 스펙 파일 파서 초기화
        SpecFileParser parser;
        // Try YAML directory first, then fallback to TSV file
        bool loaded = false;
        if (!config_.files.spec_file.empty() && config_.files.spec_file.find(".yaml") == std::string::npos) {
            // If spec_file doesn't contain .yaml, treat it as directory path for YAML files
            if (parser.loadFromYamlDirectory(config_.files.spec_file)) {
                loaded = true;
                std::cout << "Successfully loaded YAML specs from directory: " << config_.files.spec_file << std::endl;
            }
        }
        
        if (!loaded) {
            // Fallback to TSV file loading
            if (!parser.loadFromFile(config_.files.spec_file)) {
                std::cerr << "Failed to load spec file: " << config_.files.spec_file << std::endl;
                return false;
            }
        }
        
        masterLayout_ = parser.getLayout(config_.layouts.master);
        siseLayout_ = parser.getLayout(config_.layouts.sise);
        siseLayout_->dump();
        hogaLayout_ = parser.getLayout(config_.layouts.hoga);
        
        if (!masterLayout_ || !siseLayout_ || !hogaLayout_) {
            std::cerr << "Required layouts not found in spec file" << std::endl;
            return false;
        }
        
        std::cout << "✓ 스펙 파일 로드 성공" << std::endl;
        return true;
    }
    
    bool init_master_manager() {
        // MasterManager 초기화
        master_manager_.reset(new MasterManager(LOG_INFO));

        // Config에서 지정된 master 설정 파일 로드
        if (!master_manager_->loadMasterConfigs(config_.files.master_file)) {
            std::cerr << "Failed to load master configurations from: " << config_.files.master_file << std::endl;
            return false;
        }

        // Config에서 지정된 master 가져오기
        std::string master_name = config_.master;  // "JAPAN_EQUITY_MASTER"
        active_master_ = master_manager_->getMaster(master_name);
        if (!active_master_) {
            std::cerr << "Failed to get master: " << master_name << std::endl;
            std::cerr << "Available masters:" << std::endl;
            auto master_names = master_manager_->getMasterNames();
            for (const auto& name : master_names) {
                std::cerr << "  - " << name << std::endl;
            }
            return false;
        }

        std::cout << "✓ MasterManager 초기화 완료, 활성 Master: " << master_name << std::endl;
        return true;
    }
    
    bool init_publisher() {
        publisher_.reset(new SimplePublisherV2(event_base_));
        
        publisher_->set_publisher_id(config_.id);
        publisher_->set_publisher_name(config_.name);
        
        /* sequence strorage init & set */
        /*
        SequenceStorage* storage;
        if(config_.storage_type == StorageType::FILE_STORAGE) {
            std::string seq_file = publisher_->get_publisher_name() + ".seq";
            std::string storage_dir = "./data/sequence_data";
            SequenceStorage* storage = new FileSequenceStorage(storage_dir,  seq_file);
        } else {
            std::string storage_path = "./data/hashmaster/" + publisher_->get_publisher_name() + "_sequences";
            storage = new HashmasterSequenceStorage(storage_path);
        }
        if(!storage->initialize()) {
            std::cerr << "❌ Failed to initialize sequence storage" << std::endl;
            return false;
        }
        publisher_->set_sequence_storage(storage);
        */
        if(!publisher_->init_sequence_storage(config_.storage_type)) {
            std::cerr << "Failed to initialize sequence storage" << std::endl;
            return false;
        }

        if (!publisher_->init_database(config_.pubsub.publisher.database_name)) {
            std::cerr << "Failed to initialize Publisher database" << std::endl;
            return false;
        }
        
        if (!publisher_->start_both(config_.pubsub.publisher.unix_socket_path, 
                                   config_.pubsub.publisher.tcp_host, 
                                   config_.pubsub.publisher.tcp_port)) {
            std::cerr << "Failed to start Publisher server" << std::endl;
            return false;
        }
        
        std::cout << "✓ Publisher started on Unix socket: " << config_.pubsub.publisher.unix_socket_path 
                  << " and TCP port: " << config_.pubsub.publisher.tcp_port << std::endl;
        return true;
    }
    
    bool init_mq_reader() {
        mq_reader_.reset(new MQReader(event_base_));
        
        // 먼저 메인 큐 생성 시도
        if (mq_reader_->create_mq(config_.messagequeue.name.c_str(), 
                                 config_.messagequeue.max_messages, 
                                 config_.messagequeue.message_size)) {
            std::cout << "✓ MQ Reader started on queue: " << config_.messagequeue.name << std::endl;
        } else {
            std::cerr << "Failed to create message queue: " << config_.messagequeue.name << std::endl;
            return false;
        }
        
        // TREP 데이터 콜백 설정
        mq_reader_->set_topic_callback([this](DataTopic topic, const char* data, size_t size) {
            this->handle_trep_data_from_mq(topic, data, size);
        });
        
        mq_reader_->start();
        return true;
    }
    
    bool init_subscribers() {
        // Config에서 subscriber 설정들을 읽어와서 생성
        for (const auto& sub_config : config_.pubsub.subscribers) {
            if (!sub_config.enabled) {
                std::cout << "Subscriber " << sub_config.name << " is disabled, skipping" << std::endl;
                continue;
            }
            
            std::unique_ptr<SimpleSubscriber> subscriber(new SimpleSubscriber(event_base_));
            subscriber->set_client_info(config_.id, config_.name, sub_config.pub_id, sub_config.pub_name);

            if(!subscriber->init_sequence_storage(config_.storage_type)) {
                std::cerr << "Failed to initialize sequence storage" << std::endl;
                return false;
            }
            // sub_config.topic_mask = DataTopic::ALL_TOPICS;
            subscriber->set_subscription_mask(sub_config.topic_mask);
            // subscriber->set_subscription_mask(DataTopic::ALL_TOPICS);
            std::cout << "✓ Subscriber " << sub_config.name << " subscription mask: " << sub_config.topic_mask << std::endl;
            
            // TREP 데이터 콜백 설정
            subscriber->set_topic_callback([this](DataTopic topic, const char* data, int size) {
                this->handle_trep_data_from_subscriber(topic, data, size);
            });
            if (sub_config.type == "unix") {
                subscriber->set_address(SocketType::UNIX_SOCKET, sub_config.socket_path);
            } else if (sub_config.type == "tcp") {
                subscriber->set_address(SocketType::TCP_SOCKET, sub_config.host, sub_config.port);
            }
            subscribers_.push_back(std::move(subscriber));
            
            std::cout << "✓ Initialized subscriber: " << sub_config.name 
                      << " (ID: " << sub_config.client_id 
                      << ", Type: " << sub_config.type << ")";
            if (sub_config.type == "tcp") {
                std::cout << " Host: " << sub_config.host << ":" << sub_config.port;
            } else if (sub_config.type == "unix") {
                std::cout << " Socket: " << sub_config.socket_path;
            }
            std::cout << std::endl;
        }
        
        std::cout << "✓ Initialized " << subscribers_.size() << " active subscribers" << std::endl;
        return true;
    }
    
    

    // MQ에서 IPC 헤더 기반 메시지 처리
    void handle_trep_data_from_mq(DataTopic /*topic*/, const char* data, size_t size) {
        if (size < sizeof(ipc_header)) {
            std::cerr << "메시지가 너무 작습니다: " << size << " bytes" << std::endl;
            return;
        }
        
        // IPC 헤더 파싱
        const ipc_header* header = reinterpret_cast<const ipc_header*>(data);
        
        // 메시지 크기 검증
        if (header->_msg_size != size) {
            std::cerr << "메시지 크기 불일치: header=" << header->_msg_size 
                      << ", actual=" << size << std::endl;
            return;
        }
        
        // 메시지 데이터 포인터 (헤더 이후)
        const char* msg_data = data + sizeof(ipc_header);
        size_t msg_data_size = size - sizeof(ipc_header);
        
        // _msg_type별 핸들러 호출
        auto handler_it = msg_type_handlers_.find(header->_msg_type);
        if (handler_it != msg_type_handlers_.end()) {
            handler_it->second(msg_data, msg_data_size);
        } else {
            std::cerr << "알 수 없는 메시지 타입: '" << header->_msg_type 
                      << "' (0x" << std::hex << (int)(unsigned char)header->_msg_type << ")" << std::endl;
        }
        
        processed_count_++;
    }
    
    // 메시지 타입별 핸들러 함수들
    void handle_trep_data_message(const char* data, size_t size) {
        // std::string trep_line(data, size);
        // process_trep_line(trep_line);
        std::cout << "\n\n\thandle_trep_data_message: " << size << " bytes" << std::endl;
        // trep data를 파싱해서 처리 (각 업무별 클래스에서 실제 구현)
    }
    
    void handle_master_update_message(const char* data, size_t size) {
        std::cout << "🔄 마스터 업데이트 메시지 수신: " << size << " bytes" << std::endl;
        master_update_count_++;
        // 마스터 업데이트 로직 구현
    }
    
    void handle_sise_data_message(const char* data, size_t size) {
        std::cout << "📊 시세 데이터 메시지 수신: " << size << " bytes" << std::endl;
        sise_count_++;
        // 시세 데이터 처리 로직 구현
    }
    
    void handle_hoga_data_message(const char* data, size_t size) {
        std::cout << "📈 호가 데이터 메시지 수신: " << size << " bytes" << std::endl;
        hoga_count_++;
        // 호가 데이터 처리 로직 구현
    }
    
    void handle_control_message(const char* data, size_t size) {
        std::string control_cmd(data, size);
        std::cout << "🎛️ 제어 메시지 수신: " << control_cmd << std::endl;
        
        // string 기반 핸들러 호출
        auto handler_it = string_handlers_.find(control_cmd);
        if (handler_it != string_handlers_.end()) {
            handler_it->second(data, size);
        } else {
            std::cout << "알 수 없는 제어 명령: " << control_cmd << std::endl;
        }
    }
    
    void handle_status_message(const char* data, size_t size) {
        std::string status_msg(data, size);
        std::cout << "📊 상태 메시지 수신: " << status_msg << std::endl;
    }
    
    void handle_heartbeat_message(const char* data, size_t size) {
        std::cout << "💗 하트비트 수신: " << size << " bytes" << std::endl;
    }
    
    void load_message_handlers() {
#if 0   // todo
        std::string path = config["plugin"]["search_path"].as<std::string>();
        std::string module = config["plugin"]["module"].as<std::string>();

        void* handle = dlopen((path + "/" + module).c_str(), RTLD_NOW);
        if (!handle) {
            throw std::runtime_error(dlerror());
        }

        for (auto&& item : config["handlers"]["message_types"]) {
            std::string type = item.first.as<std::string>();
            std::string symbol = item.second["symbol"].as<std::string>();
            bool enabled = item.second["enabled"].as<bool>();

            if (enabled) {
                HandlerFunc func = reinterpret_cast<HandlerFunc>(dlsym(handle, symbol.c_str()));
                if (!func) {
                    throw std::runtime_error(dlerror());
                }
                // dispatch 테이블에 등록
                register_handler(type, func);
            }
        }
#endif        
    }

    void load_schedule_handlers() {
        // todo
    }

    // Scheduler management functions (moved from T2MA_JAPAN_EQUITY)
    virtual void init_scheduler_handlers();
    void setup_schedulers();
    void cleanup_schedulers();

    // Schedule helper functions (moved from T2MA_JAPAN_EQUITY)
    std::chrono::seconds parseTimeToSeconds(const std::string& time_str);
    std::chrono::system_clock::time_point getNextScheduleTime(const T2MAConfig::SchedulerItem& item);
    bool isWithinScheduleTime(const T2MAConfig::SchedulerItem& item);

    // Default scheduler handler functions (can be overridden in derived classes)
    virtual void control_stats();
    virtual void control_reload_master();
    virtual void control_clear_stats();
    virtual void control_heartbeat();

    // libevent callback wrapper (moved from T2MA_JAPAN_EQUITY)
    static void scheduler_callback(evutil_socket_t fd, short what, void* arg);

    // 메소드 테이블 초기화
    virtual void init_message_handlers() {
        std::cout << "_msg_type별 핸들러 등록 at" << __FILE__ << ":" << __LINE__ << std::endl; 
        // _msg_type별 핸들러 등록
        /*
        msg_type_handlers_[static_cast<char>(MsgType::TREP_DATA)] = 
            [this](const char* data, size_t size) { this->handle_trep_data_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::MASTER_UPDATE)] = 
            [this](const char* data, size_t size) { this->handle_master_update_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::SISE_DATA)] = 
            [this](const char* data, size_t size) { this->handle_sise_data_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::HOGA_DATA)] = 
            [this](const char* data, size_t size) { this->handle_hoga_data_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::CONTROL)] = 
            [this](const char* data, size_t size) { this->handle_control_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::STATUS)] = 
            [this](const char* data, size_t size) { this->handle_status_message(data, size); };
            
        msg_type_handlers_[static_cast<char>(MsgType::HEARTBEAT)] = 
            [this](const char* data, size_t size) { this->handle_heartbeat_message(data, size); };
        
        // string별 제어 명령 핸들러 등록
        string_handlers_[ControlCommands::START] = 
            [this](const char* data, size_t size) { 
                std::cout << "✅ START 명령 처리" << std::endl; 
            };
            
        string_handlers_[ControlCommands::STOP] = 
            [this](const char* data, size_t size) { 
                std::cout << "🛑 STOP 명령 처리" << std::endl;
                this->running_ = false;
            };
            
        string_handlers_[ControlCommands::STATS] = 
            [this](const char* data, size_t size) { 
                this->print_statistics();
            };
            
        string_handlers_[ControlCommands::RELOAD_MASTER] = 
            [this](const char* data, size_t size) { 
                std::cout << "🔄 마스터 데이터 재로드 명령" << std::endl;
                this->reload_master_data();
            };
            
        string_handlers_[ControlCommands::CLEAR_STATS] = 
            [this](const char* data, size_t size) { 
                std::cout << "🧹 통계 초기화 명령" << std::endl;
                this->clear_statistics();
            };
        
        std::cout << "✓ 메시지 핸들러 테이블 초기화 완료" << std::endl;
        std::cout << "  - _msg_type 핸들러: " << msg_type_handlers_.size() << "개" << std::endl;
        std::cout << "  - string 핸들러: " << string_handlers_.size() << "개" << std::endl;
        */
    }
    void setup_message_handlers();  /* MSG_TYPE 별 handler 설정 */
    void setup_command_handlers();  /* 제어 명령 핸들러 설정 */

    // 통계 출력
    void print_statistics() {
        std::cout << "\n=== T2MA 시스템 통계 ===" << std::endl;
        std::cout << "총 처리 메시지: " << processed_count_ << std::endl;
        std::cout << "마스터 업데이트: " << master_update_count_ << std::endl;
        std::cout << "시세 데이터: " << sise_count_ << std::endl;
        std::cout << "호가 데이터: " << hoga_count_ << std::endl;
        
        if (publisher_) {
            std::cout << "연결된 클라이언트: " << publisher_->get_client_count() << std::endl;
            std::cout << "Publisher 시퀀스: " << publisher_->get_current_sequence() << std::endl;
        }
        
        if (mq_reader_) {
            std::cout << "MQ 수신 메시지: " << mq_reader_->get_messages_received() << std::endl;
        }
        
        std::cout << "========================\n" << std::endl;
    }
    
    // 통계 초기화
    void clear_statistics() {
        processed_count_ = 0;
        master_update_count_ = 0;
        sise_count_ = 0;
        hoga_count_ = 0;
    }
    
    // 마스터 데이터 재로드
    void reload_master_data() {
        if (master_manager_ && active_master_) {
            std::cout << "마스터 데이터 재로드 중..." << std::endl;
            // CSV 로딩은 별도 함수로 처리 (load_symbols_from_csv 참조)
            load_symbols_from_csv();
        }
    }
    
    // Subscriber에서 TREP 데이터 처리
    void handle_trep_data_from_subscriber(DataTopic /*topic*/, const char* data, int size) {
        /*
        std::string trep_line(data, size);
        // process_trep_line(trep_line);
        msg_type_handlers_[static_cast<char>(MsgType::TREP_DATA)](data, size);
        */
        std::cout << "\n\n\thandle_trep_data_from_subscriber: " << size << " bytes" << std::endl;
        if (size < sizeof(ipc_header)) {
            std::cerr << "메시지가 너무 작습니다: " << size << " bytes" << std::endl;
            return;
        }
        
        // IPC 헤더 파싱
        const ipc_header* header = reinterpret_cast<const ipc_header*>(data);
        
        // 메시지 크기 검증
        if (header->_msg_size != size) {
            std::cerr << "메시지 크기 불일치: header=" << header->_msg_size 
                      << ", actual=" << size << std::endl;
            return;
        }
        {
            // for debug
            std::cout << "header->_msg_type: " << header->_msg_type << std::endl;
            std::cout << "header->_msg_size: " << header->_msg_size << std::endl;
            
        }
        
        // 메시지 데이터 포인터 (헤더 이후)
        const char* msg_data = data + sizeof(ipc_header);
        size_t msg_data_size = size - sizeof(ipc_header);
        
        // _msg_type별 핸들러 호출
        auto handler_it = msg_type_handlers_.find(header->_msg_type);
        if (handler_it != msg_type_handlers_.end()) {
            if(header->_msg_type != 'T') {
                std::cerr << "메시지타입에 따른 헨들러 호출: '" << header->_msg_type 
                          << "' (0x" << std::hex << (int)(unsigned char)header->_msg_type << ")" << std::endl;
            }
            handler_it->second(msg_data, msg_data_size);
        } else {
            std::cerr << "알 수 없는 메시지 타입: '" << header->_msg_type 
                      << "' (0x" << std::hex << (int)(unsigned char)header->_msg_type << ")" << std::endl;
        }
        
        processed_count_++;
    }
    
    // 일본 주식 TREP 데이터 처리 함수 (RAFR_JAPANEQUITYTREP2MAST.cpp 기반)
    void process_japan_equity(const std::string& line) {
        if (line.empty()) return;
        
        // TREP 데이터 파싱
        auto trepData = TrepParser::parseLine(line);
        
        // RIC 코드 추출
        auto ricIt = trepData.find("0");
        if (ricIt == trepData.end()) return;
        
        std::string ric = ricIt->second;
        
        
        
        // 1. 일본 주식 마스터 업데이트
        // update_japan_equity_master(ric, trepData);
        master_update_count_++;
        
        // 2. 체결 데이터 체크 (FID 6: 현재가, FID 178: 체결량)
        // 마스터 업데이트후 시세 송신으로 변경
        // if (has_japan_trade_data(trepData)) {
        //     send_japan_sise_data(ric, trepData);
        //     sise_count_++;
        // }
        // 3. 종료 데이터 처리 (FID 3372: 종가)
        // 여기는 일단 주석 처리
        // else if (has_japan_close_data(trepData)) {
        //     send_japan_close_data(ric, trepData);
        //     sise_count_++;
        // }
        
        
        processed_count_++;
        
        if (processed_count_ % config_.monitoring.log_interval == 0) {
            std::cout << "Processed " << processed_count_ << " Japan Equity TREP messages" << std::endl;
        }
    }
    
    
    
    
    
    
    
    
    
    // 기본 TREP 라인 처리
    void process_trep_line(const std::string& line) {
        if (line.empty()) return;
        
        // TREP 데이터 파싱
        auto trepData = TrepParser::parseLine(line);
        
        // RIC 코드 추출
        auto ricIt = trepData.find("0");
        if (ricIt == trepData.end()) return;
        
       
        processed_count_++;
        
        // Config에서 설정한 간격으로 처리 로그
        if (processed_count_ % config_.monitoring.log_interval == 0) {
            std::cout << "Processed " << processed_count_ << " TREP messages" << std::endl;
        }
    }
    
    
    
    // CSV 파일에서 종목 로딩
    bool load_symbols_from_csv() {
        if (!active_master_) {
            std::cerr << "Active master not available" << std::endl;
            return false;
        }

        active_master_->clear();
        std::string filename = config_.files.csv_file;
        std::cout << "CSV 파일에서 마스터 데이터 로딩: " << filename << std::endl;

        // CSV 파일 파싱 및 Master에 데이터 로드
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open CSV file: " << filename << std::endl;
            return false;
        }

        std::string line;
        int count = 0;
        int inserted = 0;

        // 헤더 스킵 (있는 경우)
        if (std::getline(file, line)) {
            std::cout << "CSV 헤더: " << line << std::endl;
        }

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            auto fields = CsvParser::parseLine(line);
            if (fields.size() < 2) continue;

            std::string ric = fields[3];  // RIC_CD
            std::string symbol = fields[4]; // SYMBOL_CD

            // 기본 레코드 생성
            BinaryRecord record(masterLayout_);
            record.setString("RIC_CD", ric);
            if (fields.size() > 5) record.setString("SYMBOL_CD", symbol);
            if (fields.size() > 2) record.setString("EXCHG_CD", fields[2]);
            if (fields.size() > 3) record.setString("CUR_CD", fields[3]);

            // Primary key: RIC, Secondary key: SYMBOL.EXCHANGE 형식
            std::string secondary_key = symbol + "." + (fields.size() > 2 ? fields[2] : "");

            if(active_master_->get_by_primary(ric.c_str()) == nullptr) {
                if (active_master_->put(ric.c_str(), symbol.c_str(),
                                    record.getBuffer(), record.getSize()) == 0) {
                    inserted++;
                }
            }
            count++;

            if (count % 10000 == 0) {
                std::cout << "CSV 처리 진행: " << count << "건" << std::endl;
            }
        }

        std::cout << "✓ CSV 마스터 데이터 로드 완료: " << count << "건 처리, " << inserted << "건 저장" << std::endl;
        return true;
    }
    
    void run() {
        running_ = true;
        std::cout << "\n\n\tT2MA System running...\n\n" << std::endl;
        
        // 일단 여기서 publisher 에 연결시도
        for(auto& subscriber: subscribers_) {
            #if 0
            if(subscriber->get_socket_type() == SocketType::TCP_SOCKET) {
                subscriber->connect_tcp(subscriber->get_address(), subscriber->get_port());
            } else {
                std::cout << "\n\n##############" << std::endl;
                std::cout << "try to connect : " << subscriber->get_address() << std::endl;
                subscriber->connect_unix(subscriber->get_address());
            }
            #endif
            subscriber->try_reconnect();
        }
        // 이벤트 루프 실행
        while (running_) {
            event_base_loop(event_base_, EVLOOP_ONCE);
        }
    }
    
    void stop() {
        running_ = false;
        std::cout << "Stopping T2MA System..." << std::endl;
        
        if (mq_reader_) {
            mq_reader_->stop();
        }
        
        if (publisher_) {
            publisher_->stop();
        }
        
        for (auto& subscriber : subscribers_) {
            subscriber->stop();
        }
        
        if (event_base_) {
            event_base_loopbreak(event_base_);
        }
    }
    
    void cleanup() {
        stop();

        // Cleanup schedulers
        cleanup_schedulers();

        subscribers_.clear();
        publisher_.reset();
        mq_reader_.reset();
        master_manager_.reset();

        if (event_base_) {
            event_base_free(event_base_);
            event_base_ = nullptr;
        }
    }

    // Scheduler 설정 헬퍼 메소드 (moved from T2MA_JAPAN_EQUITY)
    const std::vector<T2MAConfig::SchedulerItem>& getSchedulers() const {
        return config_.schedulers_ext;
    }
    
};

#endif // T2MASYSTEM_H