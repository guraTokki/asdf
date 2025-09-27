#include "PubSubTopicProtocol.h"
#include "Common.h"
#include <iostream>

using namespace SimplePubSub;

PubSubTopicProtocol::PubSubTopicProtocol()
    : _current_magic(0), _expected_length(0), _reading_header(true) {
    // MAGIC_TOPIC_MSG는 가변 길이이므로 별도 처리하지 않고 고정 길이로 처리
    registerMagic(MAGIC_SUBSCRIBE, sizeof(SubscriptionRequest));
    registerMagic(MAGIC_SUB_OK, sizeof(SubscriptionResponse));
    registerMagic(MAGIC_RECOVERY_REQ, sizeof(RecoveryRequest));
    registerMagic(MAGIC_RECOVERY_RES, sizeof(RecoveryResponse));
    registerMagic(MAGIC_RECOVERY_CMP, sizeof(RecoveryComplete));
}

PubSubTopicProtocol::~PubSubTopicProtocol() {
}

size_t PubSubTopicProtocol::parseBuffer(struct evbuffer* input, const MessageCallback& callback) {
    size_t total_consumed = 0;
    
    std::cout << "[Protocol] parseBuffer called, available data: " << evbuffer_get_length(input) << " bytes" << std::endl;

    while (true) {
        size_t available = evbuffer_get_length(input);
        
        // std::cout << "[Protocol] Loop iteration, available: " << available << ", reading_header: " << _reading_header << std::endl;
        // std::cout << "\n\n\t#### [PubSubTopic] Loop iteration, available: " << available << ", reading_header: " << _reading_header << std::endl;
        if (_reading_header) {
            if (available < sizeof(uint32_t)) {
                std::cout << "\n\n\t#### [PubSubTopic] Break, available: " << available << ", reading_header: " << _reading_header << std::endl;
                break;
            }
            
            uint32_t magic_raw;
            evbuffer_copyout(input, &magic_raw, sizeof(uint32_t));
            _current_magic = magic_raw;  // 바이트 오더 변환 없이 직접 사용
            
            std::cout << "[Protocol] Read magic: 0x" << std::hex << _current_magic << std::dec << " (raw) " << magic_to_string(_current_magic) << std::endl;
            auto fixed_it = _magic_to_fixed_length.find(_current_magic);
            if (fixed_it != _magic_to_fixed_length.end()) {     // 고정 길이 헤더 발견
                _expected_length = fixed_it->second;
            } else if (_current_magic == MAGIC_TOPIC_MSG) {
                // MAGIC_TOPIC_MSG의 가변 길이 처리
                if (available >= sizeof(TopicMessage)) {
#if 0                    
                    TopicMessage *msg_header = (TopicMessage*)evbuffer_pullup(input, sizeof(TopicMessage));
                    if (msg_header != nullptr) {
                        _expected_length = sizeof(TopicMessage) + msg_header->data_size;
                    } else {
                        std::cout << "[Protocol] TopicMessage is nullptr. wait for more data" << std::endl;
                        break; // wait for more data
                    }
#else
                    TopicMessage msg_header;
                    evbuffer_copyout(input, &msg_header, sizeof(TopicMessage));
                    _expected_length = sizeof(TopicMessage) + msg_header.data_size;
#endif
                } else {
                    std::cout << "[Protocol] wait for more data expected_length: " << sizeof(TopicMessage) << " available: " << available << " at " << __FILE__ << ":" << __LINE__ << std::endl;
                    break; // wait for more data
                }
            } else {
                // Unknown magic, skip
                evbuffer_drain(input, sizeof(uint32_t));
                total_consumed += sizeof(uint32_t);
                _reading_header = true;
                continue;
            }
            _reading_header = false;
        }
        if (!_reading_header) {
            _reading_header = true;
            // 데이터 부분을 읽어야 함
            if (available < _expected_length) {
                std::cout << "[Protocol] wait for more data expected_length: " << _expected_length << " available: " << available << " at " << __FILE__ << ":" << __LINE__ << std::endl;
                break;  // 더 많은 데이터 필요
            }
            
            const char* data = (const char*)evbuffer_pullup(input, _expected_length);
            if (data) {
                std::cout << "[Protocol] Calling callback with " << _expected_length << " bytes" << std::endl;
                callback(data, _expected_length);
                evbuffer_drain(input, _expected_length);
                total_consumed += _expected_length;
                
                _expected_length = 0;
            } else {
                break;  // pullup 실패
            }
        }
    }
    return total_consumed;
}

void PubSubTopicProtocol::reset() {
    _current_magic = 0;
    _expected_length = 0;
    _reading_header = true;
    std::cout << "[PubSubTopic] Protocol reset" << std::endl;
}

void PubSubTopicProtocol::registerMagic(uint32_t magic, uint32_t fixed_length) {
    _magic_to_fixed_length[magic] = fixed_length;
}

bool PubSubTopicProtocol::encodeToBuffer(struct evbuffer* output, const void* data, size_t length) {
    if (evbuffer_add(output, data, length) == 0) {
        return true;
    }
    return false;
}