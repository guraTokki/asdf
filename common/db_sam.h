#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <functional>
#include "MessageDB.h"

// Sequential Access Message Database
// 순차 접근 메시지 데이터베이스 - 메시지를 순서대로 저장하고 인덱스를 통해 빠른 검색

/**
 * DB_SAM - 파일 기반 Sequential Access Message Database
 *
 * MessageDB 인터페이스를 구현한 파일 기반 데이터베이스.
 * 데이터를 파일 시스템에 영구 저장하며, 인덱스를 통한 빠른 검색을 지원합니다.
 * 주로 실제 운영 환경에서 데이터 영속성이 필요한 경우 사용됩니다.
 */
class DB_SAM : public MessageDB {
private:
    std::string base_path_;
    std::string index_file_path_;
    std::string data_file_path_;
    
    mutable std::fstream index_file_;
    mutable std::fstream data_file_;
    
    uint32_t message_count_;
    uint32_t next_sequence_;
    
    mutable std::mutex mutex_;  // Thread-safe operations
    bool is_open_;
    
    // File operations
    bool open_files();
    void close_files();
    bool sync_files();
    
    // Index operations
    bool write_index(const SAM_INDEX& index);
    bool read_index(uint32_t seq, SAM_INDEX& index) const;
    
public:
    explicit DB_SAM(const std::string& base_path);
    virtual ~DB_SAM();

    // MessageDB 인터페이스 구현
    bool open() override;
    void close() override;
    bool isOpen() const override { return is_open_; }

    // Message storage
    bool put(const void* data, size_t size) override;
    bool put(const void* data, size_t size, uint64_t timestamp) override;

    // Message retrieval
    bool get(uint32_t seq, SAM_INDEX& index, void* buffer, uint32_t* buffer_size) const override;
    bool get(uint32_t seq, std::string& data) const override;

    // Database information
    uint32_t count() const override { return message_count_; }
    uint32_t get_next_sequence() const override { return next_sequence_; }
    uint32_t max_seq() const override;

    // Range operations
    bool get_range(uint32_t start_seq, uint32_t end_seq,
                   std::function<bool(uint32_t seq, const SAM_INDEX& index, const void* data, size_t size)> callback) const override;

    // Maintenance operations
    bool verify_integrity() const override;
    bool compact() override;

    // Statistics
    int64_t get_data_file_size() const override;
    int64_t get_index_file_size() const override;
    
    // File paths
    const std::string& get_base_path() const { return base_path_; }
    const std::string& get_index_file_path() const { return index_file_path_; }
    const std::string& get_data_file_path() const { return data_file_path_; }
};