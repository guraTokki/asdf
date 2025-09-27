#ifndef COMMAND_H
#define COMMAND_H

#include <event2/event.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <memory>
#include <iostream>
#include <thread>

// Command pattern base class
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
};

class PrintCommand : public Command {
    std::string _msg;
public:
    explicit PrintCommand(std::string msg) : _msg(std::move(msg)) {}
    void execute() override {
        std::cout << "[PrintCommand] " << _msg << " (tid=" << std::this_thread::get_id() << ")\n";
    }
};

// Lock-free command queue
class CommandQueue {
private:
    std::queue<std::unique_ptr<Command>> _commands;
    std::mutex _mtx;
    
public:
    void push(std::unique_ptr<Command> cmd) {
        std::lock_guard<std::mutex> lock(_mtx);
        _commands.push(std::move(cmd));
    }
    std::unique_ptr<Command> pop() {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_commands.empty()) return nullptr;
        auto cmd = std::move(_commands.front());
        _commands.pop();
        return cmd;
    }
};

class EventProcessor {
    CommandQueue _queue;
    int _event_fd;
    struct event_base *_base;
    struct event *_ev;
    bool _owns_base;  // event_base 소유 여부

    static void on_event(evutil_socket_t fd, short, void *arg) {
        auto *self = static_cast<EventProcessor*>(arg);
        eventfd_t value;
        eventfd_read(fd, &value);  // wakeup 신호 소비
        self->processCommand();
    }

public:
    EventProcessor() : _owns_base(true) {
        _event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (_event_fd < 0) throw std::runtime_error("eventfd failed");

        _base = event_base_new();
        if (!_base) throw std::runtime_error("event_base_new failed");

        _ev = event_new(_base, _event_fd, EV_READ | EV_PERSIST, on_event, this);
        event_add(_ev, nullptr);
    }
    
    // 외부 event_base를 사용하는 생성자
    EventProcessor(struct event_base* external_base) : _owns_base(false) {
        _event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (_event_fd < 0) throw std::runtime_error("eventfd failed");

        _base = external_base;  // 외부 event_base 사용
        if (!_base) throw std::runtime_error("external event_base is null");

        _ev = event_new(_base, _event_fd, EV_READ | EV_PERSIST, on_event, this);
        event_add(_ev, nullptr);
    }

    ~EventProcessor() {
        event_free(_ev);
        if (_owns_base) {
            event_base_free(_base);
        }
        close(_event_fd);
    }

    void throwEvent(std::unique_ptr<Command> cmd) {
        _queue.push(std::move(cmd));
        eventfd_write(_event_fd, 1);  // wakeup
    }

    void processCommand() {
        while (auto cmd = _queue.pop()) {
            cmd->execute();
        }
    }

    void run() {
        event_base_dispatch(_base);
    }
};

#endif // COMMAND_H