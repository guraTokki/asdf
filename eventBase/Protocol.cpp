#include "Protocol.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>  // for ntohl, htonl

// ==================== RawProtocol ====================
size_t RawProtocol::parseBuffer(struct evbuffer* input, const MessageCallback& callback) {
    size_t len = evbuffer_get_length(input);
    if (len > 0) {
        // Zero-Copy: evbuffer 내부 데이터에 직접 접근
        const char* data = (const char*)evbuffer_pullup(input, len);
        if (data) {
            callback(data, len);
            return len;  // 전체 데이터 소비
        }
    }
    return 0;  // 소비한 바이트 수
}

bool RawProtocol::encodeToBuffer(struct evbuffer* output, const void* data, size_t length) {
    // Zero-Copy: evbuffer에 직접 데이터 추가 (내부적으로 한 번만 복사)
    return evbuffer_add(output, data, length) == 0;
}

void RawProtocol::reset() {
    // No state to reset for raw protocol
}

// ==================== LengthPrefixedProtocol ====================
LengthPrefixedProtocol::LengthPrefixedProtocol() 
    : _expected_length(0), _reading_header(true) {
}

size_t LengthPrefixedProtocol::parseBuffer(struct evbuffer* input, const MessageCallback& callback) {
    size_t total_consumed = 0;
    
    while (true) {
        size_t available = evbuffer_get_length(input);
        
        if (_reading_header) {
            // 4바이트 길이 헤더를 읽어야 함
            if (available < sizeof(uint32_t)) {
                break;  // 더 많은 데이터 필요
            }
            
            uint32_t length_be;
            evbuffer_remove(input, &length_be, sizeof(uint32_t));
            _expected_length = ntohl(length_be);
            _reading_header = false;
            total_consumed += sizeof(uint32_t);
            
            std::cout << "[LengthPrefixed] Header read: expecting " << _expected_length << " bytes" << std::endl;
            available -= sizeof(uint32_t);
        }
        
        if (!_reading_header) {
            // 데이터 부분을 읽어야 함
            if (available < _expected_length) {
                break;  // 더 많은 데이터 필요
            }
            
            // Zero-Copy: evbuffer 내부 데이터에 직접 접근
            const char* data = (const char*)evbuffer_pullup(input, _expected_length);
            if (data) {
                std::cout << "[LengthPrefixed] Complete message read: " << _expected_length << " bytes" << std::endl;
                callback(data, _expected_length);
                
                evbuffer_drain(input, _expected_length);
                total_consumed += _expected_length;
                
                // 다음 메시지를 위해 상태 리셋
                _reading_header = true;
                _expected_length = 0;
            } else {
                break;  // pullup 실패
            }
        }
    }
    
    return total_consumed;
}

bool LengthPrefixedProtocol::encodeToBuffer(struct evbuffer* output, const void* data, size_t length) {
    // 길이를 네트워크 바이트 순서로 변환하여 헤더 추가
    uint32_t length_be = htonl((uint32_t)length);
    if (evbuffer_add(output, &length_be, sizeof(uint32_t)) != 0) {
        return false;
    }
    
    // 실제 데이터 추가
    return evbuffer_add(output, data, length) == 0;
}

void LengthPrefixedProtocol::reset() {
    _expected_length = 0;
    _reading_header = true;
    std::cout << "[LengthPrefixed] Protocol reset" << std::endl;
}

// ==================== MagicBasedProtocol ====================
MagicBasedProtocol::MagicBasedProtocol() 
    : _current_magic(0), _expected_length(0), _reading_header(true) {
}

void MagicBasedProtocol::registerMagic(uint32_t magic, uint32_t fixed_length) {
    _magic_to_fixed_length[magic] = fixed_length;
    std::cout << "[MagicBased] Registered magic 0x" << std::hex << magic 
              << " with fixed length " << std::dec << fixed_length << std::endl;
}

void MagicBasedProtocol::registerMagic(uint32_t magic, std::function<uint32_t(const char*)> length_calculator) {
    _magic_to_length_calculator[magic] = length_calculator;
    std::cout << "[MagicBased] Registered magic 0x" << std::hex << magic 
              << " with variable length calculator" << std::dec << std::endl;
}

size_t MagicBasedProtocol::parseBuffer(struct evbuffer* input, const MessageCallback& callback) {
    size_t total_consumed = 0;
    
    while (true) {
        size_t available = evbuffer_get_length(input);
        std::cout << "\n\n\t#### [MagicBased] Available: " << available << std::endl;
        std::cout << "\n\n\t#### [MagicBased] _reading_header: " << _reading_header << std::endl;
        if (_reading_header) {
            // 4바이트 magic을 읽어야 함
            if (available < sizeof(uint32_t)) {
                break;  // 더 많은 데이터 필요
            }
            
            uint32_t magic_be;
            evbuffer_remove(input, &magic_be, sizeof(uint32_t));
            _current_magic = ntohl(magic_be);
            total_consumed += sizeof(uint32_t);
            
            std::cout << "[MagicBased] Magic read: 0x" << std::hex << _current_magic << std::dec << std::endl;
            
            // Magic에 따른 길이 결정
            auto fixed_it = _magic_to_fixed_length.find(_current_magic);
            if (fixed_it != _magic_to_fixed_length.end()) {
                _expected_length = fixed_it->second;
                std::cout << "[MagicBased] Using fixed length: " << _expected_length << std::endl;
            } else {
                auto calc_it = _magic_to_length_calculator.find(_current_magic);
                if (calc_it != _magic_to_length_calculator.end()) {
                    // 가변 길이를 위한 추가 헤더가 필요할 수 있음
                    // 여기서는 간단히 다음 4바이트를 길이로 읽는다고 가정
                    if (available < sizeof(uint32_t)) {
                        // Magic 다시 버퍼에 넣고 나중에 다시 처리
                        uint32_t magic_be_restore = htonl(_current_magic);
                        evbuffer_prepend(input, &magic_be_restore, sizeof(uint32_t));
                        total_consumed -= sizeof(uint32_t);
                        break;
                    }
                    
                    char length_header[4];
                    evbuffer_remove(input, length_header, sizeof(uint32_t));
                    _expected_length = calc_it->second(length_header);
                    total_consumed += sizeof(uint32_t);
                    std::cout << "[MagicBased] Using calculated length: " << _expected_length << std::endl;
                    available -= sizeof(uint32_t);
                } else {
                    std::cout << "[MagicBased] Unknown magic 0x" << std::hex << _current_magic 
                              << ", skipping..." << std::dec << std::endl;
                    _reading_header = true;
                    continue;
                }
            }
            
            _reading_header = false;
            available -= sizeof(uint32_t);
        }
        
        if (!_reading_header) {
            // 데이터 부분을 읽어야 함
            if (available < _expected_length) {
                break;  // 더 많은 데이터 필요
            }
            
            // Zero-Copy: evbuffer 내부 데이터에 직접 접근
            const char* data = (const char*)evbuffer_pullup(input, _expected_length);
            if (data) {
                std::cout << "[MagicBased] Complete message read: magic=0x" << std::hex << _current_magic
                          << ", length=" << std::dec << _expected_length << std::endl;
                
                callback(data, _expected_length);
                
                evbuffer_drain(input, _expected_length);
                total_consumed += _expected_length;
                
                // 다음 메시지를 위해 상태 리셋
                _reading_header = true;
                _current_magic = 0;
                _expected_length = 0;
            } else {
                break;  // pullup 실패
            }
        }
    }
    
    return total_consumed;
}

bool MagicBasedProtocol::encodeToBuffer(struct evbuffer* output, const void* data, size_t length) {
    // Magic을 네트워크 바이트 순서로 변환하여 헤더 추가
    // 기본적으로 첫 번째 등록된 magic을 사용하거나 고정 값 사용
    uint32_t magic = 0x12345678;  // 기본 magic 값
    if (!_magic_to_fixed_length.empty()) {
        magic = _magic_to_fixed_length.begin()->first;
    } else if (!_magic_to_length_calculator.empty()) {
        magic = _magic_to_length_calculator.begin()->first;
    }
    
    uint32_t magic_be = htonl(magic);
    if (evbuffer_add(output, &magic_be, sizeof(uint32_t)) != 0) {
        return false;
    }
    
    // 가변 길이 magic인 경우 길이 헤더도 추가
    auto calc_it = _magic_to_length_calculator.find(magic);
    if (calc_it != _magic_to_length_calculator.end()) {
        uint32_t length_be = htonl((uint32_t)length);
        if (evbuffer_add(output, &length_be, sizeof(uint32_t)) != 0) {
            return false;
        }
    }
    
    // 실제 데이터 추가
    return evbuffer_add(output, data, length) == 0;
}

void MagicBasedProtocol::reset() {
    _current_magic = 0;
    _expected_length = 0;
    _reading_header = true;
    std::cout << "[MagicBased] Protocol reset" << std::endl;
}