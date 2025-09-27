#include "EventBase.h"
#include "EventUnixDomainSocket.h"
#include "EventTimer.h"
#include <string>
#include <iostream>
#include <event2/buffer.h>

EventBase::EventBase(struct event_base* base, bool base_owned) : _base(base), _base_owned(base_owned), _event(nullptr), _bev(nullptr), _ctx(nullptr), _path(""), _running(false), _stopped(false), _paused(false), _resumed(false), _interval(0), _timeout(0), _protocol(nullptr) {
}

EventBase::~EventBase() {
    if (_bev) {
        bufferevent_free(_bev); _bev = nullptr;
    }
    if (_base_owned && _base) {
        event_base_free(_base);
    }
}

struct event_base* EventBase::getBase() {
    return _base;
}

void EventBase::setBase(struct event_base* base) {
    _base = base;
}

struct event* EventBase::getEvent() {
    return _event;
}

void EventBase::setEvent(struct event* event) {
    _event = event;
}

bufferevent* EventBase::getBev() {
    return _bev;
}

void EventBase::setBev(bufferevent* bev) {
    _bev = bev;
}

void EventBase::start() {
    if (_running) {
        return;
    }
    _running = true;
    event_base_dispatch(_base);
}

void EventBase::stop() {
    if (!_running) {
        return;
    }
    _running = false;
    event_base_loopexit(_base, nullptr);
}

void EventBase::pause() {
    if (_paused) {
        return;
    }
    _paused = true;
}

void EventBase::resume() {
    if (!_paused) {
        return;
    }
    _paused = false;
}

void EventBase::setReadCallback(std::function<void(char *data, int size)> callback) {
    _read_cb = std::move(callback);
}

void EventBase::setWriteCallback(std::function<void(char *data, int size)> callback) {
    _write_cb = std::move(callback);
}

void EventBase::setConnectCallback(std::function<void(char *data, int size)> callback) {
    _connect_cb = std::move(callback);
}

void EventBase::setDisconnectCallback(std::function<void(char *data, int size)> callback) {
    _disconnect_cb = std::move(callback);
}

void EventBase::setTimeoutCallback(std::function<void(char *data, int size)> callback) {
    _timeout_cb = std::move(callback);
}

void EventBase::setErrorCallback(std::function<void(char *data, int size)> callback) {
    _error_cb = std::move(callback);
}

void EventBase::setAcceptCallback(std::function<void(int fd, struct sockaddr* addr, int len)> callback) {
    _accept_cb = std::move(callback);
}

void EventBase::setInterval(int interval) {
    _interval = interval;
}

void EventBase::setTimeout(int timeout) {
    _timeout = timeout;
}

bool EventBase::trySend(const void* buffer, size_t size) {
    if (_bev) {
        if (_protocol) {
            // Protocol encoder 사용 (Zero-Copy)
            struct evbuffer* output = bufferevent_get_output(_bev);
            return _protocol->encodeToBuffer(output, buffer, size);
        } else {
            // 기본 동작 (직접 전송)
            return bufferevent_write(_bev, buffer, size) == 0;
        }
    }
    return false;
}

bool EventBase::setupBufferevent(int fd) {
    if (_bev) {
        bufferevent_free(_bev);
        _bev = nullptr;
    }
    
    _bev = bufferevent_socket_new(getBase(), fd, BEV_OPT_CLOSE_ON_FREE);
    if (_bev) {
        bufferevent_setcb(_bev, EventBase::static_read_cb, EventBase::static_write_cb, EventBase::static_event_cb, this);
        bufferevent_enable(_bev, EV_READ | EV_WRITE);
        return true;
    }
    return false;
}

void EventBase::setProtocol(Protocol* protocol) {
    _protocol = protocol;
}

Protocol* EventBase::getProtocol() const {
    return _protocol;
}

void EventBase::call_read_callback(char* data, int size) {
    if (_read_cb) {
        _read_cb(data, size);
    }
}

void EventBase::call_write_callback(char* data, int size) {
    if (_write_cb) {
        _write_cb(data, size);
    }
}

void EventBase::call_connect_callback() {
    if (_connect_cb) {
        _connect_cb(nullptr, 0);
    }
}

void EventBase::call_disconnect_callback() {
    if (_disconnect_cb) {
        _disconnect_cb(nullptr, 0);
    }
}

void EventBase::call_timeout_callback() {
    if (_timeout_cb) {
        _timeout_cb(nullptr, 0);
    }
}

void EventBase::call_error_callback() {
    if (_error_cb) {
        _error_cb(nullptr, 0);
    }
}

void EventBase::call_accept_callback(int fd, struct sockaddr* addr, int len) {
    if (_accept_cb) {
        _accept_cb(fd, addr, len);
    }
}

// Static callback functions for libevent
void EventBase::static_read_cb(struct bufferevent *bev, void *ctx) {
    EventBase* self = static_cast<EventBase*>(ctx);
    struct evbuffer *input = bufferevent_get_input(bev);
    
    if (self->_protocol) {
        // Protocol parser 사용 (Zero-Copy)
        size_t consumed = self->_protocol->parseBuffer(input, [self](const char* data, size_t len) {
            self->call_read_callback(const_cast<char*>(data), len);
        });
        std::cout << "[EventBase] static_read_cb consumed: " << consumed << std::endl;
        // evbuffer_drain(input, consumed); // protocol 에서 처리하므로 여기서는 처리하지 않음
    } else {
        // 기본 동작 (기존 Raw 방식)
        size_t len = evbuffer_get_length(input);
        if (len > 0) {
            char *data = new char[len + 1];
            evbuffer_remove(input, data, len);
            data[len] = '\0';
            self->call_read_callback(data, len);
            delete[] data;
        }
    }
}

void EventBase::static_write_cb(struct bufferevent *bev, void *ctx) {
    (void)bev; (void)ctx;
    // Write completed callback can be implemented if needed
}

void EventBase::static_event_cb(struct bufferevent *bev, short events, void *ctx) {
    EventBase* self = static_cast<EventBase*>(ctx);
    if (events & BEV_EVENT_CONNECTED) {
        std::cout << "Client connected to server" << std::endl;
        self->call_connect_callback();
    }
    if (events & BEV_EVENT_ERROR) {
        std::cout << "Connection error occurred" << std::endl;
        self->call_error_callback();
        bufferevent_free(bev); self->_bev = nullptr;
    } else if (events & BEV_EVENT_EOF) {
        std::cout << "Connection closed by peer" << std::endl;
        self->call_disconnect_callback();
        bufferevent_free(bev); self->_bev = nullptr;
    }
}

void EventBase::static_accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx) {
    EventBase* self = static_cast<EventBase*>(ctx);
    self->call_accept_callback(fd, address, socklen);
    (void)listener;
}

EventBase* createEventBase(std::string type, struct event_base* base, bool base_owned) {
    if (type == "unix_domain_socket") {
        return new EventUnixDomainSocket(base, base_owned);
    } else if (type == "timer") {
        return new EventTimer(base, base_owned);
    }
    return nullptr;
}
