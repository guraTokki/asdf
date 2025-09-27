#ifndef EVENTBASE_H
#define EVENTBASE_H

#include <event2/event.h>
#include <functional>
#include <memory>
#include <string>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include "Protocol.h"

/* libEvent 라이브러리를 사용하여 이벤트 기반 프로그래밍을 위한 클래스 
 * tcp, udp, unix domain socket, message queue 등 다양한 프로토콜을 지원하기 위해 
 * 추상 클래스로 설계되었습니다.
 * 
 * create 함수를 통해 각 프로토콜에 맞는 객체를 생성
 * tcp, udp, unix domain socket, message queue 를 이 class 로 사용하는 것이 목적
 * static_read_cb, static_write_cb, static_event_cb, static_accept_cb 를 사용하여 이벤트 콜백을 처리
 * 이 static_xxxx_cb 함수들에서 setXXXXCallback으로 등록한 callback 함수를 호출하여 이벤트 처리
 */
class EventBase {
protected:    
    struct event_base* _base;
    bool _base_owned;
    struct event* _event;   // message queue의 경우 이벤트를 사용
    bufferevent* _bev;  // socket의 경우 버퍼이벤트를 사용

    void* _ctx;
    std::string _path;  // socket path for unix domain socket, address for TCP/UDP

    std::function<void(char *data, int size)> _read_cb;
    std::function<void(char *data, int size)> _write_cb;
    std::function<void(char *data, int size)> _connect_cb;
    std::function<void(char *data, int size)> _disconnect_cb;
    std::function<void(char *data, int size)> _timeout_cb;
    std::function<void(char *data, int size)> _error_cb;
    // accept callback
    std::function<void(int fd, struct sockaddr* addr, int len)> _accept_cb;
    
    // Protocol parser (shared across all connections)
    Protocol* _protocol;

private:

    bool _running;
    bool _stopped;
    bool _paused;
    bool _resumed;

    int _interval;
    int _timeout;


public:
    EventBase(struct event_base* base, bool base_owned = false);
    virtual ~EventBase();

    struct event_base* getBase();
    void setBase(struct event_base* base);
    struct event* getEvent();
    void setEvent(struct event* event);
    bufferevent* getBev();
    void setBev(bufferevent* bev);

    // event_base->_base_owned is true, then do something
    // event_base->_base_owned is false, then do nothing
    void start();
    void stop();
    void pause();
    void resume();

    /* set callback */
    void setReadCallback(std::function<void(char *data, int size)> callback);
    void setWriteCallback(std::function<void(char *data, int size)> callback);
    void setConnectCallback(std::function<void(char *data, int size)> callback);
    void setDisconnectCallback(std::function<void(char *data, int size)> callback);
    void setTimeoutCallback(std::function<void(char *data, int size)> callback);
    void setErrorCallback(std::function<void(char *data, int size)> callback);
    void setAcceptCallback(std::function<void(int fd, struct sockaddr* addr, int len)> callback);

    void setInterval(int interval);
    void setTimeout(int timeout);

    /* listen */
    virtual void listen(const std::string& path, bool keep_alive = true, bool reuse_addr = true) {};

    /* connect */
    virtual void connect(const std::string& path) {};

    /* accept */
    // virtual void accept() {};

    /* close */
    virtual void close() {};

    /* trySend */
    virtual bool trySend(const void* buffer, size_t size);
    
    /* Setup bufferevent for accepted connection */
    bool setupBufferevent(int fd);
    
    /* Protocol management */
    void setProtocol(Protocol* protocol);
    Protocol* getProtocol() const;
    
    /* callback helpers - can be used by derived classes */
    void call_read_callback(char* data, int size);
    void call_write_callback(char* data, int size);
    void call_connect_callback();
    void call_disconnect_callback();
    void call_timeout_callback();
    void call_error_callback();
    void call_accept_callback(int fd, struct sockaddr* addr, int len);
    
    /* static callback functions for libevent - can be used by all derived classes */
    /* static_read_cb: input evbuffer에 데이터가 들어왔을 때 호출되는 콜백 */
    /* static_write_cb: output evbuffer가 완전히 비워졌을 때 호출되는 콜백 */
    /* static_event_cb: bufferevent의 이벤트가 발생했을 때 호출되는 콜백 */
    /* static_accept_cb: 새로운 연결이 들어왔을 때 호출되는 콜백 */
    static void static_read_cb(struct bufferevent *bev, void *ctx);
    static void static_write_cb(struct bufferevent *bev, void *ctx);
    static void static_event_cb(struct bufferevent *bev, short events, void *ctx);
    static void static_accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx);
};

EventBase* createEventBase(std::string type, struct event_base* base, bool base_owned = false);

#endif // EVENTBASE_H