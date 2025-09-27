#include "SimplePublisherV2.h"
#include <cstring>
#include <cerrno>
#include <chrono>
#include <signal.h>
#include <pthread.h>

SimplePublisherV2::SimplePublisherV2(event_base *main_base) :
        _main_base(main_base),
        _listener(nullptr),
        _use_unix(true),
        _publisher_id(0),
        _sequence_storage(nullptr)
{
    pipe(_main_notify_pipe);
    _main_notify_event = event_new(_main_base, _main_notify_pipe[0], EV_READ | EV_PERSIST,
                                    [](evutil_socket_t fd, short, void* arg){
                                        static_cast<SimplePublisherV2*>(arg)->main_notify_cb(fd);
                                    }, this);
    event_add(_main_notify_event, nullptr);
    // _publisher_sequence_record = new PublisherSequenceRecord(); // move to set_sequence_storage
}
SimplePublisherV2::~SimplePublisherV2() {
    stop();
    if (_main_notify_event) event_free(_main_notify_event);
    close(_main_notify_pipe[0]);
    close(_main_notify_pipe[1]);
    if (_sequence_storage_type == StorageType::FILE_STORAGE && _publisher_sequence_record) {
        delete _publisher_sequence_record;
        _publisher_sequence_record = nullptr;
    }
    if (_db) {
        _db->close();
        _db.reset();
    }
}    

void SimplePublisherV2::set_address(SocketType socket_type, std::string address, int port) {
    if (socket_type == UNIX_SOCKET) {
        _unix_path = address;
        _use_unix = true;
    } else if (socket_type == TCP_SOCKET) {
        _tcp_address = address;
        _tcp_port = port;
        _use_unix = false;
    }
}

void SimplePublisherV2::set_unix_path(const std::string &path) { _unix_path = path; _use_unix = true; }
void SimplePublisherV2::set_tcp_address(const std::string &address) { _tcp_address = address; _use_unix = false; }
void SimplePublisherV2::set_tcp_port(uint16_t p) { _tcp_port = p; _use_unix = false; }

void SimplePublisherV2::set_sequence_storage(SequenceStorage* sequence_storage) {
    _sequence_storage = sequence_storage;
}

bool SimplePublisherV2::init_sequence_storage(StorageType storage_type) {
    _sequence_storage_type = storage_type;
    if(_sequence_storage_type == StorageType::FILE_STORAGE) {
        std::string seq_file = get_publisher_name() + ".seq";
        std::string storage_dir = "./data/sequence_data";
        _sequence_storage = new FileSequenceStorage(storage_dir, seq_file);
        _publisher_sequence_record = new PublisherSequenceRecord(get_publisher_name(), 0, 0);
    } else {
        std::string storage_path = "./sequence_data/" + get_publisher_name() + "_sequences";
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

bool SimplePublisherV2::init_database(const std::string& db_path) {
    _db_path = db_path;
    if( _db_path.empty() ) {
        _db = std::make_unique<Memory_SAM>();
    } else {
        _db = std::make_unique<DB_SAM>(_db_path);
    }
    if(!_db->open()) {
        std::cerr << "Failed to open database" << std::endl;
        return false;
    }
    std::cout << "Database initialized successfully" << std::endl;
    return true;
}

bool SimplePublisherV2::start(size_t recovery_thread_count) {
    if (!_main_base) return false;
    if (_use_unix) {
        std::cout << "Starting Unix socket server on: " << _unix_path << std::endl;
        sockaddr_un sa; memset(&sa,0,sizeof(sa));
        sa.sun_family = AF_UNIX;
        strncpy(sa.sun_path, _unix_path.c_str(), sizeof(sa.sun_path)-1);

        // Remove existing socket file
        ::unlink(_unix_path.c_str());
        std::cout << "Removed existing socket file" << std::endl;
         _listener = evconnlistener_new_bind(_main_base, static_accept_cb, this,
                                            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
                                            (sockaddr*)&sa,sizeof(sa));
        if (!_listener) {
            std::cerr << "Failed to create evconnlistener for Unix socket" << std::endl;
            return false;
        }
        std::cout << "Created evconnlistener successfully" << std::endl;

    } else {
        sockaddr_in sin; memset(&sin,0,sizeof(sin));
        sin.sin_family=AF_INET; sin.sin_addr.s_addr=htonl(0); sin.sin_port=htons(_tcp_port);
        _listener = evconnlistener_new_bind(_main_base, static_accept_cb, this,
                                            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
                                            (sockaddr*)&sin,sizeof(sin));
    }
    if (!_listener) {
        std::cerr << "Failed to create listener" << std::endl;
        return false;
    }

    // 개선된 복구 워커 생성
    std::cout << "Creating " << recovery_thread_count << " recovery workers with improved safety..." << std::endl;
    
    for (size_t i = 0; i < recovery_thread_count; ++i) {
        auto *w = new RecoveryWorker;
        w->base = event_base_new();
        int p[2]; pipe(p);
        w->notify_pipe_r=p[0]; w->notify_pipe_w=p[1];
        w->notify_event = event_new(w->base,w->notify_pipe_r,EV_READ|EV_PERSIST,
                                    [](evutil_socket_t,short,void* arg){
                                        ((RecoveryWorker*)arg)->on_notify();
                                    }, w);
        event_add(w->notify_event,nullptr);
        w->running=true;
        w->th=std::thread([w](){ event_base_dispatch(w->base); });
        _workers.push_back(w);
    }
    /*
    for (size_t i = 0; i < recovery_thread_count; ++i) {
        try {
            auto *w = new RecoveryWorker;

            // 멤버 변수 초기화
            w->base = nullptr;
            w->notify_event = nullptr;
            w->notify_pipe_r = -1;
            w->notify_pipe_w = -1;
            w->running = false;

            // event_base 생성
            w->base = event_base_new();
            if (!w->base) {
                std::cerr << "Failed to create event_base for recovery worker " << i << std::endl;
                delete w;
                continue;
            }

            // pipe 생성
            int p[2];
            if (pipe(p) < 0) {
                std::cerr << "Failed to create pipe for recovery worker " << i << ": " << strerror(errno) << std::endl;
                event_base_free(w->base);
                delete w;
                continue;
            }
            w->notify_pipe_r = p[0];
            w->notify_pipe_w = p[1];

            // pipe를 non-blocking으로 설정
            evutil_make_socket_nonblocking(w->notify_pipe_r);
            evutil_make_socket_nonblocking(w->notify_pipe_w);

            // notify event 생성
            w->notify_event = event_new(w->base, w->notify_pipe_r, EV_READ | EV_PERSIST,
                                        [](evutil_socket_t fd, short events, void* arg) {
                                            try {
                                                auto* worker = static_cast<RecoveryWorker*>(arg);
                                                if (worker && worker->running) {
                                                    worker->on_notify();
                                                }
                                            } catch (const std::exception& e) {
                                                std::cerr << "Exception in recovery worker notify callback: " << e.what() << std::endl;
                                            } catch (...) {
                                                std::cerr << "Unknown exception in recovery worker notify callback" << std::endl;
                                            }
                                        }, w);

            if (!w->notify_event) {
                std::cerr << "Failed to create notify event for recovery worker " << i << std::endl;
                close(w->notify_pipe_r);
                close(w->notify_pipe_w);
                event_base_free(w->base);
                delete w;
                continue;
            }

            // 이벤트 추가
            if (event_add(w->notify_event, nullptr) < 0) {
                std::cerr << "Failed to add notify event for recovery worker " << i << std::endl;
                event_free(w->notify_event);
                close(w->notify_pipe_r);
                close(w->notify_pipe_w);
                event_base_free(w->base);
                delete w;
                continue;
            }

            // 이제 스레드 시작 전에 running을 true로 설정
            w->running = true;

            // 개선된 스레드 생성 - exception handling과 더 안전한 event loop
            w->th = std::thread([w, i]() {
                try {
                    std::cout << "Recovery worker thread " << i << " started" << std::endl;

                    // 스레드별 signal mask 설정 (SIGPIPE 무시)
                    sigset_t set;
                    sigemptyset(&set);
                    sigaddset(&set, SIGPIPE);
                    pthread_sigmask(SIG_BLOCK, &set, nullptr);

                    while (w->running.load()) {
                        // 더 짧은 타임아웃으로 응답성 향상
                        struct timeval timeout = {0, 500000}; // 0.5초

                        // event_base_dispatch 대신 event_base_loop 사용
                        int result = event_base_loop(w->base, EVLOOP_ONCE | EVLOOP_NONBLOCK);

                        if (result < 0) {
                            std::cerr << "Recovery worker " << i << " event loop error: " << strerror(errno) << std::endl;
                            break;
                        }

                        // CPU 사용량 줄이기 위한 짧은 sleep
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }

                    std::cout << "Recovery worker thread " << i << " ended gracefully" << std::endl;

                } catch (const std::exception& e) {
                    std::cerr << "Exception in recovery worker thread " << i << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception in recovery worker thread " << i << std::endl;
                }
            });

            _workers.push_back(w);
            std::cout << "Recovery worker " << i << " created successfully" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Failed to create recovery worker " << i << ": " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error creating recovery worker " << i << std::endl;
        }
    }
    */
    std::cout << "SimplePublisherV2 started successfully with " << recovery_thread_count << " recovery workers" << std::endl;
    std::cout << "Publisher is ready and waiting for connections..." << std::endl;
    return true;
}

bool SimplePublisherV2::start_both(const std::string& unix_path, const std::string& tcp_host, int tcp_port) {
    set_unix_path(unix_path);
    set_tcp_address(tcp_host);
    set_tcp_port(tcp_port);
    return start();
}

size_t SimplePublisherV2::get_client_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(_clients_mu));
    return _clients.size();
}

uint32_t SimplePublisherV2::get_current_sequence() const {
    if (_publisher_sequence_record) {
        return _publisher_sequence_record->all_topics_sequence;
    }
    return 0;
}

void SimplePublisherV2::stop() {
    std::cout << "SimplePublisherV2::stop - Starting graceful shutdown..." << std::endl;

    // 1. 먼저 새로운 연결 수락 중지
    if (_listener) {
        evconnlistener_free(_listener);
        _listener = nullptr;
        std::cout << "Listener closed" << std::endl;
    }

    // 2. 복구 워커 스레드들을 안전하게 종료
    std::cout << "Shutting down " << _workers.size() << " recovery workers..." << std::endl;
    // std::cout << "일단 복구 thread 처리는 나중에... SKIIIP" << std::endl;
    for(auto w:_workers) {
        if (!w) continue;
        w->running=false;
        event_base_loopbreak(w->base);
        char c='q'; write(w->notify_pipe_w,&c,1);
        if (w->th.joinable()) w->th.join();
        if (w->notify_event) event_free(w->notify_event);
        close(w->notify_pipe_r); close(w->notify_pipe_w);
        if (w->base) event_base_free(w->base);
        delete w;
    }
    _workers.clear();

    /*
    for (size_t i = 0; i < _workers.size(); ++i) {
        auto* w = _workers[i];
        if (!w) continue;

        try {
            std::cout << "Stopping recovery worker " << i << "..." << std::endl;

            // running 플래그를 false로 설정하여 워커 스레드 루프 종료
            w->running.store(false);

            // event_base에 loopbreak 신호 전송
            if (w->base) {
                event_base_loopbreak(w->base);
            }

            // 워커에게 알림 신호 전송 (안전하게)
            if (w->notify_pipe_w >= 0) {
                char c = 'q';
                ssize_t written = write(w->notify_pipe_w, &c, 1);
                if (written < 0) {
                    std::cerr << "Failed to write to worker " << i << " notify pipe: " << strerror(errno) << std::endl;
                }
            }

            // 스레드 종료 대기 (타임아웃 포함)
            if (w->th.joinable()) {
                std::cout << "Waiting for worker thread " << i << " to finish..." << std::endl;

                // 비동기적으로 스레드가 종료되기를 기다림
                auto start = std::chrono::steady_clock::now();
                const auto timeout = std::chrono::seconds(5);

                bool joined = false;
                while (!joined && (std::chrono::steady_clock::now() - start) < timeout) {
                    if (w->th.joinable()) {
                        try {
                            w->th.join();
                            joined = true;
                            std::cout << "Worker thread " << i << " joined successfully" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Exception while joining worker thread " << i << ": " << e.what() << std::endl;
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (!joined) {
                    std::cerr << "Worker thread " << i << " did not terminate within timeout" << std::endl;
                    // 강제 종료는 위험하므로 로그만 남김
                }
            }

            // 이벤트와 파이프 정리
            if (w->notify_event) {
                event_free(w->notify_event);
                w->notify_event = nullptr;
            }

            if (w->notify_pipe_r >= 0) {
                close(w->notify_pipe_r);
                w->notify_pipe_r = -1;
            }
            if (w->notify_pipe_w >= 0) {
                close(w->notify_pipe_w);
                w->notify_pipe_w = -1;
            }

            if (w->base) {
                event_base_free(w->base);
                w->base = nullptr;
            }

            delete w;
            std::cout << "Recovery worker " << i << " cleaned up successfully" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "Exception while stopping worker " << i << ": " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception while stopping worker " << i << std::endl;
        }
    }
    _workers.clear();
    */
    // 3. 클라이언트 연결 정리
    std::cout << "Cleaning up client connections..." << std::endl;
    {
        std::lock_guard<std::mutex> g(_clients_mu);
        for (auto& p : _clients) {
            if (p.second && p.second->bev) {
                bufferevent_free(p.second->bev);
                p.second->bev = nullptr;
            }
        }
        _clients.clear();
    }

    std::cout << "SimplePublisherV2::stop - Shutdown complete" << std::endl;
}

void SimplePublisherV2::publish(DataTopic topic, const char* data,size_t size) {
    if (!_publisher_sequence_record || !_db) {
        std::cerr << "Publisher sequence record or _db not initialized" << std::endl;
        std::cerr << "Publisher not properly initialized - EARLY RETURN!" << std::endl;
        return;
    }

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
    if (!_db->put(msg_buffer.data(), msg_size)) {
        std::cerr << "Failed to store message in database - continuing anyway" << std::endl;
        // Don't return here - continue to send to clients even if DB fails
    }

    // 4. Save sequence to storage
    if (_sequence_storage) {
        if (!_sequence_storage->save_sequences(*_publisher_sequence_record)) {
            std::cerr << "Failed to save sequence to storage" << std::endl;
        }
    }

    // 5. Send message to ONLINE clients
    std::vector<std::shared_ptr<ClientInfo>> send_list;
    {
        std::lock_guard<std::mutex> g(_clients_mu);
        for(auto&kv:_clients){
            auto ci=kv.second;
            std::lock_guard<std::mutex> cg(ci->mu);
            if(!(ci->topic_mask&(1u<<topic))) continue;

            if(ci->status==CLIENT_ONLINE) {
                send_list.push_back(ci);
            } else if(ci->status==CLIENT_RECOVERING) {
                std::cout << "RECOVERING CLIENT: " << ci->fd << " PUSHING MESSAGE" << std::endl;
                ci->pending_messages.push(PendingMessage(topic, new_global_seq, new_topic_seq, msg_buffer.data(), msg_size));
            }
        }
    }
    for(auto&ci:send_list)
        if(ci->bev) bufferevent_write(ci->bev,msg_buffer.data(), msg_size);
    // return new_global_seq;
}

void SimplePublisherV2::enqueue_return_client(std::shared_ptr<ClientInfo> ci) {
    {
        std::lock_guard<std::mutex> g(_main_return_mu);
        _main_return_q.push(ci);
    }
    char c='b'; write(_main_notify_pipe[1],&c,1);
}

void read_callback(bufferevent*bev,void*ctx) {
    std::cout << "READ CALL BACK TEST FUNCTION" << std::endl;
}

// accept
void SimplePublisherV2::static_accept_cb(evconnlistener*,evutil_socket_t fd,sockaddr*,int,void*ptr){
    std::cout << "SimplePublisherV2::static_accept_cb called with fd=" << fd << std::endl;
    auto*self=(SimplePublisherV2*)ptr; 
    std::cout << "ID:" << self->_publisher_id << " name:" << self->_publisher_name << std::endl;
    self->on_accept(fd);
}

void SimplePublisherV2::on_accept(evutil_socket_t fd){
    std::cout << "SimplePublisherV2::on_accept called with fd=" << fd << std::endl;
    std::cout << "ID:" << _publisher_id << " name:" << _publisher_name << std::endl;

    auto bev=bufferevent_socket_new(_main_base,fd,BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        std::cerr << "Failed to create bufferevent for fd=" << fd << std::endl;
        close(fd);
        return;
    }
    std::cout << "Created bufferevent successfully" << std::endl;

    auto ci=std::make_shared<ClientInfo>();
    ci->fd=fd; ci->bev=bev; ci->parent=(void*)this;
    auto*ctx=new std::pair<SimplePublisherV2*,std::shared_ptr<ClientInfo>>(this,ci);

    bufferevent_setcb(bev,static_read_cb,nullptr,static_event_cb,ctx);
    std::cout << "Set bufferevent callbacks" << std::endl;

    // 버퍼 크기 설정 및 즉시 읽기 활성화
    // bufferevent_setwatermark(bev, EV_READ, 1, 0);  // 1바이트만 와도 콜백 호출
    bufferevent_enable(bev,EV_READ|EV_WRITE);
    std::cout << "Enabled bufferevent for EV_READ|EV_WRITE with low watermark" << std::endl;

    {std::lock_guard<std::mutex>g(_clients_mu);_clients[fd]=ci;}
    std::cout<<"Client fd="<<fd<<" connected and ready to receive data\n";
}

// read
void SimplePublisherV2::static_read_cb(bufferevent*bev,void*ctx){
    std::cout << "\n\n*** STATIC_READ_CB CALLED - DATA RECEIVED ***" << std::endl;
    auto pairptr=(std::pair<SimplePublisherV2*,std::shared_ptr<ClientInfo>>*)ctx;
    if (!pairptr) {
        std::cerr << "ERROR: Invalid context in static_read_cb" << std::endl;
        return;
    }
    std::cout << "Context valid, calling on_read..." << std::endl;
    pairptr->first->on_read(bev,pairptr->second);
    std::cout << "*** STATIC_READ_CB FINISHED ***\n" << std::endl;
}
void SimplePublisherV2::on_read(bufferevent*bev,std::shared_ptr<ClientInfo>ci){
    std::cout << " SimplePublisherV2::on_read" << std::endl;
    auto*in=bufferevent_get_input(bev);
    while(evbuffer_get_length(in)>0){
        size_t len=evbuffer_get_length(in);
        if (len < sizeof(uint32_t)) {
            break; // Need at least 4 bytes for magic number
        }
        uint32_t magic; evbuffer_copyout(in,&magic,sizeof(uint32_t));
        std::cout << "magic: 0x" << std::hex << magic << std::dec << " MAGIC_SUBSCRIBE: 0x" << std::hex << MAGIC_SUBSCRIBE << std::dec << std::endl;
        if(magic==MAGIC_SUBSCRIBE){
            if (len >= sizeof(SubscriptionRequest)) {
                // SubscriptionRequest *req = reinterpret_cast<SubscriptionRequest*>(data);
                SubscriptionRequest *req = new SubscriptionRequest();
                evbuffer_remove(in,req,sizeof(SubscriptionRequest));
#if 0                
                std::cout << "Received subscription request from client " << req->client_id
                          << " topic_mask: 0x" << std::hex << req->topic_mask << std::dec << std::endl;
                SubscriptionResponse subscription_response;
                subscription_response.magic = MAGIC_SUB_OK;
                subscription_response.result = 0;
                subscription_response.approved_topics = req->topic_mask;
                subscription_response.current_seq = 0; // TODO: Get current sequence from publisher

                // Update client status and info after successful subscription
                ci->status=CLIENT_ONLINE;
                ci->client_id = req->client_id;
                ci->topic_mask = req->topic_mask;
                bufferevent_write(bev,&subscription_response,sizeof(subscription_response));
                std::cout << "Client " << req->client_id << " status changed to ONLINE" << std::endl;
#endif
                handle_subscription_request(ci, req);
                delete req;
            } else {
                std::cerr << "Invalid subscription request size" << std::endl;
            }
        }else if(magic==MAGIC_RECOVERY_REQ){
            if (len >= sizeof(RecoveryRequest)) {
                // RecoveryRequest* req = reinterpret_cast<RecoveryRequest*>(data);
                RecoveryRequest *req = new RecoveryRequest();
                evbuffer_remove(in,req,sizeof(RecoveryRequest));
#if 0
                std::cout << "Received recovery request from client " << req->client_id << std::endl;

                // Send RecoveryResponse with proper target sequence
                RecoveryResponse response;
                response.magic = MAGIC_RECOVERY_RES;
                response.result = 0;  // Success
                response.start_seq = req->last_seq + 1;
                // Capture current global sequence as recovery target (Gap-Free Recovery)
                response.end_seq = _publisher_sequence_record ? _publisher_sequence_record->all_topics_sequence : _db->count();
                response.total_messages = (response.end_seq >= response.start_seq) ? (response.end_seq - response.start_seq + 1) : 0;

                bufferevent_write(bev,&response,sizeof(response));
                // std::cout << " 일단 RECOVERY 처리는 나중에... SKIIIP" << std::endl;
                
                ci->status=CLIENT_RECOVERING;
                // Keep client in _clients map during recovery for pending message handling
                // {std::lock_guard<std::mutex>g(_clients_mu);_clients.erase(ci->fd);}
                auto*w=_workers[_rr_counter++%_workers.size()];
                ::RecoveryTask task = {ci, response.start_seq, response.end_seq};
                {std::lock_guard<std::mutex>qg(w->queue_mu); w->task_q.push(task);}
                char c='r'; write(w->notify_pipe_w,&c,1);
#endif
                handle_recovery_request(ci, req);                
                delete req;
            } else {
                std::cerr << "Invalid recovery request size" << std::endl;
            }

        }else{
            std::cout << "Unknown message type: 0x" << std::hex << magic << std::dec << std::endl;
            // Skip the unknown magic number to avoid infinite loop
            evbuffer_drain(in, sizeof(uint32_t));
        }
    }
}

// event
void SimplePublisherV2::static_event_cb(bufferevent*bev,short ev,void*ctx){
    auto pairptr=(std::pair<SimplePublisherV2*,std::shared_ptr<ClientInfo>>*)ctx;
    auto*self=pairptr->first; auto ci=pairptr->second;
    if(ev&(BEV_EVENT_ERROR|BEV_EVENT_EOF)){
        self->on_client_disconnect(ci); delete pairptr;
    }
}
void SimplePublisherV2::on_client_disconnect(std::shared_ptr<ClientInfo>ci){
    {std::lock_guard<std::mutex>g(_clients_mu);_clients.erase(ci->fd);}
    if(ci->bev){bufferevent_free(ci->bev); ci->bev=nullptr;}
}

// main_notify_cb
void SimplePublisherV2::main_notify_cb(evutil_socket_t fd){
    std::cout << "SimplePublisherV2::main_notify_cb .. back to main_base" << std::endl;
    char buf[16]; read(fd,buf,sizeof(buf));
    std::vector<std::shared_ptr<ClientInfo>>list;
    {std::lock_guard<std::mutex>g(_main_return_mu);
    while(!_main_return_q.empty()){list.push_back(_main_return_q.front());_main_return_q.pop();}}
    for(auto&ci:list){
        if(ci->bev){
            bufferevent_disable(ci->bev,EV_READ|EV_WRITE);
            bufferevent_base_set(_main_base,ci->bev);
            bufferevent_enable(ci->bev,EV_READ|EV_WRITE);
        }
        {
            std::lock_guard<std::mutex> g(ci->mu);
            while(!ci->pending_messages.empty()){
                auto &m=ci->pending_messages.front();
                if(ci->bev) bufferevent_write(ci->bev,m.data.data(),m.data.size());
                std::cout << "SENT Published: [" << topic_to_string(m.topic) << "] " << std::string(m.data.data(), m.data.size()) << std::endl;
                ci->pending_messages.pop();
            }
            ci->status=CLIENT_ONLINE;
        }
        //  Recovery 시작 시 _clients에서 제거하지 않도록 수정했지만, 안전성을 위해 명시적으로 다시 추가
        {std::lock_guard<std::mutex>g(_clients_mu);_clients[ci->fd]=ci;}
        std::cout<<"Client "<<ci->fd<<" back to main base (flushed)\n";
    }
}

// -----------------------------
// RecoveryWorker::on_notify
// -----------------------------
void RecoveryWorker::on_notify(){
    try {
        // 파이프에서 알림 데이터 읽기 (안전하게)
        char buf[16];
        ssize_t bytes_read = read(notify_pipe_r, buf, sizeof(buf));
        if (bytes_read < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Recovery worker notify pipe read error: " << strerror(errno) << std::endl;
            }
            return;
        }

        // 종료 신호 확인
        if (bytes_read > 0 && buf[0] == 'q') {
            std::cout << "Recovery worker received quit signal" << std::endl;
            return;
        }

        // 태스크 큐에서 작업 가져오기
        ::RecoveryTask t;
        {
            std::lock_guard<std::mutex> g(queue_mu);
            if (task_q.empty()) {
                return;
            }
            t = task_q.front();
            task_q.pop();
        }
        std::cout << "Processing recovery task for client " << t.client->fd
                  << " seq range: " << t.from_seq << "-" << t.to_seq << std::endl;
        // 클라이언트 유효성 검사
        auto ci = t.client;
        if (!ci || !ci->bev) {
            std::cerr << "Recovery worker: Invalid client info" << std::endl;
            return;
        }

        // 퍼블리셔 유효성 검사
        auto pub = static_cast<SimplePublisherV2*>(ci->parent);
        if (!pub) {
            std::cerr << "Recovery worker: Invalid publisher reference" << std::endl;
            return;
        }

        std::cout << "Processing recovery task for client " << ci->fd
                  << " seq range: " << t.from_seq << "-" << t.to_seq << std::endl;

        // bufferevent를 이 워커의 event_base로 이동 (안전하게)
        try {
            bufferevent_disable(ci->bev, EV_READ | EV_WRITE);
            if (bufferevent_base_set(base, ci->bev) < 0) {
                std::cerr << "Failed to set bufferevent base for recovery worker" << std::endl;
                return;
            }
            bufferevent_enable(ci->bev, EV_READ | EV_WRITE);
        } catch (...) {
            std::cerr << "Exception while setting up bufferevent for recovery" << std::endl;
            return;
        }

        // 복구 데이터 전송
        uint32_t to = t.to_seq;
        if (to == 0) {
            to = pub->db()->max_seq();
        }

        uint32_t sent_count = 0;
        SAM_INDEX index;
        void* buffer = nullptr;
        uint32_t buffer_size = 0;

        for (uint32_t seq = t.from_seq; seq <= to; seq++) {
            // 워커가 종료 중인지 확인
            if (!running.load()) {
                std::cout << "Recovery worker stopping, aborting recovery task" << std::endl;
                break;
            }

            try {
                buffer = new char[4096];
                if (pub->db()->get(seq, index, buffer, &buffer_size)) {
                    if (buffer && buffer_size > 0) {
                        if (bufferevent_write(ci->bev, buffer, buffer_size) == 0) {
                            sent_count++;
                        } else {
                            std::cerr << "Failed to write recovery data for seq " << seq << std::endl;
                            break;
                        }
                    }
                } else {
                    std::cout << "No data found for sequence " << seq << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception while sending recovery data for seq " << seq << ": " << e.what() << std::endl;
                break;
            }
        }

        // std::cout << " 복구 테스트를 위해 10초가 지나면 종료합니다. " << std::endl;
        // std::this_thread::sleep_for(std::chrono::seconds(10));

        // 복구 완료 메시지 전송
        try {
            RecoveryComplete recovery_complete;
            recovery_complete.magic = MAGIC_RECOVERY_CMP;
            recovery_complete.total_sent = sent_count;
            recovery_complete.timestamp = get_current_timestamp();

            if (bufferevent_write(ci->bev, &recovery_complete, sizeof(recovery_complete)) == 0) {
                std::cout << "Recovery complete sent for client " << ci->fd
                          << ", total sent: " << sent_count << std::endl;
            } else {
                std::cerr << "Failed to send recovery complete message" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception while sending recovery complete: " << e.what() << std::endl;
        }
        pub->enqueue_return_client(ci);
    } catch (const std::exception& e) {
        std::cerr << "Exception in RecoveryWorker::on_notify(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in RecoveryWorker::on_notify()" << std::endl;
    }
    
}

void SimplePublisherV2::handle_subscription_request(std::shared_ptr<ClientInfo> ci, const SubscriptionRequest* req) {
    std::cout << "SimplePublisherV2::handle_subscription_request" << std::endl;
    std::cout << "Received subscription request from client " << req->client_id
                << " topic_mask: 0x" << std::hex << req->topic_mask << std::dec << std::endl;
    SubscriptionResponse subscription_response;
    subscription_response.magic = MAGIC_SUB_OK;
    subscription_response.result = 0;
    subscription_response.approved_topics = req->topic_mask;
    subscription_response.current_seq = 0; // TODO: Get current sequence from publisher

    // Update client status and info after successful subscription
    ci->status=CLIENT_ONLINE;
    ci->client_id = req->client_id;
    ci->topic_mask = req->topic_mask;
    bufferevent_write(ci->bev,&subscription_response,sizeof(subscription_response));
    std::cout << "Client " << req->client_id << " status changed to ONLINE" << std::endl;
}

void SimplePublisherV2::handle_recovery_request(std::shared_ptr<ClientInfo> ci, const RecoveryRequest* req) {
    std::cout << "Received recovery request from client " << req->client_id << std::endl;

    if(ci->status != CLIENT_ONLINE) {
        std::cout << "\n\n####\tWARN Client " << req->client_id << " is not online, skip recovery request" << std::endl;
        return;
    }
    // Send RecoveryResponse with proper target sequence
    RecoveryResponse response;
    response.magic = MAGIC_RECOVERY_RES;
    response.result = 0;  // Success
    response.start_seq = req->last_seq + 1;
    // Capture current global sequence as recovery target (Gap-Free Recovery)
    response.end_seq = _publisher_sequence_record ? _publisher_sequence_record->all_topics_sequence : _db->count();
    response.total_messages = (response.end_seq >= response.start_seq) ? (response.end_seq - response.start_seq + 1) : 0;

    bufferevent_write(ci->bev,&response,sizeof(response));
    // std::cout << " 일단 RECOVERY 처리는 나중에... SKIIIP" << std::endl;
    
    ci->status=CLIENT_RECOVERING;
    // Keep client in _clients map during recovery for pending message handling
    // {std::lock_guard<std::mutex>g(_clients_mu);_clients.erase(ci->fd);}
    // 리커버리 테스크 를 리커비리 스레드로 넘김 (리커버리 스레드의 notify_pipe_w를 통해 알림)
    auto*w=_workers[_rr_counter++%_workers.size()];
    ::RecoveryTask task = {ci, response.start_seq, response.end_seq};
    {std::lock_guard<std::mutex>qg(w->queue_mu); w->task_q.push(task);}
    char c='r'; write(w->notify_pipe_w,&c,1);
}