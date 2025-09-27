#include "SimplePublisher.h"
#include "PubClient.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace SimplePubSub;

SimplePublisher::SimplePublisher(struct event_base* shared_event_base)
    : _publisher_id(0), _publisher_name("SimplePublisher"),
      _unix_listener(nullptr), _tcp_listener(nullptr),
      _tcp_port(0), _libevent_base(shared_event_base),
      _unix_event_base(nullptr), _tcp_event_base(nullptr),
      _sequence_storage(nullptr), _publisher_sequence_record(nullptr),
      _db_path("./publisher_data"), _threaded(false) {
    
    _protocol = new PubSubTopicProtocol();
    _command_queue = std::make_unique<CommandQueue>();
    
    // Initialize publisher sequence record with default values
    _publisher_sequence_record = new PublisherSequenceRecord(_publisher_name, _publisher_id, 
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() / 86400 + 19700101); // yyyymmdd format
    
    // Initialize message database
    _message_db = std::make_unique<DB_SAM>(_db_path);
    
    std::cout << "SimplePublisher initialized (threaded mode: " << (_threaded ? "ON" : "OFF") << ")" << std::endl;
}

SimplePublisher::~SimplePublisher() {
    std::cout << "SimplePublisher destructor started" << std::endl;
    
    // Signal shutdown
    _shutdown = true;
    
    // Stop recovery threads first
    if (recovery_threads_running_) {
        stop_recovery_threads();
    }
    
    // Stop listeners
    if (_unix_listener) {
        evconnlistener_free(_unix_listener);
    }
    if (_tcp_listener) {
        evconnlistener_free(_tcp_listener);
    }
    
    // Clean up clients (thread-safe)
    {
        std::lock_guard<std::mutex> lock(_clients_mutex);
        std::cout << "Cleaning up " << _clients.size() << " clients" << std::endl;
        _clients.clear();
    }
    
    // Wait for any remaining client threads to finish
    // Note: client threads are detached, so we can't join them
    // This is by design for graceful shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Clean up event bases
    delete _unix_event_base;
    delete _tcp_event_base;
    
    // Clean up protocol
    delete _protocol;
    
    // Clean up sequence record
    delete _publisher_sequence_record;
    
    // Message DB and Command queue will be cleaned up automatically by unique_ptr
    
    std::cout << "SimplePublisher destructor completed" << std::endl;
}

void SimplePublisher::set_unix_socket_path(const std::string& path) {
    _unix_socket_path = path;
}

void SimplePublisher::set_tcp_address(const std::string& address, int port) {
    _tcp_address = address;
    _tcp_port = port;
}

void SimplePublisher::set_sequence_storage(SequenceStorage* sequence_storage) {
    _sequence_storage = sequence_storage;
}

bool SimplePublisher::start_unix(const std::string& socket_path) {
    _unix_socket_path = socket_path;
    
    // 팩토리 패턴으로 Unix Domain Socket용 EventBase 생성
    _unix_event_base = createEventBase("unix_domain_socket", _libevent_base, false);
    if (!_unix_event_base) {
        std::cerr << "Failed to create unix event base" << std::endl;
        return false;
    }
    _unix_event_base->setBase(_libevent_base);
    
    // Accept 콜백 등록 - 새로운 클라이언트 연결 시 handle_accept 호출
    _unix_event_base->setAcceptCallback([this](int fd, struct sockaddr* addr, int len) {
        handle_accept(fd, addr, len);
    });
    
    // 프로토콜 설정
    _unix_event_base->setProtocol(_protocol);
    
    // Unix socket listen 시작
    _unix_event_base->listen(socket_path);
    
    std::cout << "Unix socket listener started: " << socket_path << std::endl;
    return true;
}

bool SimplePublisher::start_tcp(const std::string& ip, int port) {
    _tcp_address = ip;
    _tcp_port = port;
    
    // 팩토리 패턴으로 TCP Socket용 EventBase 생성  
    _tcp_event_base = createEventBase("tcp_socket", _libevent_base, false);
    if (!_tcp_event_base) {
        std::cerr << "Failed to create tcp event base" << std::endl;
        return false;
    }
    _tcp_event_base->setBase(_libevent_base);
    
    // Accept 콜백 등록 - 새로운 클라이언트 연결 시 handle_accept 호출
    _tcp_event_base->setAcceptCallback([this](int fd, struct sockaddr* addr, int len) {
        handle_accept(fd, addr, len);
    });
    
    // 프로토콜 설정
    _tcp_event_base->setProtocol(_protocol);
    
    // TCP socket listen 시작
    std::string tcp_address = ip + ":" + std::to_string(port);
    _tcp_event_base->listen(tcp_address);
    
    std::cout << "TCP socket listener started: " << tcp_address << std::endl;
    return true;
}

bool SimplePublisher::start_both(const std::string& unix_path, const std::string& tcp_ip, int tcp_port) {
    if (!start_unix(unix_path)) {
        return false;
    }
    if (!start_tcp(tcp_ip, tcp_port)) {
        return false;
    }
    return true;
}

void SimplePublisher::handle_accept(int fd, struct sockaddr* addr, int len) {
    std::cout << "SimplePublisher::handle_accept - New client connected, fd: " << fd << std::endl;
    
    // Create new PubClient for this connection
    auto client = std::make_unique<PubClient>(fd, addr, len);
    if (!client) {
        std::cerr << "Failed to create PubClient for fd: " << fd << std::endl;
        close(fd);
        return;
    }
    
    // Set initial client status
    client->set_status(CLIENT_CONNECTED);
    client->set_publisher(this);
    
    if (_threaded) {
        std::cout << "Creating separate thread for client fd: " << fd << std::endl;
        
        // Release ownership from unique_ptr since thread will take ownership
        PubClient* client_raw = client.release();
        
        // Create and detach thread for this client
        std::thread client_thread([this, client_raw, fd]() {
            run_client_thread(client_raw, fd);
        });
        client_thread.detach();
        
    } else {
        std::cout << "Setting up client in single-threaded mode for fd: " << fd << std::endl;
        setup_client_sync(std::move(client), fd);
    }
    
    std::cout << "Client connection handling initiated for fd: " << fd << std::endl;
}

void SimplePublisher::handle_error(char* data, int size) {
    std::cout << "SimplePublisher handle_error" << std::endl;
    std::cout << "data: " << data << std::endl;
    std::cout << "size: " << size << std::endl;
}

void SimplePublisher::publish(DataTopic topic, const char* data, size_t size) {
    std::cout << "SimplePublisher::publish() called - topic: " << topic_to_string(topic) << ", size: " << size << std::endl;
    std::cout << "Checking initialization: _publisher_sequence_record=" << (_publisher_sequence_record ? "OK" : "NULL") 
              << ", _message_db=" << (_message_db ? "OK" : "NULL") << std::endl;
    if (!_publisher_sequence_record || !_message_db) {
        std::cerr << "Publisher not properly initialized - EARLY RETURN!" << std::endl;
        return;
    }
    std::cout << "Publisher initialization check passed, continuing..." << std::endl;
    
    // 1. Update sequence numbers in publisher_sequence_record
    uint32_t current_topic_seq = _publisher_sequence_record->get_topic_sequence(topic);
    uint32_t new_topic_seq = current_topic_seq + 1;
    uint32_t new_global_seq = _publisher_sequence_record->all_topics_sequence + 1;
    
    _publisher_sequence_record->set_topic_sequence(new_global_seq, topic, new_topic_seq);
    
    // 2. Create TopicMessage structure
    size_t msg_size = sizeof(TopicMessage) + size;
    std::vector<char> msg_buffer(msg_size);
    TopicMessage* topic_msg = reinterpret_cast<TopicMessage*>(msg_buffer.data());
    
    topic_msg->magic = MAGIC_TOPIC_MSG;
    topic_msg->topic = topic;
    topic_msg->global_seq = new_global_seq;
    topic_msg->topic_seq = new_topic_seq;
    topic_msg->timestamp = get_current_timestamp();
    topic_msg->data_size = static_cast<uint32_t>(size);
    memcpy(topic_msg->data, data, size);
    
    // 3. Store message in database
    if (!_message_db->put(msg_buffer.data(), msg_size, topic_msg->timestamp)) {
        std::cerr << "Failed to store message in database - continuing anyway" << std::endl;
        // Don't return here - continue to send to clients even if DB fails
    }
    
    // 4. Save sequence to storage
    if (_sequence_storage) {
        if (!_sequence_storage->save_sequences(*_publisher_sequence_record)) {
            std::cerr << "Failed to save sequence to storage" << std::endl;
        }
    }
    
    // 5. Send message to ONLINE clients (thread-safe)
    int online_clients = 0;
    {
        std::lock_guard<std::mutex> lock(_clients_mutex);
        std::cout << "publish() - Total clients in map: " << _clients.size() << std::endl;
        
        for (auto& [fd, client] : _clients) {
            std::cout << "Checking client fd=" << fd << ", status=" << client->get_status() << std::endl;
            if (client && client->get_status() == CLIENT_ONLINE) {
                std::cout << "Creating PublishCommand for ONLINE client " << client->get_client_id() << std::endl;
                // Create PublishCommand for this client
                auto publish_cmd = std::make_unique<PublishCommand>(
                    client.get(), static_cast<uint32_t>(topic), 
                    msg_buffer.data(), msg_size
                );
                
                // Throw command to client
                client->throwCommand(std::move(publish_cmd));
                online_clients++;
            }
        }
    }
    
    std::cout << "Published " << topic_to_string(topic) 
              << " message (global_seq=" << new_global_seq 
              << ", topic_seq=" << new_topic_seq 
              << ", size=" << size 
              << ") to " << online_clients << " clients" << std::endl;
}

void SimplePublisher::run_client_thread(PubClient* client, int fd) {
    std::cout << "Starting client thread for fd: " << fd << std::endl;
    
    // Create separate event_base for this client thread
    struct event_base* client_event_base = event_base_new();
    if (!client_event_base) {
        std::cerr << "Failed to create event_base for client fd: " << fd << std::endl;
        close(fd);
        return;
    }
    
    // Setup EventBase wrapper for this client
    EventBase* event_wrapper = createEventBase("client_thread", client_event_base, false);
    if (!event_wrapper) {
        std::cerr << "Failed to create EventBase wrapper for client fd: " << fd << std::endl;
        event_base_free(client_event_base);
        close(fd);
        return;
    }
    
    // Configure the event wrapper
    event_wrapper->setupBufferevent(fd);
    event_wrapper->setProtocol(_protocol);
    event_wrapper->setReadCallback([client](char* data, int size) {
        client->handle_incomming_messages(data, size);
    });
    
    // Set event base and processor for client
    client->set_event_base(event_wrapper);
    client->set_event_processor();
    
    // Thread-safe client registration
    {
        std::lock_guard<std::mutex> lock(_clients_mutex);
        _clients[fd] = std::unique_ptr<PubClient>(client);
    }
    
    std::cout << "Client thread " << fd << " starting event dispatch..." << std::endl;
    
    // Run the event loop (this will block until client disconnects)
    event_base_dispatch(client_event_base);
    
    std::cout << "Client thread " << fd << " event dispatch ended, cleaning up..." << std::endl;
    
    // Cleanup: remove client from map
    {
        std::lock_guard<std::mutex> lock(_clients_mutex);
        _clients.erase(fd);
    }
    
    // Cleanup resources
    delete event_wrapper;
    event_base_free(client_event_base);
    
    std::cout << "Client thread for fd: " << fd << " terminated" << std::endl;
}

void SimplePublisher::setup_client_sync(std::unique_ptr<PubClient> client, int fd) {
    std::cout << "Setting up client synchronously for fd: " << fd << std::endl;
    
    // Use publisher's main event_base for non-threaded mode
    struct event_base* client_event_base = _libevent_base;
    
    // Setup EventBase for this client to handle incoming messages
    EventBase* client_event_base_wrapper = createEventBase("unix_domain_socket", client_event_base, false);
    if (client_event_base_wrapper) {
        client_event_base_wrapper->setupBufferevent(fd);
        client_event_base_wrapper->setProtocol(_protocol);
        client_event_base_wrapper->setReadCallback([client_ptr = client.get()](char* data, int size) {
            client_ptr->handle_incomming_messages(data, size);
        });
        client->set_event_base(client_event_base_wrapper);
    }
    
    // Setup EventProcessor with shared event_base for command handling
    client->set_event_processor();
    
    // Store client in our client map (no locking needed in single-threaded mode)
    _clients[fd] = std::move(client);
    
    std::cout << "Client added successfully. Total clients: " << _clients.size() << std::endl;
}

void SimplePublisher::start_recovery_threads() {
    std::cout << "Starting recovery thread pool with " << MAX_RECOVERY_THREADS << " threads" << std::endl;
    
    recovery_threads_running_ = true;
    
    for (int i = 0; i < MAX_RECOVERY_THREADS; ++i) {
        recovery_threads_.emplace_back([this, i]() {
            std::cout << "Recovery worker thread " << i << " started" << std::endl;
            recovery_worker_thread();
            std::cout << "Recovery worker thread " << i << " terminated" << std::endl;
        });
    }
    
    std::cout << "Recovery thread pool started successfully" << std::endl;
}

void SimplePublisher::stop_recovery_threads() {
    std::cout << "Stopping recovery thread pool..." << std::endl;
    
    recovery_threads_running_ = false;
    _recovery_queue_cv.notify_all();
    
    for (auto& thread : recovery_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    recovery_threads_.clear();
    std::cout << "Recovery thread pool stopped" << std::endl;
}

void SimplePublisher::recovery_worker_thread() {
    while (recovery_threads_running_) {
        std::unique_ptr<RecoveryRequestCommand> cmd;
        
        // Wait for recovery requests
        {
            std::unique_lock<std::mutex> lock(_recovery_queue_mutex);
            _recovery_queue_cv.wait(lock, [this] {
                return !_recovery_queue.empty() || !recovery_threads_running_;
            });
            
            if (!recovery_threads_running_) {
                break;
            }
            
            if (!_recovery_queue.empty()) {
                cmd = std::move(_recovery_queue.front());
                _recovery_queue.pop();
            }
        }
        
        // Execute recovery command if we got one
        if (cmd) {
            std::cout << "Processing recovery request..." << std::endl;
            try {
                cmd->execute();
                std::cout << "Recovery request completed successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Recovery request failed: " << e.what() << std::endl;
            }
        }
    }
}

void SimplePublisher::enqueue_recovery_request(std::unique_ptr<RecoveryRequestCommand> cmd) {
    if (!cmd) {
        std::cerr << "Attempted to enqueue null recovery command" << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(_recovery_queue_mutex);
    _recovery_queue.push(std::move(cmd));
    _recovery_queue_cv.notify_one();
    
    std::cout << "Recovery request enqueued. Queue size: " << _recovery_queue.size() << std::endl;
}