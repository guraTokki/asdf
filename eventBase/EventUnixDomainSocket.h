#ifndef EVENTUNIXDOMAINSocket_H
#define EVENTUNIXDOMAINSocket_H

#include "EventBase.h"
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <string>

class EventUnixDomainSocket : public EventBase {
private:
    int _fd;
    bool _connected;
    bool _listening;
    bool _accepting;
    bool _closing;
    evconnlistener* _listener;  // listener

public:
    EventUnixDomainSocket(struct event_base* base, bool base_owned = false);
    ~EventUnixDomainSocket();
    /* connect */
    void connect(const std::string& path) override;
    /* listen : KEEP ALIVE , REUSE ADDR */
    void listen(const std::string& path, bool keep_alive = true, bool reuse_addr = true) override;
    /* accept */
    // void accept() override;
    /* close */
    // void close() override;
    
    /* send data */
    // bool trySend(const void* buffer, size_t size) override;
    
    /* event handlers specific to Unix Domain Socket */
    // void on_accept(evconnlistener* listener, evutil_socket_t fd, struct sockaddr* addr, int len, void* arg);
};

#endif // EVENTUNIXDOMAINSocket_H