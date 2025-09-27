#include "SimpleSubscriber.h"
#include <iostream>
#include <cstring>

SimpleSubscriber::SimpleSubscriber(struct event_base* shared_event_base)
    : _libevent_base(shared_event_base) {
    _protocol = new PubSubTopicProtocol();
    _subscription_mask = 0;
    _current_status = CLIENT_OFFLINE;
    _socket_handler = nullptr;
    _sequence_storage = nullptr;
    _publisher_sequence_record = new PublisherSequenceRecord();
}

SimpleSubscriber::~SimpleSubscriber() {
    if (_socket_handler) {
        delete _socket_handler;
    }
    if (_protocol) {
        delete _protocol;
    }
    if (_sequence_storage) {
        delete _sequence_storage;
    }
    if (_publisher_sequence_record) {
        delete _publisher_sequence_record;
    }
}

bool SimpleSubscriber::change_status(ClientStatus status) {
    _current_status = status;
    return true;
}

void SimpleSubscriber::set_address(SocketType socket_type, std::string address, int port) {
    _socket_type = socket_type;
    _address = address;
    _port = port;
}

void SimpleSubscriber::set_client_info(uint32_t id, const std::string& name, uint32_t pub_id, const std::string& pub_name) {
    _subscriber_id = id * 10000 + pub_id;
    _subscriber_name = name + "_" + pub_name;
}

void SimpleSubscriber::set_sequence_storage(SequenceStorage* sequence_storage) {
    _sequence_storage = sequence_storage;
    if(_sequence_storage_type == StorageType::FILE_STORAGE) {
        std::string seq_file = "sub_" + _subscriber_name +  get_publisher_name() + ".seq";
    } else {
        "./sequence_data/sub" + _subscriber_name + get_publisher_name() + "_sequences";
    }
}

bool SimpleSubscriber::init_sequence_storage(StorageType storage_type) {
    _sequence_storage_type = storage_type;
    if(_sequence_storage_type == StorageType::FILE_STORAGE) {
        std::string seq_file = "sub_" + _subscriber_name + ".seq";
        std::string storage_dir = "./data/sequence_data";
        _sequence_storage = new FileSequenceStorage(storage_dir, seq_file);
        _publisher_sequence_record = new PublisherSequenceRecord(get_publisher_name(), 0, 0);
    } else {
        std::string storage_path = "./sequence_data/sub" + _subscriber_name + "_sequences";
        _sequence_storage = new HashmasterSequenceStorage(storage_path);
    }
    _sequence_storage->initialize();

    // HashMaster storage의 경우 mmap 포인터를 직접 사용
    if(_sequence_storage_type == StorageType::HASHMASTER_STORAGE) {
        HashmasterSequenceStorage* hashmaster_storage = static_cast<HashmasterSequenceStorage*>(_sequence_storage);
        _publisher_sequence_record = hashmaster_storage->load_sequences_direct(get_publisher_name());
    } else {
        _sequence_storage->load_sequences(get_publisher_name(), _publisher_sequence_record);
    }
    if(_publisher_sequence_record == nullptr) {
        std::cerr << "Failed to load sequence record" << std::endl;
        return false;
    }
    return true;
}

bool SimpleSubscriber::connect() {
    std::cout << "Connecting to " << (_socket_type == UNIX_SOCKET ? "Unix socket" : "TCP socket") << ": " << _address << std::endl;
    
    if (_socket_handler) {
        delete _socket_handler;
        _socket_handler = nullptr;
    }
    
    std::string socket_type_str;
    if (_socket_type == UNIX_SOCKET) {
        socket_type_str = "unix_domain_socket";
    } else if (_socket_type == TCP_SOCKET) {
        socket_type_str = "tcp_socket";
    } else {
        std::cerr << "Unknown socket type" << std::endl;
        return false;
    }
    
    // EventBase wrapper 생성
    _socket_handler = createEventBase(socket_type_str, _libevent_base, false);
    if (!_socket_handler) {
        std::cerr << "Failed to create socket handler" << std::endl;
        return false;
    }
    
    // Protocol 및 콜백 설정
    _socket_handler->setProtocol(_protocol);
    _socket_handler->setReadCallback([this](char *data, int size) {
        handle_incomming_messages(data, size);
    });
    _socket_handler->setConnectCallback([this](char *data, int size) {
        handle_connected(data, size);
    });
    _socket_handler->setDisconnectCallback([this](char *data, int size) {
        handle_disconnected(data, size);
    });
    _socket_handler->setErrorCallback([this](char *data, int size) {
        handle_error(data, size);
    });
    
    // 서버에 연결 시도
    try {
        _socket_handler->connect(_address);
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void SimpleSubscriber::try_reconnect() {
    // Create a timer event for delayed reconnection
    struct timeval delay = {3, 0}; // 3 second delay
    event_base_once(_libevent_base, -1, EV_TIMEOUT,
        [](evutil_socket_t fd, short event, void *arg) {
            SimpleSubscriber* self = static_cast<SimpleSubscriber*>(arg);
            std::cout << "Attempting reconnection..." << std::endl;
            if (!self->connect()) {
                std::cout << "Reconnection failed, will retry in 1 second..." << std::endl;
                self->try_reconnect(); // 재귀적으로 재연결 시도
            }
        }, this, &delay);
}

void SimpleSubscriber::set_subscription_mask(uint32_t mask) {
    _subscription_mask = mask;
}

void SimpleSubscriber::set_topic_callback(TopicDataCallback callback) {
    _topic_callback = callback;
}

void SimpleSubscriber::handle_connected(char* data, int size) {
    std::cout << "Connected to publisher" << std::endl;
    change_status(CLIENT_CONNECTED);
    send_subscription_request();
}

void SimpleSubscriber::handle_disconnected(char* data, int size) {
    std::cout << "Disconnected from publisher" << std::endl;
    change_status(CLIENT_OFFLINE);
    try_reconnect();
}

void SimpleSubscriber::handle_error(char* data, int size) {
    std::cout << "Error occurred, will reconnect in 1 second..." << std::endl;
    change_status(CLIENT_OFFLINE);
    try_reconnect();
}

int SimpleSubscriber::validate_sequence(DataTopic topic, uint32_t sequence) {
    uint32_t current_seq = _publisher_sequence_record->get_topic_sequence(topic);
    if(sequence == (current_seq + 1)) {
        return 0;
    } else if(sequence <= current_seq) {
        return 2;
    }
    return 1;
}

void SimpleSubscriber::handle_incomming_messages(char* data, int size) {
    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(uint32_t));
    std::cout << "Received message - magic: 0x" << std::hex << magic << std::dec << " (" << magic << "), size: " << size << std::endl;
    std::cout << "Expected magics - TOPIC_MSG: 0x" << std::hex << MAGIC_TOPIC_MSG << ", SUB_OK: 0x" << MAGIC_SUB_OK << std::dec << std::endl;
    
    switch(magic) {
        case MAGIC_TOPIC_MSG:
            std::cout << "Handling TOPIC_MSG" << std::endl;
            handle_topic_message(*reinterpret_cast<const TopicMessage*>(data));
            break;
        case MAGIC_SUB_OK:
            std::cout << "Handling SUBSCRIPTION_RESPONSE" << std::endl;
            handle_subscription_response(*reinterpret_cast<const SubscriptionResponse*>(data));
            break;
        case MAGIC_RECOVERY_RES:
            std::cout << "Handling RECOVERY_RESPONSE" << std::endl;
            handle_recovery_response(*reinterpret_cast<const RecoveryResponse*>(data));
            break;
        case MAGIC_RECOVERY_CMP:
            std::cout << "Handling RECOVERY_COMPLETE" << std::endl;
            handle_recovery_complete(*reinterpret_cast<const RecoveryComplete*>(data));
            break;
        default:
            std::cout << "Unknown message type: 0x" << std::hex << magic << std::dec << std::endl;
            break;
    }
}

void SimpleSubscriber::handle_topic_message(const TopicMessage& topic_message) {
    std::cout << "Received topic message - topic: " << topic_message.topic 
              << ", global_seq: " << topic_message.global_seq 
              << ", topic_seq: " << topic_message.topic_seq
              << ", data_size: " << topic_message.data_size 
              << ", currnet topic seq: " << _publisher_sequence_record->get_topic_sequence(topic_message.topic) << std::endl;
    
    int result = validate_sequence(topic_message.topic, topic_message.topic_seq);
    if(result == 1) {
        std::cout << "Sequence lost" << std::endl;
        if(_current_status == CLIENT_ONLINE) {
            change_status(CLIENT_RECOVERY_NEEDED);
            send_recovery_request();
        } else {
            std::cout << "######\n#\tWarning: Client is not online, skip recovery" << std::endl;
        }
        return;
    } else if(result == 2) {
        std::cout << "Sequence duplicate, skip message" << std::endl;
        return;
    }

    _publisher_sequence_record->set_topic_sequence(topic_message.global_seq, topic_message.topic, topic_message.topic_seq);
    if(_sequence_storage) {
        _sequence_storage->save_sequences(*_publisher_sequence_record);
    }
    
    // 콜백 함수 호출
    if (_topic_callback) {
        _topic_callback(static_cast<DataTopic>(topic_message.topic), 
                       topic_message.data, topic_message.data_size);
    }
}

void SimpleSubscriber::handle_subscription_response(const SubscriptionResponse& subscription_response) {
    std::cout << "Subscription response - result: " << subscription_response.result << std::endl;
    
    if (subscription_response.result == 0) {
        change_status(CLIENT_RECOVERY_NEEDED);
        send_recovery_request();
    }
}

void SimpleSubscriber::handle_recovery_response(const RecoveryResponse& recovery_response) {
    std::cout << "Recovery response - result: " << recovery_response.result 
              << ", start_seq: " << recovery_response.start_seq 
              << ", end_seq: " << recovery_response.end_seq 
              << ", total_messages: " << recovery_response.total_messages << std::endl;
    
    if (recovery_response.result == 0) {
        change_status(CLIENT_RECOVERING);
    }
}

void SimpleSubscriber::handle_recovery_complete(const RecoveryComplete& recovery_complete) {
    std::cout << "Recovery complete - total_sent: " << recovery_complete.total_sent << std::endl;
    change_status(CLIENT_ONLINE);
}

bool SimpleSubscriber::send_subscription_request() {
    if (!_socket_handler) {
        std::cerr << "Socket handler not available" << std::endl;
        return false;
    }
    
    SubscriptionRequest subscription_request;
    subscription_request.magic = MAGIC_SUBSCRIBE;
    subscription_request.client_id = _subscriber_id;
    subscription_request.client_name[0] = '\0';
    strncpy(subscription_request.client_name, _subscriber_name.c_str(), sizeof(subscription_request.client_name) - 1);
    subscription_request.topic_mask = _subscription_mask;
    
    std::cout << "Sending subscription request" << std::endl;
    _socket_handler->trySend(&subscription_request, sizeof(subscription_request));
    return true;
}

bool SimpleSubscriber::send_recovery_request() {
    if (!_socket_handler) {
        std::cerr << "Socket handler not available" << std::endl;
        return false;
    }
    
    RecoveryRequest recovery_request;
    recovery_request.magic = MAGIC_RECOVERY_REQ;
    recovery_request.client_id = _subscriber_id;
    recovery_request.topic_mask = _subscription_mask;
    recovery_request.last_seq = _publisher_sequence_record->get_topic_sequence(DataTopic::ALL_TOPICS);
    
    std::cout << "Sending recovery request" << std::endl;
    _socket_handler->trySend(&recovery_request, sizeof(recovery_request));
    return true;
}

void SimpleSubscriber::stop() {
    _current_status = CLIENT_OFFLINE;
    if (_socket_handler) {
        delete _socket_handler;
        _socket_handler = nullptr;
    }
}
