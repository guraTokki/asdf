#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <chrono>
#include <cstring>
#include "Common.h"

namespace SimplePubSub {

enum class StorageType {
    FILE_STORAGE,
    HASHMASTER_STORAGE
};

// Forward declaration
struct PublisherSequenceRecord;

// HashMaster/File storage에 저장할 Publisher 시퀀스 레코드 구조체 (통합)
struct PublisherSequenceRecord {
    char publisher_name[64];    // Primary Key
    uint32_t publisher_id;
    int publisher_date;         // yyyymmdd
    // uint32_t message_sequence;
    uint32_t topic1_sequence;
    uint32_t topic2_sequence;
    uint32_t misc_sequence;
    uint32_t all_topics_sequence;
    uint64_t last_updated_time; // timestamp (nanoseconds)
    char reserved[32];          // 향후 확장용
    
    PublisherSequenceRecord() {
        memset(this, 0, sizeof(PublisherSequenceRecord));
    }
    
    // 생성자
    PublisherSequenceRecord(const std::string& name, uint32_t id, int date) {
        memset(this, 0, sizeof(PublisherSequenceRecord));
        strncpy(publisher_name, name.c_str(), sizeof(publisher_name) - 1);
        publisher_name[sizeof(publisher_name) - 1] = '\0';
        publisher_id = id;
        publisher_date = date;
        // message_sequence = 1;
        topic1_sequence = 0;
        topic2_sequence = 0;
        misc_sequence = 0;
        all_topics_sequence = 0;
        last_updated_time = get_current_time_ns();
    }
    
    // Helper methods for topic sequence access
    uint32_t get_topic_sequence(DataTopic topic) const {
        switch (topic) {
            case DataTopic::TOPIC1: return topic1_sequence;
            case DataTopic::TOPIC2: return topic2_sequence;
            case DataTopic::MISC: return misc_sequence;
            case DataTopic::ALL_TOPICS: return all_topics_sequence;
            default: return 0;
        }
    }
    
    void set_topic_sequence(uint32_t global_seq, DataTopic topic, uint32_t topic_seq) {
        switch (topic) {
            case DataTopic::TOPIC1: topic1_sequence = topic_seq; break;
            case DataTopic::TOPIC2: topic2_sequence = topic_seq; break;
            case DataTopic::MISC: misc_sequence = topic_seq; break;
            case DataTopic::ALL_TOPICS: all_topics_sequence = topic_seq; break;
        }
        all_topics_sequence = global_seq;
        last_updated_time = get_current_time_ns();
    }
    
    // Legacy support - convert to map format
    std::map<DataTopic, uint32_t> get_topic_sequences_map() const {
        std::map<DataTopic, uint32_t> result;
        if (topic1_sequence > 0) result[DataTopic::TOPIC1] = topic1_sequence;
        if (topic2_sequence > 0) result[DataTopic::TOPIC2] = topic2_sequence;
        if (misc_sequence > 0) result[DataTopic::MISC] = misc_sequence;
        if (all_topics_sequence > 0) result[DataTopic::ALL_TOPICS] = all_topics_sequence;
        return result;
    }
    
    // Legacy support - set from map format
    void set_topic_sequences_from_map(const std::map<DataTopic, uint32_t>& topic_map) {
        for (const auto& pair : topic_map) {
            set_topic_sequence(0, pair.first, pair.second);  // global_seq를 0으로 설정
        }
    }
    
private:
    static uint64_t get_current_time_ns() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }
};

// 시퀀스 저장 전략 인터페이스 (통합된 PublisherSequenceRecord 사용)
class SequenceStorage {
public:
    virtual ~SequenceStorage() = default;
    
    // 시퀀스 저장 (통합된 레코드 사용)
    virtual bool save_sequences(const PublisherSequenceRecord& record) = 0;
    
    // 시퀀스 로드 (publisher_name으로 검색)
    virtual bool load_sequences(const std::string& publisher_name, PublisherSequenceRecord* record) = 0;
    
    // 저장소 초기화
    virtual bool initialize() = 0;
    
    // 저장소 데이터 삭제(hashmaster의 경우 레코드 및 hashtable clear)
    virtual void clear() = 0;

    // 저장소 정리
    virtual void cleanup() = 0;
    
    // 저장소 타입 반환
    virtual std::string get_storage_type() const = 0;
    
    // HashMaster 타입 확인 (최적화 경로 선택용)
    virtual bool is_hashmaster_type() const { return false; }
    
};

} // namespace SimplePubSub