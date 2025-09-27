#ifndef SIMPLE_PUBLISHER_V2_H
#define SIMPLE_PUBLISHER_V2_H

#include "Common.h"
#include "../common/MessageDB.h"
#include "../common/Memory_SAM.h"
#include "../common/db_sam.h"
#include "PubSubTopicProtocol.h"
#include "../eventBase/EventBase.h"
#include "SequenceStorage.h"
#include "FileSequenceStorage.h"
#include "HashmasterSequenceStorage.h"

#include <map>
#include <memory>
#include <atomic>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

using namespace SimplePubSub;

// Forward declaration
class SimplePublisherV2;

// -----------------------------
// Recovery Worker
// -----------------------------
struct RecoveryTask {
    std::shared_ptr<ClientInfo> client;
    uint32_t from_seq;
    uint32_t to_seq;
};

struct RecoveryWorker {
    event_base *base{nullptr};
    event *notify_event{nullptr};
    int notify_pipe_r{-1};
    int notify_pipe_w{-1};
    std::mutex queue_mu;
    std::queue<::RecoveryTask> task_q;
    std::thread th;
    std::atomic<bool> running{false};

    void on_notify();
};


/*
 * 새로운 SimplePublisher 설계:
 * 
 * 기본 원칙:
 * 1. SimpleSubscriber와 대칭적인 구조로 간단하게 설계
 * 2. 
 * 3. 복구 처리는 별도 스레드에서 
 * 
 * 핵심 기능:
 * 1. 서버 시작 - Unix Domain Socket 또는 TCP Socket에서 listen
 * 2. 구독 요청 처리 - SubscriptionRequest/Response
 * 3. 복구 요청 처리 - RecoveryRequest/Response (별도 스레드)
 * 4. 메시지 발행 - publish() 호출 시 모든 ONLINE 클라이언트에 전송
 * 
 * 클라이언트 스레드 이동:
 * 1. 메인 스레드: 일반적인 클라이언트 통신 처리 (ONLINE 상태)
 * 2. 복구 스레드: 복구 중인 클라이언트 처리 (RECOVERING 상태)
 * 3. 복구 완료 후 클라이언트를 다시 메인 스레드로 이동
 */
class SimplePublisherV2 {
private:
    // 기본 정보
    uint32_t _publisher_id;
    std::string _publisher_name;
    
    // 네트워크 설정
    std::string _unix_path;
    std::string _tcp_address;
    uint16_t _tcp_port;

    
    // EventBase 관련
    struct event_base* _main_base;
    evconnlistener* _listener;

    std::mutex _clients_mu;
    std::map<uint32_t,std::shared_ptr<ClientInfo>> _clients;
    std::vector<RecoveryWorker*> _workers;
    std::atomic<uint32_t> _rr_counter{0};
    

    int _main_notify_pipe[2];
    event *_main_notify_event{nullptr};
    std::mutex _main_return_mu;
    std::queue<std::shared_ptr<ClientInfo>> _main_return_q;
    
    // 시퀀스 관리
    PublisherSequenceRecord* _publisher_sequence_record;
    StorageType _sequence_storage_type;
    SequenceStorage* _sequence_storage;
    
    // 데이터 저장
    std::unique_ptr<MessageDB> _db;
    std::string _db_path;

    // 추가 멤버 변수들
    bool _use_unix{true};
    
    friend struct RecoveryWorker;

    // accept
    static void static_accept_cb(evconnlistener*,evutil_socket_t fd,sockaddr*,int,void*ptr);
    void on_accept(evutil_socket_t fd);
    // read
    static void static_read_cb(bufferevent*bev,void*ctx);
    void on_read(bufferevent*bev,std::shared_ptr<ClientInfo>ci);
    static void static_event_cb(bufferevent*bev,short ev,void*ctx);
    void on_client_disconnect(std::shared_ptr<ClientInfo>ci);
    
    // main notify
    void main_notify_cb(evutil_socket_t fd);

public:
    // 생성자/소멸자
    SimplePublisherV2(struct event_base* shared_event_base);
    ~SimplePublisherV2();
    
    // 설정
    void set_address(SocketType socket_type, std::string address, int port=0);
    void set_sequence_storage(SequenceStorage* sequence_storage);
    void set_unix_path(const std::string &path);
    void set_tcp_address(const std::string &address);
    void set_tcp_port(uint16_t port);

    void set_publisher_id(uint32_t id) {_publisher_id = id;}
    void set_publisher_name(const std::string &name) {_publisher_name = name;}
    inline std::string get_publisher_name() const {return _publisher_name;}
    inline uint32_t get_publisher_id() const {return _publisher_id;}
    // Sequence Storage initialization (스토리지 생성, 초기화, sequence record 로드)
    bool init_sequence_storage(StorageType storage_type);
    // Database initialization
    bool init_database(const std::string& db_path);

    MessageDB* db(){return _db.get();}
    event_base* main_base(){return _main_base;}
    size_t get_client_count() const;
    inline int get_publisher_date() const {return _publisher_sequence_record->publisher_date;}
    uint32_t get_current_sequence() const ;

    // 서버 시작/종료
    bool start(size_t recovery_thread_count = 2);
    bool start_both(const std::string& unix_path, const std::string& tcp_host, int tcp_port);
    void stop();
    
    // 복구 스레드 관리
    void start_recovery_threads();
    void stop_recovery_threads();
    void recovery_worker_thread();
    void enqueue_recovery_task(const ::RecoveryTask& task);
    
    // 메시지 발행
    void publish(DataTopic topic, const char* data, size_t size);
    
    /*
    // 이벤트 핸들러
    void handle_accept(int fd, struct sockaddr* addr, int len);
    void handle_disconnected(char* data, int size);
    void handle_error(char* data, int size);
    void handle_client_messages(uint32_t client_id, char* data, int size);
    */
    // 메시지 처리
    void handle_subscription_request(std::shared_ptr<ClientInfo> ci, const SubscriptionRequest* request);
    void handle_recovery_request(std::shared_ptr<ClientInfo> ci, const RecoveryRequest* request);

    void enqueue_return_client(std::shared_ptr<ClientInfo> ci);

    /* 미사용
    // 클라이언트 이동 관리
    void move_client_to_recovery(uint32_t client_id);
    void move_client_to_main(uint32_t client_id);
    void cleanup_client_thread(uint32_t client_id);
    
    
    
    // 복구 처리 (복구 스레드에서 실행)
    void process_client_recovery(uint32_t client_id, const ::RecoveryTask& task);
    void send_recovery_response(uint32_t client_id, uint32_t start_seq, uint32_t end_seq, uint32_t total_messages);
    void send_recovery_complete(uint32_t client_id, uint32_t total_sent);
    void send_message_from_recovery_thread(uint32_t client_id, const char* data, size_t size);
    */

    /*
    // 클라이언트 관리
    void add_client(const ClientInfo& client);
    void remove_client(uint32_t client_id);
    ClientInfo* get_client(uint32_t client_id);
    */
};

#endif // SIMPLE_PUBLISHER_V2_H
