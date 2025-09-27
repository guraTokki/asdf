#ifndef SIMPLE_PUBLISHER_H
#define SIMPLE_PUBLISHER_H

#include "../common/Common.h"
#include "../common/db_sam.h"
#include "../common/Command.h"
#include "PubSubTopicProtocol.h"
#include "../eventBase/EventBase.h"
#include "SequenceStorage.h"
#include "pubsubCommand.h"
#include <map>
#include <memory>
#include <atomic>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

// Forward declarations
class PubClient;

using namespace SimplePubSub;


/*
 * TOPIC Publisher: EventBase 클래스를 이용하여 통신 연결 및 데이터 송수신 하며, 쓰레드간 동기화를 위해 CommandQueue 클래스를 이용한다.
 * Unix Domain Socket, TCP 서버로 subscriber 연결 요청을 받아 연결 완료 후 구독 요청을 받아 구독 응답을 보낸다.
 * 구독 요청을 받으면 구독 응답을 보내고, 구독 응답을 받으면 구독 요청을 받은 클라이언트에 대한 정보를 저장한다.
 * 리커버리 처리는 별도의 쓰레드 풀을 이용하여 처리한다.
 * Simpublisher 는 threaded 옵션을 두어 개별 PubConn 을 별도의 쓰레드로 기동할지 단일 스레드로 기동할지 선택 할 수 있다.
 * publish 요청을 받으면 PublishCommand를 throw 한다.
 * PubClient : 
 *   연결 종료 시 RemoveClientCommand를 throw 한다.
 *   에러 발생 시 ErrorCommand를 throw 한다.
 *   구독 요청을 받으면 _publisher에 ...
 *   리커버리 요청을 받으면 _publisher에 RecoveryRequestCommand를 throw 한다.
 * Publisher :
 *   연결 요청을 수신하면 PubClient 를 새로 만들고 _clients 에 저장한다.
 *   새로운 PubClient는 threaded == true 인 경우는 별도의 struct event_base를 만들고, false 인 경우는 publisher의 _libevent_base를 사용한다.
 *   새로운 PubClient는 publisher의 _clients 에 저장한다.
 *   publish 가 실행되면  PublishCommand를 ONLINE 상태의 PubClient 에 전송한다.
 *   전송 후 데이터를 _message_db에 저장하고, _publisher_sequence_record에 일련번호 update 한다.
 *   RecoveryRequestCommand를 받으면 recovery thread pool에 해당 요청을 추가한다.
 *   RemoveClientCommand를 받으면 _clients 에서 해당 클라이언트를 제거한다.
 *   ErrorCommand를 받으면 에러 처리를 한다.
 *   RecoveryCompleteCommand를 받으면 리커버리 완료 처리를 한다.
 */
class SimplePublisher {

private:
    uint32_t _publisher_id;
    std::string _publisher_name;
    
    // Unix Domain Socket
    struct evconnlistener* _unix_listener;
    std::string _unix_socket_path;
    
    // TCP Socket
    struct evconnlistener* _tcp_listener;
    std::string _tcp_address;
    int _tcp_port;

    struct event_base* _libevent_base;  // libevent 기본 이벤트 루프
    EventBase* _unix_event_base;         // EventBase wrapper (unix socket handler)
    EventBase* _tcp_event_base;         // EventBase wrapper (tcp socket handler)
    Protocol* _protocol;                // 프로토콜 인스턴스(TOPIC MSG 파싱용)

    SequenceStorage* _sequence_storage;
    PublisherSequenceRecord* _publisher_sequence_record;    // 현재 publisher가 publish중인 topic 별 sequence 정보

    // Data storage (DB_SAM)
    std::unique_ptr<DB_SAM> _message_db;
    std::string _db_path;

    std::unique_ptr<CommandQueue> _command_queue;

    std::map<int, std::unique_ptr<PubClient>> _clients;     // fd -> PubClient

    // Threaded option
    bool _threaded;
    std::vector<std::thread> _client_threads;

    // Recovery Thread Pool
    std::vector<std::thread> recovery_threads_;
    std::atomic<bool> recovery_threads_running_{false};
    static constexpr int MAX_RECOVERY_THREADS = 3;

    // Thread synchronization
    mutable std::mutex _clients_mutex;
    mutable std::mutex _recovery_queue_mutex;
    std::condition_variable _recovery_queue_cv;
    std::queue<std::unique_ptr<RecoveryRequestCommand>> _recovery_queue;
    std::atomic<bool> _shutdown{false};

public:
    SimplePublisher(struct event_base* shared_event_base);
    ~SimplePublisher();

    void set_unix_socket_path(const std::string& path);
    void set_tcp_address(const std::string& address, int port);
    void set_sequence_storage(SequenceStorage* sequence_storage);

    // Server startup
    bool start_unix(const std::string& socket_path);
    bool start_tcp(const std::string& ip, int port);
    bool start_both(const std::string& unix_path, const std::string& tcp_ip, int tcp_port);

    void handle_accept(int fd, struct sockaddr* addr, int len);
    void handle_error(char* data, int size);

    // Message publishing (Lock-Free)
    void publish(DataTopic topic, const char* data, size_t size);

    // Threading control
    void set_threaded(bool threaded) { _threaded = threaded; }
    bool get_threaded() const { return _threaded; }

    // Recovery Thread Pool
    void start_recovery_threads();
    void stop_recovery_threads();
    void recovery_worker_thread();
    void enqueue_recovery_request(std::unique_ptr<RecoveryRequestCommand> cmd);

    // Client thread management
    void run_client_thread(PubClient* client, int fd);
    void setup_client_sync(std::unique_ptr<PubClient> client, int fd);
    
};

#endif