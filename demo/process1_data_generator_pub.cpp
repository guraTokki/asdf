#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <cstring>
#include <event2/event.h>

#include "pubsub/SequenceStorage.h"
#include "pubsub/SimplePublisherV2.h"
#include "pubsub/FileSequenceStorage.h"
#include "pubsub/HashmasterSequenceStorage.h"
#include "common/IPCHeader.h"

using namespace SimplePubSub;




class Process1_DataGenerator_Pub {
private:
    struct event_base* event_base_;
    std::unique_ptr<SimplePublisherV2> publisher_;
    std::unique_ptr<SimplePubSub::SequenceStorage> storage_;
    std::string data_file_;
    std::string socket_path_;
    bool running_;
    int messages_sent_;
    int send_interval_ms_;
    int max_messages_;
    MsgType default_msg_type_;
    StorageType storage_type_;
    bool clear_storage_;

public:
    Process1_DataGenerator_Pub(const std::string& data_file, const std::string& socket_path, 
                              int interval_ms = 100, int max_msg = 100, MsgType msg_type = MsgType::TREP_DATA,
                              StorageType storage = StorageType::FILE_STORAGE, bool clear_storage = false) 
        : data_file_(data_file), socket_path_(socket_path), running_(false), messages_sent_(0), 
          send_interval_ms_(interval_ms), max_messages_(max_msg), default_msg_type_(msg_type),
          storage_type_(storage), clear_storage_(clear_storage) {
        event_base_ = nullptr;
        publisher_ = nullptr;
    }

    ~Process1_DataGenerator_Pub() {
        stop();
        if (event_base_) {
            event_base_free(event_base_);
        }
    }

    bool init_publisher() {
        // Create event base
        event_base_ = event_base_new();
        if (!event_base_) {
            std::cerr << "❌ Failed to create event base" << std::endl;
            return false;
        }

        // Create publisher with shared event base
        publisher_ = std::make_unique<SimplePublisherV2>(event_base_);
        
        // Set publisher identity
        publisher_->set_publisher_id(1000);
        publisher_->set_publisher_name("DataGeneratorPub");
#ifdef OLD
        publisher_->set_publisher_date(publisher_->get_current_date_int());
#endif        
#if 0
        // Configure sequence storage using Strategy pattern
        if (storage_type_ == StorageType::FILE_STORAGE) {
            // File-based storage
            std::string seq_file = publisher_->get_publisher_name() + ".seq";
            std::string storage_dir = "./data/sequence_data";
            storage_ = std::make_unique<SimplePubSub::FileSequenceStorage>(storage_dir, seq_file);
            std::cout << "📄 Using FileSequenceStorage: " << seq_file << std::endl;
        } else {
            // HashMaster-based storage
            std::string storage_path = "./data/hashmaster/" + publisher_->get_publisher_name() + "_sequences";
            storage_ = std::make_unique<SimplePubSub::HashmasterSequenceStorage>(storage_path);
            std::cout << "⚡ Using HashmasterSequenceStorage: " << storage_path << std::endl;
        }

        // Initialize and set storage strategy
        if (!storage_->initialize()) {
            std::cerr << "❌ Failed to initialize sequence storage" << std::endl;
            return false;
        }
#endif
        publisher_->init_sequence_storage(storage_type_);
        // Clear storage if requested (especially useful for HashMaster storage)
        if (clear_storage_) {
            std::cout << "🗑️  Clearing storage data as requested..." << std::endl;
            storage_->clear();
        }

        publisher_->set_sequence_storage(storage_.get());
#ifdef OLD                
        // Load sequences from storage (if exists)
        if (!publisher_->load_sequences()) {
            std::cerr << "⚠️  Warning: Failed to load sequences, starting fresh" << std::endl;
        }

        // Configure auto-save (save every 5 messages)
        publisher_->set_auto_save_sequences(true);
        publisher_->set_save_interval(5);
#endif        
        // Initialize database (optional)
        std::string db_path = "./data/data_generator_pubsub_db";
        if (!publisher_->init_database(db_path)) {
            std::cerr << "⚠️  Warning: Failed to initialize database (continuing anyway)" << std::endl;
        }

        publisher_->set_address(UNIX_SOCKET, socket_path_);
        // Start Unix socket server
        if (!publisher_->start(2)) {
            std::cerr << "❌ Failed to start Unix socket server: " << socket_path_ << std::endl;
            return false;
        }

        std::cout << "✓ Publisher started on Unix socket: " << socket_path_ << std::endl;
        return true;
    }

    SimplePubSub::DataTopic msgTypeToDataTopic(MsgType msg_type) {
        switch (msg_type) {
            case MsgType::TREP_DATA:
            case MsgType::SISE_DATA:
                return SimplePubSub::DataTopic::TOPIC1;
            case MsgType::HOGA_DATA:
                return SimplePubSub::DataTopic::TOPIC2;
            case MsgType::MASTER_UPDATE:
            case MsgType::STATUS:
            case MsgType::CONTROL:
            case MsgType::HEARTBEAT:
            default:
                return SimplePubSub::DataTopic::MISC;
        }
    }

    bool publish_message(MsgType msg_type, const std::string& data) {
        if (!publisher_) {
            std::cerr << "Publisher not initialized" << std::endl;
            return false;
        }
        
        // IPC 헤더 구성 (기존과 호환성을 위해)
        ipc_header header;
        header._msg_type = static_cast<char>(msg_type);
        header._reserved = 0;
        header._msg_size = sizeof(ipc_header) + data.size();
        
        // 전체 메시지 버퍼 구성
        std::string message;
        message.append(reinterpret_cast<const char*>(&header), sizeof(ipc_header));
        message.append(data);
        
        // 메시지 타입에 따라 적절한 토픽으로 발행
        SimplePubSub::DataTopic topic = msgTypeToDataTopic(msg_type);
        publisher_->publish(topic, message.c_str(), message.size());
        
        return true;
    }

    bool run() {
        std::ifstream file(data_file_);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open data file: " << data_file_ << std::endl;
            return false;
        }

        std::cout << "\n=== Process 1: T2MA Data Generator (Publisher) ===" << std::endl;
        std::cout << "📁 Data file: " << data_file_ << std::endl;
        std::cout << "🔌 Unix socket: " << socket_path_ << std::endl;
        std::cout << "🆔 Publisher: " << publisher_->get_publisher_name() 
                  << " (ID: " << publisher_->get_publisher_id() 
                  << ", Date: " << publisher_->get_publisher_date() << ")" << std::endl;
#ifdef OLD        
        // Display storage type information
        auto* storage = publisher_->get_sequence_storage();
        if (storage) {
            std::string storage_type_name = (storage_type_ == StorageType::FILE_STORAGE) ? "File" : "HashMaster";
            std::cout << "💾 Storage: " << storage->get_storage_type() << " (" << storage_type_name << ")" << std::endl;
        } else {
            std::cout << "⚠️  Storage: Not configured" << std::endl;
        }
        std::cout << "💾 Auto-save: " << (publisher_->get_auto_save_sequences() ? "ON" : "OFF") 
                  << " (every " << publisher_->get_save_interval() << " messages)" << std::endl;
        std::cout << "🏷️  Message type: '" << static_cast<char>(default_msg_type_) 
                  << "' (" << msgTypeToString(default_msg_type_) << ")" << std::endl;
        std::cout << "⏱️  Send interval: " << send_interval_ms_ << "ms" << std::endl;
        std::cout << "📊 Max messages: " << max_messages_ << std::endl;
        std::cout << "🚀 Starting data generation with SimplePublisher..." << std::endl;
#endif
        running_ = true;
        std::string line;
        
        // 시작 메시지 발행
        publish_message(MsgType::STATUS, "Data generator started");
        
        // 별도 스레드에서 event loop 실행
        std::thread event_thread([this]() {
            event_base_dispatch(event_base_);
        });
        event_thread.detach();
        
        getchar();
        
        while (running_ && std::getline(file, line) && messages_sent_ < max_messages_) {
            if (line.empty()) {
                continue;
            }

            // 너무 긴 라인은 자르기 (IPC 헤더 고려)
            size_t max_data_size = 1020 - sizeof(ipc_header); // 안전 마진
            if (line.length() > max_data_size) {
                line = line.substr(0, max_data_size - 3) + "...";
            }

            // IPC 헤더와 함께 메시지 발행
            if (publish_message(default_msg_type_, line)) {
                std::cout << "📤 [" << static_cast<char>(default_msg_type_) << "] " << line << std::endl;
                messages_sent_++;
                
                // 진행상황 출력
                if (messages_sent_ % 10 == 0) {
                    std::cout << "📊 Progress: " << messages_sent_ << "/" << max_messages_ << " messages sent" << std::endl;
                }
            }

            // 메시지 간 대기
            std::this_thread::sleep_for(std::chrono::milliseconds(send_interval_ms_));
        }

        // 완료 메시지 발행
        publish_message(MsgType::STATUS, "Data generation completed");

        file.close();
        std::cout << "\n✅ Process 1: Data generation completed. Total sent: " << messages_sent_ << std::endl;
        
        // 추가 대기 시간 (클라이언트가 데이터를 받을 수 있도록)
        std::cout << "🕐 Waiting 5 seconds for clients to receive data..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        return true;
    }

    void send_control_commands() {
        std::cout << "\n--- Sending control commands ---" << std::endl;
        
        publish_message(MsgType::CONTROL, "HELLOWORLD");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 통계 요청
        publish_message(MsgType::CONTROL, ControlCommands::STATS);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 하트비트
        publish_message(MsgType::HEARTBEAT, "Generator heartbeat");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 상태 메시지
        publish_message(MsgType::STATUS, "Generator running normally");
    }

    void stop() {
        if (running_) {
            running_ = false;
            // 종료 메시지 발행
            if (publisher_) {
                publish_message(MsgType::STATUS, "Data generator stopping");
            }
        }
        
        // 먼저 event loop 중지
        if (event_base_) {
            event_base_loopbreak(event_base_);
        }
        
        // 잠시 대기하여 event loop가 완전히 종료되도록 함
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Publisher와 storage 안전하게 정리
        if (publisher_) {
            publisher_->stop();  // Publisher 내부 정리 먼저
            publisher_.reset();   // 그 다음 리소스 해제
        }
    }

    int get_messages_sent() const {
        return messages_sent_;
    }
};

// Global generator instance for signal handling
Process1_DataGenerator_Pub* g_generator = nullptr;

void signal_handler(int sig) {
    std::cout << "\n🛑 Process 1: Signal " << sig << " received. Stopping..." << std::endl;
    if (g_generator) {
        g_generator->stop();
    }
}

void print_usage(const char* program_name) {
    std::cout << "\n=== Process 1: T2MA Data Generator (SimplePublisher) ===" << std::endl;
    std::cout << "Usage: " << program_name << " <data_file> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -i <interval_ms>  : Interval between messages (default: 100)" << std::endl;
    std::cout << "  -s <socket_path>  : Unix socket path (default: /tmp/japan_feed1.sock)" << std::endl;
    std::cout << "  -m <max_messages> : Maximum messages to send (default: 50)" << std::endl;
    std::cout << "  -t <msg_type>     : Message type (T=TREP, S=SISE, H=HOGA, M=MASTER, default: T)" << std::endl;
    std::cout << "  -c                : Send control commands after data" << std::endl;
    std::cout << "  --storage <type>  : Storage type (file|hashmaster, default: file)" << std::endl;
    std::cout << "  --clear-storage   : Clear storage data before starting (useful for hashmaster)" << std::endl;
    std::cout << "  -h                : Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Message Types:" << std::endl;
    std::cout << "  T : TREP_DATA (default) -> TRADE topic" << std::endl;
    std::cout << "  S : SISE_DATA -> TRADE topic" << std::endl;
    std::cout << "  H : HOGA_DATA -> QUOTE topic" << std::endl;
    std::cout << "  M : MASTER_UPDATE -> MISC topic" << std::endl;
    std::cout << "  C : CONTROL -> MISC topic" << std::endl;
    std::cout << "  X : STATUS -> MISC topic" << std::endl;
    std::cout << "  B : HEARTBEAT -> MISC topic" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  # Send Japan TREP data via Unix socket (file storage)" << std::endl;
    std::cout << "  " << program_name << " ../HashMaster/trep_data/jp_eq.trep.0813.txt -s /tmp/japan_feed1.sock -m 30" << std::endl;
    std::cout << "  # Send NASDAQ data as HOGA (QUOTE) type with HashMaster storage" << std::endl;
    std::cout << "  " << program_name << " ../HashMaster/trep_data/nasdaq_basic.trep.dat -t H -m 20 --storage hashmaster" << std::endl;
    std::cout << "  # Use HashMaster for high-performance sequence storage" << std::endl;
    std::cout << "  " << program_name << " data.trep -s /tmp/feed.sock --storage hashmaster" << std::endl;
    std::cout << "  # Clear HashMaster storage before starting (fresh data)" << std::endl;
    std::cout << "  " << program_name << " data.trep --storage hashmaster --clear-storage" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Check for help
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::string data_file = argv[1];
    std::string socket_path = "/tmp/japan_feed1.sock";  // 일본 시스템 기본값
    int interval_ms = 100;
    int max_messages = 50;
    MsgType msg_type = MsgType::TREP_DATA;
    bool send_control = false;
    StorageType storage_type = StorageType::FILE_STORAGE;  // 기본값: 파일 저장소
    bool clear_storage = false;

    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-i" && i + 1 < argc) {
            interval_ms = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "-s" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (std::string(argv[i]) == "-m" && i + 1 < argc) {
            max_messages = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "-t" && i + 1 < argc) {
            char type = argv[++i][0];
            msg_type = charToMsgType(type);
        } else if (std::string(argv[i]) == "-c") {
            send_control = true;
        } else if (std::string(argv[i]) == "--storage" && i + 1 < argc) {
            std::string storage_opt = argv[++i];
            if (storage_opt == "file" || storage_opt == "FILE") {
                storage_type = StorageType::FILE_STORAGE;
            } else if (storage_opt == "hashmaster" || storage_opt == "HASHMASTER") {
                storage_type = StorageType::HASHMASTER_STORAGE;
            } else {
                std::cerr << "⚠️  Unknown storage type: " << storage_opt << " (using default: file)" << std::endl;
            }
        } else if (std::string(argv[i]) == "--clear-storage") {
            clear_storage = true;
        }
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create generator with storage type
    Process1_DataGenerator_Pub generator(data_file, socket_path, interval_ms, max_messages, msg_type, storage_type, clear_storage);
    g_generator = &generator;

    // Initialize and run
    if (!generator.init_publisher()) {
        std::cerr << "❌ Process 1: Failed to initialize publisher" << std::endl;
        return 1;
    }

    bool success = generator.run();
    
    // Send control commands if requested
    if (send_control && success) {
        generator.send_control_commands();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "\n🏁 Process 1: Finished. Messages sent: " << generator.get_messages_sent() << std::endl;
    return success ? 0 : 1;
}