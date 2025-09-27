#ifndef SIMPLE_SUBSCRIBER_H
#define SIMPLE_SUBSCRIBER_H

#include "Common.h"
#include "PubSubTopicProtocol.h"
#include "../eventBase/EventBase.h"
#include "SequenceStorage.h"
#include "FileSequenceStorage.h"
#include "HashmasterSequenceStorage.h"

using namespace SimplePubSub;

typedef std::function<void(DataTopic topic, const char* data, int size)> TopicDataCallback;



/*
* STATUS: CONNECTED, RECOVERY_NEEDED, RECOVERING, CATCHING_UP, ONLINE, OFFLINE
* CONNECTED: 서버 연결 완료 , 서버 연결이 되면 서버에 구독 요청을 보내고 구독 응답을 받음, RECOVERY_NEEDED 상태로 변경
* RECOVERY_NEEDED: publisher에 복구요청을 보내고 복구 응답을 받고 RECOVERING 상태로 변경
* RECOVERING: 복구완료 메시지를 받으면 ONLINE 상태로 변경
* ONLINE: 실시간 메시지를 받으면 콜백 함수를 호출, 일련번호 누락 감지 시 RECOVERY_NEEDED 상태로 변경
* OFFLINE: 서버와 연결 끊어짐, 재연결 시도 후 CONNECTED 상태로 변경
* ONLINE 또는 RECOVERING 상태에서 메시지를 받으면 
*   valid sequence 인지 확인하고 _topic_callback 함수를 호출
*   valid sequence이면 _publisher_sequence_record 일련번호등 관련 정보 update, 
*   일련번호 누락이면 RECOVERY_NEEDED 상태로 변경 (publisher에 복구요청을 보내고 복구 응답을 받고 RECOVERING 상태로 변경)
*   DUPLICATE_MESSAGE 이면 무시 (DEBUG_LOG 출력)
*/
class SimpleSubscriber {
private:
    uint32_t _subscriber_id;
    std::string _subscriber_name;

    std::string _publisher_name;
    SocketType _socket_type;
    std::string _address;
    int _port;

    // int _reconnect_interval;    나중에 필요하면 추가, 현재는 연결이 끊기면 자동 재연결 처리
    // int _max_reconnect_count;

    ClientStatus _current_status;
    uint32_t _subscription_mask;

    StorageType _sequence_storage_type;
    SequenceStorage* _sequence_storage;
    PublisherSequenceRecord* _publisher_sequence_record;    // 현재 구독중인 topic 별 sequence 정보

    struct event_base* _libevent_base;  // libevent 기본 이벤트 루프
    EventBase* _socket_handler;         // EventBase wrapper (unix/tcp socket handler)
    Protocol* _protocol;                // 프로토콜 인스턴스(TOPIC MSG 파싱용)

    TopicDataCallback _topic_callback;
    
public:
    SimpleSubscriber(struct event_base* shared_event_base);
    ~SimpleSubscriber();

    bool change_status(ClientStatus status);

    void set_publisher_name(const std::string& publisher_name) {_publisher_name = publisher_name;}
    inline std::string get_publisher_name() const {return _publisher_name;}

    void set_address(SocketType socket_type, std::string address, int port=0);
    void set_subscription_mask(uint32_t mask);
    void set_topic_callback(TopicDataCallback callback);
    void set_client_info(uint32_t id, const std::string& name, uint32_t pub_id, const std::string& pub_name);
    void set_sequence_storage(SequenceStorage* sequence_storage);
    bool init_sequence_storage(StorageType storage_type);

    /* 서버 연결 시도, _socket_type 에 따라 소켓 생성 및 연결 */
    bool connect();
    /* connect 실패 시 _reconnect_interval 후 재연결 시도 (최대 _max_reconnect_count 번) */
    void try_reconnect();
    void stop();

    bool send_subscription_request();
    bool send_recovery_request();

    void handle_connected(char* data, int size);
    void handle_disconnected(char* data, int size);
    void handle_error(char* data, int size);

    /* 일련번호 검증  0: 정상, 1: 누락, 2: 중복 */
    int validate_sequence(DataTopic topic, uint32_t sequence);
    // Message processing
    void handle_incomming_messages(char* data, int size);   
    /* TOPIC MSG 파싱 처리 */
    void handle_topic_message(const TopicMessage& topic_message);
    /* SUBSCRIPTION RESPONSE 처리 */
    void handle_subscription_response(const SubscriptionResponse& subscription_response);
    /* RECOVERY RESPONSE 처리 */
    void handle_recovery_response(const RecoveryResponse& recovery_response);
    /* RECOVERY COMPLETE 처리 */
    void handle_recovery_complete(const RecoveryComplete& recovery_complete);

   
};

#endif // SIMPLE_SUBSCRIBER_H