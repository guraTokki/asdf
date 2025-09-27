#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <cstdint>
#include <event2/buffer.h>

class Protocol {
public:
    // Zero-Copy 수신: 완전한 메시지가 파싱되면 콜백 함수 호출
    // data는 evbuffer 내부 포인터이므로 복사 없음 (사용 후 즉시 소비해야 함)
    using MessageCallback = std::function<void(const char* data, size_t length)>;
    
    virtual ~Protocol() = default;
    
    // 수신: evbuffer에서 직접 파싱, 완전한 메시지가 있으면 callback 호출
    // 반환값: 처리한 바이트 수 (evbuffer에서 drain할 양)
    virtual size_t parseBuffer(struct evbuffer* input, const MessageCallback& callback) = 0;
    
    // 송신: evbuffer에 직접 인코딩된 데이터 추가 (Zero-Copy)
    virtual bool encodeToBuffer(struct evbuffer* output, const void* data, size_t length) = 0;
    
    virtual void reset() = 0;  // 연결 종료 시 상태 리셋
};

// 1. 기본: evbuffer에 있는 모든 데이터를 즉시 전달 (Zero-Copy)
class RawProtocol : public Protocol {
public:
    size_t parseBuffer(struct evbuffer* input, const MessageCallback& callback) override;
    bool encodeToBuffer(struct evbuffer* output, const void* data, size_t length) override;
    void reset() override;
};

// 2. 길이(4byte) + 데이터 형식
class LengthPrefixedProtocol : public Protocol {
private:
    uint32_t _expected_length;
    bool _reading_header;
    
public:
    LengthPrefixedProtocol();
    size_t parseBuffer(struct evbuffer* input, const MessageCallback& callback) override;
    bool encodeToBuffer(struct evbuffer* output, const void* data, size_t length) override;
    void reset() override;
};

// 3. magic (4byte) 를 읽어 magic에 따라 정해진 (또는 가변)길이
class MagicBasedProtocol : public Protocol {
private:
    std::map<uint32_t, uint32_t> _magic_to_fixed_length;  // magic -> 고정길이
    std::map<uint32_t, std::function<uint32_t(const char*)>> _magic_to_length_calculator;  // magic -> 가변길이 계산
    
    uint32_t _current_magic;
    uint32_t _expected_length;
    bool _reading_header;
    
public:
    MagicBasedProtocol();
    
    // magic에 고정 길이 등록
    void registerMagic(uint32_t magic, uint32_t fixed_length);
    
    // magic에 가변 길이 계산 함수 등록 (헤더 데이터를 받아서 총 길이를 반환)
    void registerMagic(uint32_t magic, std::function<uint32_t(const char*)> length_calculator);
    
    size_t parseBuffer(struct evbuffer* input, const MessageCallback& callback) override;
    bool encodeToBuffer(struct evbuffer* output, const void* data, size_t length) override;
    void reset() override;
};

#endif // PROTOCOL_H