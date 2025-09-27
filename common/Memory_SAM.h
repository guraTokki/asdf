#ifndef MEMORY_SAM_H
#define MEMORY_SAM_H

#include "MessageDB.h"
#include <map>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <chrono>

/**
 * Memory_SAM - 메모리 기반 Sequential Access Message Database
 *
 * MessageDB 인터페이스를 구현한 인메모리 데이터베이스.
 * 빠른 읽기/쓰기 성능을 제공하지만 데이터 영속성이 없습니다.
 * 테스트, 캐싱, 임시 데이터 저장 용도로 사용됩니다.
 */
class Memory_SAM : public MessageDB {
private:
    // 메시지 데이터 저장소
    std::map<uint32_t, void*> _data_map;
    // 인덱스 정보 저장소
    std::map<uint32_t, SAM_INDEX> _index_map;

    mutable std::mutex _mutex;
    bool _is_open;
    uint32_t _next_sequence;

    // 현재 시간을 나노초로 반환
    uint64_t get_current_timestamp() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        return static_cast<uint64_t>(nanos.count());
    }

public:
    Memory_SAM() : _is_open(false), _next_sequence(1) {}

    ~Memory_SAM() override {
        close();
    }

    // 데이터베이스 연결/해제
    bool open() override {
        std::lock_guard<std::mutex> lock(_mutex);
        _is_open = true;
        return true;
    }

    void close() override {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_is_open) {
            // 모든 동적 할당된 메모리 해제
            for (auto& pair : _data_map) {
                free(pair.second);
            }
            _data_map.clear();
            _index_map.clear();
            _is_open = false;
        }
    }

    bool isOpen() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        return _is_open;
    }

    // 메시지 저장
    bool put(const void* data, size_t size) override {
        return put(data, size, get_current_timestamp());
    }

    bool put(const void* data, size_t size, uint64_t timestamp) override {
        if (!data || size == 0) return false;

        std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_open) return false;

        uint32_t seq = _next_sequence++;

        // 데이터 복사본 생성
        void* data_copy = malloc(size);
        if (!data_copy) return false;

        memcpy(data_copy, data, size);

        // 데이터와 인덱스 저장
        _data_map[seq] = data_copy;
        _index_map[seq] = SAM_INDEX{
            0,  // seek는 메모리에서는 의미 없음
            static_cast<uint32_t>(size),
            seq,
            timestamp
        };

        return true;
    }

    // 메시지 검색
    bool get(uint32_t seq, SAM_INDEX& index, void* buffer, uint32_t* buffer_size) const override {
        if (!buffer_size) return false;

        std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_open) return false;

        auto index_it = _index_map.find(seq);
        if (index_it == _index_map.end()) return false;

        auto data_it = _data_map.find(seq);
        if (data_it == _data_map.end()) return false;

        index = index_it->second;

        // 버퍼가 충분한지 확인
        if (*buffer_size < index._size) {
            *buffer_size = index._size;  // 필요한 크기를 반환
            return false;
        }

        // 데이터 복사
        memcpy(buffer, data_it->second, index._size);
        *buffer_size = index._size;

        return true;
    }

    bool get(uint32_t seq, std::string& data) const override {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_open) return false;

        auto data_it = _data_map.find(seq);
        if (data_it == _data_map.end()) return false;

        auto index_it = _index_map.find(seq);
        if (index_it == _index_map.end()) return false;

        data = std::string(static_cast<char*>(data_it->second), index_it->second._size);
        return true;
    }

    // 메타데이터
    uint32_t get_next_sequence() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        return _next_sequence;
    }

    uint32_t count() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        return static_cast<uint32_t>(_data_map.size());
    }

    uint32_t max_seq() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        return _index_map.empty() ? 0 : _index_map.rbegin()->first;
    }

    // 범위 연산 구현
    bool get_range(uint32_t start_seq, uint32_t end_seq,
                   std::function<bool(uint32_t seq, const SAM_INDEX& index, const void* data, size_t size)> callback) const override {
        if (!callback || start_seq > end_seq) return false;

        std::lock_guard<std::mutex> lock(_mutex);
        if (!_is_open) return false;

        auto start_it = _index_map.lower_bound(start_seq);
        auto end_it = _index_map.upper_bound(end_seq);

        for (auto it = start_it; it != end_it; ++it) {
            uint32_t seq = it->first;
            const SAM_INDEX& index = it->second;

            auto data_it = _data_map.find(seq);
            if (data_it == _data_map.end()) continue;

            // 콜백 호출, false를 반환하면 중단
            if (!callback(seq, index, data_it->second, index._size)) {
                break;
            }
        }

        return true;
    }

    // 유틸리티 메서드들
    bool verify_integrity() const override {
        std::lock_guard<std::mutex> lock(_mutex);

        // 데이터와 인덱스의 일관성 검사
        if (_data_map.size() != _index_map.size()) {
            return false;
        }

        for (const auto& index_pair : _index_map) {
            uint32_t seq = index_pair.first;
            if (_data_map.find(seq) == _data_map.end()) {
                return false;
            }
        }

        return true;
    }

    bool compact() override {
        // 메모리 기반이므로 컴팩션이 불필요
        return true;
    }

    // 메모리 사용량 정보 (바이트 단위)
    int64_t get_data_file_size() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        int64_t total_size = 0;
        for (const auto& index_pair : _index_map) {
            total_size += index_pair.second._size;
        }
        return total_size;
    }

    int64_t get_index_file_size() const override {
        std::lock_guard<std::mutex> lock(_mutex);
        return static_cast<int64_t>(_index_map.size() * sizeof(SAM_INDEX));
    }
};

#endif // MEMORY_SAM_H