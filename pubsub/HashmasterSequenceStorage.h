#pragma once

#include "SequenceStorage.h"
#include "../HashMaster/HashMaster.h"
#include <iostream>
#include <memory>
#include <string>
#include <cstring>
#include <chrono>

namespace SimplePubSub {

class HashmasterSequenceStorage : public SequenceStorage {
private:
    std::unique_ptr<HashMaster> hashmaster_;
    std::string storage_path_;
    HashMasterConfig config_;
    
    // Direct mmap access optimization (초기화 시 한번 설정)
    PublisherSequenceRecord* direct_record_ptr_;
    std::string cached_publisher_name_;
    
    // 현재 시간을 nanoseconds로 반환
    uint64_t get_current_time_ns() const;
    
    // Direct pointer management
    bool setup_direct_access(const std::string& publisher_name);
    
public:
    explicit HashmasterSequenceStorage(const std::string& storage_path, const std::string& config_file_path = "");
    virtual ~HashmasterSequenceStorage();
    
    // SequenceStorage 인터페이스 구현 (통합 구조체 사용)
    bool save_sequences(const PublisherSequenceRecord& record) override;
    bool load_sequences(const std::string& publisher_name, PublisherSequenceRecord* record) override;

    // mmap 포인터를 직접 반환하는 메서드 (HashmasterSequenceStorage 전용)
    PublisherSequenceRecord* load_sequences_direct(const std::string& publisher_name);

    bool initialize() override;
    inline void clear() override { hashmaster_->clear();}
    void cleanup() override;
    std::string get_storage_type() const override;
    
    // 특정 publisher/subscriber 레코드만 초기화
    bool clear_publisher_record(const std::string& publisher_name, uint32_t publisher_id, int publisher_date=0);
    
    // HashMaster 설정 관리
    void set_hashmaster_config(const HashMasterConfig& config);
    const HashMasterConfig& get_hashmaster_config() const;
    
    // 통계 정보
    int get_total_publishers() const;
    int get_max_publishers() const;
    size_t get_record_size() const;
    bool list_all_publishers(std::vector<std::string>& publisher_names) const;
    
    // Direct mmap access 최적화 메서드들
    bool initialize_for_publisher(const std::string& publisher_name, uint32_t publisher_id, int publisher_date);
    
    // 실시간 시퀀스 업데이트 (zero-copy, 초기화된 direct pointer 사용)
    inline bool increment_sequence_direct(DataTopic topic, uint32_t topic_seq, uint32_t global_seq) {
        if (!direct_record_ptr_) return false;
        
        // 직접 mmap 메모리 업데이트 (변환 없음, 포인터 조회 없음)
        direct_record_ptr_->all_topics_sequence = global_seq;
        
        switch(topic) {
            case DataTopic::TOPIC1: direct_record_ptr_->topic1_sequence = topic_seq; break;
            case DataTopic::TOPIC2: direct_record_ptr_->topic2_sequence = topic_seq; break;
            case DataTopic::MISC:  direct_record_ptr_->misc_sequence = topic_seq; break;
            case DataTopic::ALL_TOPICS: /* 모든 토픽은 이미 all_topics_sequence에서 처리 */ break;
        }
        
        // 타임스탬프 업데이트
        direct_record_ptr_->last_updated_time = get_current_time_ns();
        
        std::cout << "##\n## increment_sequence_direct\n ::"
            << "\t topic:" << static_cast<int>(topic)
            << "\t all_topic_seq=" << direct_record_ptr_->all_topics_sequence
            << "\t TOPIC1=" << direct_record_ptr_->topic1_sequence
            << "\t TOPIC2=" << direct_record_ptr_->topic2_sequence
            << "\t MISC=" << direct_record_ptr_->misc_sequence
            << std::endl;

        return true;
    }
    
    // Direct access 상태 확인
    bool is_direct_access_ready() const { return direct_record_ptr_ != nullptr; }
    const std::string& get_cached_publisher_name() const { return cached_publisher_name_; }
    
    // HashMaster 타입 확인 (SimplePublisher에서 최적화 경로 선택용)
    bool is_hashmaster_type() const { return true; }
};

} // namespace SimplePubSub