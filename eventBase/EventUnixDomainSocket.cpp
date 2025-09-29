#include "EventUnixDomainSocket.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <iostream>
#include <event2/buffer.h>

EventUnixDomainSocket::EventUnixDomainSocket(struct event_base* base, bool base_owned) : EventBase(base, base_owned), _fd(0), _connected(false), _listening(false), _accepting(false), _closing(false), _listener(nullptr) {

}

EventUnixDomainSocket::~EventUnixDomainSocket() {
    if (_listener) {
        evconnlistener_free(_listener);
    }
    // Note: _bev is handled by parent class EventBase destructor
}

void EventUnixDomainSocket::connect(const std::string& path) {
    _path = path;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, _path.c_str(), sizeof(addr.sun_path) - 1);
    
    auto base = getBase();
    _bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (_bev == nullptr) {
        throw std::runtime_error("Failed to create bufferevent");
    }
    
    bufferevent_setcb(_bev, EventBase::static_read_cb, EventBase::static_write_cb, EventBase::static_event_cb, this);
    bufferevent_enable(_bev, EV_READ | EV_WRITE);
    
    if ( (_bev, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if(_bev != nullptr) {
            bufferevent_free(_bev);
            _bev = nullptr;
        }
        throw std::runtime_error("Failed to connect");
    }
    _connected = true;
}

void EventUnixDomainSocket::listen(const std::string& path, bool keep_alive, bool reuse_addr) {
    _path = path;
    
    // Remove existing socket file if it exists
    unlink(_path.c_str());
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, _path.c_str(), sizeof(addr.sun_path) - 1);
    
    _listener = evconnlistener_new_bind(getBase(), EventBase::static_accept_cb, this, 
                                       LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, 
                                       -1, (struct sockaddr*)&addr, sizeof(addr));
    if (_listener == nullptr) {
        throw std::runtime_error("Failed to create listener");
    }
    _listening = true;
}

// void EventUnixDomainSocket::on_accept(evconnlistener* listener, evutil_socket_t fd, struct sockaddr* addr, int len, void* arg) {
//     std::cout << "New connection accepted on fd: " << fd << std::endl;
    
//     // Create bufferevent for the accepted connection
//     // _bev = bufferevent_socket_new(getBase(), fd, BEV_OPT_CLOSE_ON_FREE);
//     // if (_bev) {
//     //     bufferevent_setcb(_bev, EventBase::static_read_cb, EventBase::static_write_cb, EventBase::static_event_cb, this);
//     //     bufferevent_enable(_bev, EV_READ | EV_WRITE);
//     // }
    
//     // Call accept callback to notify application
//         call_accept_callback(fd, addr, len);
    
//     (void)listener; (void)arg;
// }

// bool EventUnixDomainSocket::trySend(const void* buffer, size_t size) {
//     std::cout << "EventUnixDomainSocket::trySend" << std::endl;
//     if (_bev) {
//         std::cout << "trySend" << std::endl;
//         uint32_t length = htonl(size);
//         bufferevent_write(_bev, &length, sizeof(uint32_t));
//         return bufferevent_write(_bev, buffer, size) == 0;
//     }
//     return false;
// }