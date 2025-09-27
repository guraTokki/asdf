#ifndef MESSAGE_DB_H
#define MESSAGE_DB_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <functional>

// Forward declaration of SAM_INDEX
struct SAM_INDEX {
    int64_t _seek;         // 데이터 파일 내 위치
    uint32_t _size;        // 메시지 크기
    uint32_t _seq;         // 일련번호
    uint64_t _timestamp;   // 저장 시간 (nanoseconds)
};

/**
 * MessageDB - 메시지 데이터베이스 추상 기본 클래스
 *
 * 이 클래스는 메시지 저장 및 검색을 위한 공통 인터페이스를 정의합니다.
 * 구현체는 파일 기반(DB_SAM), 메모리 기반(Memory_SAM) 등이 있을 수 있습니다.
 */
class MessageDB {
public:
    MessageDB() = default;
    virtual ~MessageDB() = default;

    // 데이터베이스 연결/해제
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // 메시지 저장
    virtual bool put(const void* data, size_t size) = 0;
    virtual bool put(const void* data, size_t size, uint64_t timestamp) = 0;

    // 메시지 검색
    virtual bool get(uint32_t seq, SAM_INDEX& index, void* buffer, uint32_t* buffer_size) const = 0;
    virtual bool get(uint32_t seq, std::string& data) const = 0;

    // 메타데이터
    virtual uint32_t get_next_sequence() const = 0;
    virtual uint32_t count() const = 0;
    virtual uint32_t max_seq() const = 0;

    // 범위 연산 (선택사항 - 기본 구현은 false 반환)
    virtual bool get_range(uint32_t start_seq, uint32_t end_seq,
                          std::function<bool(uint32_t seq, const SAM_INDEX& index, const void* data, size_t size)> callback) const {
        return false;  // 기본적으로 지원하지 않음
    }

    // 유틸리티 메서드들 (기본 구현 제공)
    virtual bool verify_integrity() const { return true; }
    virtual bool compact() { return true; }
    virtual int64_t get_data_file_size() const { return -1; }  // -1은 지원하지 않음을 의미
    virtual int64_t get_index_file_size() const { return -1; }
};

#endif // MESSAGE_DB_H