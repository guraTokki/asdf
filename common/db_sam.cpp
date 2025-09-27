#include "db_sam.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

DB_SAM::DB_SAM(const std::string& base_path) 
    : base_path_(base_path)
    , index_file_path_(base_path + ".idx")
    , data_file_path_(base_path + ".data")
    , message_count_(0)
    , next_sequence_(1)
    , is_open_(false) {
}

DB_SAM::~DB_SAM() {
    close();
}

bool DB_SAM::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (is_open_) {
        return true;
    }
    
    // Create directory if it doesn't exist (basic implementation for C++11)
    size_t last_slash = base_path_.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string dir_path = base_path_.substr(0, last_slash);
        if (!dir_path.empty()) {
            mkdir(dir_path.c_str(), 0755);
        }
    }
    
    if (!open_files()) {
        return false;
    }
    
    // Read existing index to get message count and next sequence
    index_file_.seekg(0, std::ios::end);
    size_t index_size = index_file_.tellg();
    message_count_ = index_size / sizeof(SAM_INDEX);
    
    if (message_count_ > 0) {
        // Read the last index entry to get the next sequence
        SAM_INDEX last_index;
        index_file_.seekg(-static_cast<int>(sizeof(SAM_INDEX)), std::ios::end);
        index_file_.read(reinterpret_cast<char*>(&last_index), sizeof(SAM_INDEX));
        next_sequence_ = last_index._seq + 1;
    }
    
    // Position at end for appending
    index_file_.clear();
    index_file_.seekp(0, std::ios::end);
    data_file_.clear();
    data_file_.seekp(0, std::ios::end);
    
    is_open_ = true;
    return true;
}

void DB_SAM::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (is_open_) {
        close_files();
        is_open_ = false;
    }
}

bool DB_SAM::open_files() {
    // Open index file
    index_file_.open(index_file_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!index_file_.is_open()) {
        // Try to create if it doesn't exist
        index_file_.open(index_file_path_, std::ios::out | std::ios::binary);
        index_file_.close();
        index_file_.open(index_file_path_, std::ios::in | std::ios::out | std::ios::binary);
    }
    
    if (!index_file_.is_open()) {
        return false;
    }
    
    // Open data file
    data_file_.open(data_file_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!data_file_.is_open()) {
        // Try to create if it doesn't exist
        data_file_.open(data_file_path_, std::ios::out | std::ios::binary);
        data_file_.close();
        data_file_.open(data_file_path_, std::ios::in | std::ios::out | std::ios::binary);
    }
    
    return data_file_.is_open();
}

void DB_SAM::close_files() {
    if (index_file_.is_open()) {
        index_file_.close();
    }
    if (data_file_.is_open()) {
        data_file_.close();
    }
}

bool DB_SAM::sync_files() {
    if (index_file_.is_open()) {
        index_file_.flush();
    }
    if (data_file_.is_open()) {
        data_file_.flush();
    }
    return true;
}

bool DB_SAM::put(const void* data, size_t size) {
    return put(data, size, std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

bool DB_SAM::put(const void* data, size_t size, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_) {
        return false;
    }
    
    // Get current data file position
    data_file_.seekp(0, std::ios::end);
    int64_t data_position = data_file_.tellp();
    
    // Write data to data file
    data_file_.write(static_cast<const char*>(data), size);
    if (data_file_.fail()) {
        return false;
    }
    
    // Create index entry
    SAM_INDEX index;
    index._seek = data_position;
    index._size = static_cast<uint32_t>(size);
    index._seq = next_sequence_;
    index._timestamp = timestamp;
    
    // Write index entry
    if (!write_index(index)) {
        return false;
    }
    
    message_count_++;
    next_sequence_++;
    
    // Sync every 100 messages
    if (message_count_ % 100 == 0) {
        sync_files();
    }
    
    return true;
}

bool DB_SAM::write_index(const SAM_INDEX& index) {
    index_file_.write(reinterpret_cast<const char*>(&index), sizeof(SAM_INDEX));
    return !index_file_.fail();
}

bool DB_SAM::read_index(uint32_t seq, SAM_INDEX& index) const {
    if (seq < 1 || seq >= next_sequence_) {
        return false;
    }
    
    // Calculate position in index file
    size_t position = (seq - 1) * sizeof(SAM_INDEX);
    
    index_file_.seekg(position, std::ios::beg);
    index_file_.read(reinterpret_cast<char*>(&index), sizeof(SAM_INDEX));
    
    return !index_file_.fail() && index._seq == seq;
}

bool DB_SAM::get(uint32_t seq, SAM_INDEX& index, void* buffer, uint32_t* buffer_size) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_ || !buffer_size) {
        return false;
    }
    
    // Read index
    if (!read_index(seq, index)) {
        return false;
    }
    
    // Check buffer size
    // if (*buffer_size < index._size) {
    //     *buffer_size = index._size;
    //     return false;
    // }
    
    // Read data
    data_file_.seekg(index._seek, std::ios::beg);
    data_file_.read(static_cast<char*>(buffer), index._size);
    
    if (data_file_.fail()) {
        return false;
    }
    
    *buffer_size = index._size;
    return true;
}

bool DB_SAM::get(uint32_t seq, std::string& data) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_) {
        return false;
    }
    
    SAM_INDEX index;
    if (!read_index(seq, index)) {
        return false;
    }
    
    // Resize string and read data
    data.resize(index._size);
    data_file_.seekg(index._seek, std::ios::beg);
    data_file_.read(&data[0], index._size);
    
    return !data_file_.fail();
}

bool DB_SAM::get_range(uint32_t start_seq, uint32_t end_seq, 
                       std::function<bool(uint32_t seq, const SAM_INDEX& index, const void* data, size_t size)> callback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_ || start_seq > end_seq) {
        return false;
    }
    
    std::vector<char> buffer;
    
    for (uint32_t seq = start_seq; seq <= end_seq && seq < next_sequence_; ++seq) {
        SAM_INDEX index;
        if (!read_index(seq, index)) {
            continue;
        }
        
        // Resize buffer if needed
        if (buffer.size() < index._size) {
            buffer.resize(index._size);
        }
        
        // Read data
        data_file_.seekg(index._seek, std::ios::beg);
        data_file_.read(buffer.data(), index._size);
        
        if (data_file_.fail()) {
            continue;
        }
        
        // Call callback
        if (!callback(seq, index, buffer.data(), index._size)) {
            break;  // Callback requested to stop
        }
    }
    
    return true;
}

bool DB_SAM::verify_integrity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_) {
        return false;
    }
    
    // Check if index and data files are consistent
    index_file_.seekg(0, std::ios::end);
    size_t index_size = index_file_.tellg();
    uint32_t expected_count = index_size / sizeof(SAM_INDEX);
    
    if (expected_count != message_count_) {
        return false;
    }
    
    // Check each index entry
    for (uint32_t seq = 1; seq < next_sequence_; ++seq) {
        SAM_INDEX index;
        if (!read_index(seq, index)) {
            return false;
        }
        
        if (index._seq != seq) {
            return false;
        }
    }
    
    return true;
}

bool DB_SAM::compact() {
    // TODO: Implement compaction if needed
    // This would involve removing gaps and optimizing storage
    return true;
}

int64_t DB_SAM::get_data_file_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_) {
        return -1;
    }
    
    data_file_.seekg(0, std::ios::end);
    return data_file_.tellg();
}

int64_t DB_SAM::get_index_file_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!is_open_) {
        return -1;
    }

    index_file_.seekg(0, std::ios::end);
    return index_file_.tellg();
}

uint32_t DB_SAM::max_seq() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_open_ || message_count_ == 0) {
        return 0;
    }

    // 마지막 인덱스 엔트리를 읽어서 최대 시퀀스 번호 반환
    SAM_INDEX last_index;
    index_file_.seekg(-static_cast<int>(sizeof(SAM_INDEX)), std::ios::end);
    index_file_.read(reinterpret_cast<char*>(&last_index), sizeof(SAM_INDEX));

    if (index_file_.good()) {
        return last_index._seq;
    }

    return next_sequence_ - 1;
}