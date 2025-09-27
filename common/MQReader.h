#pragma once

#include "../pubsub/Common.h"
#include <functional>
#include <string>
#include <atomic>

namespace SimplePubSub {

// POSIX Message Queue Reader with libevent integration
class MQReader {
private:
    // libevent core
    struct event_base* event_base_;
    struct event* mq_event_;
    
    // POSIX Message Queue
    mqd_t mq_fd_;
    std::string mq_name_;
    
    // Message processing
    std::function<void(const char* data, size_t size)> data_callback_;
    std::function<void(DataTopic topic, const char* data, size_t size)> topic_callback_;
    
    // Configuration
    size_t max_msg_size_;
    long max_msg_count_;
    
    // State
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> messages_received_{0};
    
    // Message buffer
    char* message_buffer_;
    
    // libevent callback (static)
    static void mq_read_callback(evutil_socket_t fd, short events, void* user_data);
    
    // Internal methods
    void process_mq_message();
    DataTopic classify_mq_data(const char* data, size_t size);
    void cleanup();
    
public:
    // Constructor with shared event_base
    explicit MQReader(struct event_base* shared_event_base);
    virtual ~MQReader();
    
    // Message Queue operations
    bool open_mq(const std::string& mq_name, int oflag = O_RDONLY);
    bool create_mq(const std::string& mq_name, long max_msgs = 10, size_t max_msg_size = 8192);
    void close_mq();
    
    // Callback configuration
    void set_data_callback(std::function<void(const char*, size_t)> callback);
    void set_topic_callback(std::function<void(DataTopic, const char*, size_t)> callback);
    
    // Control
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Statistics
    uint64_t get_messages_received() const { return messages_received_.load(); }
    const std::string& get_mq_name() const { return mq_name_; }
    size_t get_max_msg_size() const { return max_msg_size_; }
    long get_max_msg_count() const { return max_msg_count_; }
    
    // Message Queue information
    bool get_mq_attributes(struct mq_attr& attr) const;
    
    // libevent integration - event loop should be managed externally
    struct event_base* get_event_base() const { return event_base_; }
};

} // namespace SimplePubSub