#include "MQReader.h"
#include <iostream>
#include <errno.h>
#include <string.h>

namespace SimplePubSub {

MQReader::MQReader(struct event_base* shared_event_base)
    : event_base_(shared_event_base)
    , mq_event_(nullptr)
    , mq_fd_(-1)
    , max_msg_size_(8192)
    , max_msg_count_(10)
    , message_buffer_(nullptr) {
    
    if (!event_base_) {
        throw std::runtime_error("event_base cannot be null");
    }
}

MQReader::~MQReader() {
    stop();
    close_mq();
    
    if (message_buffer_) {
        delete[] message_buffer_;
    }
}

bool MQReader::open_mq(const std::string& mq_name, int oflag) {
    mq_name_ = mq_name;
    
    // Open existing message queue
    mq_fd_ = mq_open(mq_name.c_str(), oflag);
    if (mq_fd_ == -1) {
        std::cerr << "Failed to open message queue '" << mq_name 
                  << "': " << strerror(errno) << std::endl;
        return false;
    }
    
    // Get message queue attributes
    struct mq_attr attr;
    if (mq_getattr(mq_fd_, &attr) != 0) {
        std::cerr << "Failed to get message queue attributes: " << strerror(errno) << std::endl;
        close_mq();
        return false;
    }
    
    max_msg_size_ = attr.mq_msgsize;
    max_msg_count_ = attr.mq_maxmsg;
    
    // Allocate message buffer
    message_buffer_ = new char[max_msg_size_];
    
    std::cout << "Opened message queue: " << mq_name 
              << " (max_msg_size=" << max_msg_size_ 
              << ", max_msg_count=" << max_msg_count_ << ")" << std::endl;
    
    return true;
}

bool MQReader::create_mq(const std::string& mq_name, long max_msgs, size_t max_msg_size) {
    mq_name_ = mq_name;
    max_msg_size_ = max_msg_size;
    max_msg_count_ = max_msgs;
    
    // Set message queue attributes
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = max_msgs;
    attr.mq_msgsize = max_msg_size;
    attr.mq_curmsgs = 0;
    
    // Create message queue
    mq_fd_ = mq_open(mq_name.c_str(), O_CREAT | O_RDONLY, 0644, &attr);
    if (mq_fd_ == -1) {
        std::cerr << "Failed to create message queue '" << mq_name 
                  << "': " << strerror(errno) << std::endl;
        return false;
    }
    
    // Allocate message buffer
    message_buffer_ = new char[max_msg_size_];
    
    std::cout << "Created message queue: " << mq_name 
              << " (max_msg_size=" << max_msg_size_ 
              << ", max_msg_count=" << max_msg_count_ << ")" << std::endl;
    
    return true;
}

void MQReader::close_mq() {
    if (mq_fd_ != -1) {
        mq_close(mq_fd_);
        mq_fd_ = -1;
        std::cout << "Closed message queue: " << mq_name_ << std::endl;
    }
}

void MQReader::set_data_callback(std::function<void(const char*, size_t)> callback) {
    data_callback_ = callback;
}

void MQReader::set_topic_callback(std::function<void(DataTopic, const char*, size_t)> callback) {
    topic_callback_ = callback;
}

void MQReader::start() {
    if (running_.load() || mq_fd_ == -1) {
        return;
    }
    
    // Create libevent for message queue file descriptor
    mq_event_ = event_new(event_base_, mq_fd_, EV_READ | EV_PERSIST, mq_read_callback, this);
    if (!mq_event_) {
        std::cerr << "Failed to create message queue event" << std::endl;
        return;
    }
    
    if (event_add(mq_event_, nullptr) != 0) {
        std::cerr << "Failed to add message queue event" << std::endl;
        event_free(mq_event_);
        mq_event_ = nullptr;
        return;
    }
    
    running_.store(true);
    std::cout << "MQReader started for: " << mq_name_ << std::endl;
}

void MQReader::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (mq_event_) {
        event_del(mq_event_);
        event_free(mq_event_);
        mq_event_ = nullptr;
    }
    
    std::cout << "MQReader stopped" << std::endl;
}

void MQReader::mq_read_callback(evutil_socket_t fd, short events, void* user_data) {
    auto* reader = static_cast<MQReader*>(user_data);
    reader->process_mq_message();
}

void MQReader::process_mq_message() {
    if (mq_fd_ == -1 || !message_buffer_) {
        return;
    }
    
    unsigned int priority;
    ssize_t msg_size = mq_receive(mq_fd_, message_buffer_, max_msg_size_, &priority);
    
    if (msg_size == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No message available, this is normal
            return;
        } else {
            std::cerr << "Failed to receive message from queue: " << strerror(errno) << std::endl;
            return;
        }
    }
    
    if (msg_size == 0) {
        return; // Empty message
    }
    
    messages_received_.fetch_add(1);
    
    // Call data callback if set
    if (data_callback_) {
        data_callback_(message_buffer_, msg_size);
    }
    
    // Call topic callback if set
    if (topic_callback_) {
        DataTopic topic = classify_mq_data(message_buffer_, msg_size);
        topic_callback_(topic, message_buffer_, msg_size);
    }
}

DataTopic MQReader::classify_mq_data(const char* data, size_t size) {
    // Convert to string for analysis
    std::string line(data, size);
    
    // Simple data classification
    return DataTopic::TOPIC1;
}

bool MQReader::get_mq_attributes(struct mq_attr& attr) const {
    if (mq_fd_ == -1) {
        return false;
    }
    
    return mq_getattr(mq_fd_, &attr) == 0;
}

void MQReader::cleanup() {
    stop();
    close_mq();
}

} // namespace SimplePubSub