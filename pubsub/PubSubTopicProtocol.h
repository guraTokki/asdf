#ifndef PUBSUB_TOPIC_PROTOCOL_H
#define PUBSUB_TOPIC_PROTOCOL_H

#include "../eventBase/Protocol.h"

class PubSubTopicProtocol : public Protocol {
public:
    PubSubTopicProtocol();
    ~PubSubTopicProtocol();

    size_t parseBuffer(struct evbuffer* input, const MessageCallback& callback) override;
    bool encodeToBuffer(struct evbuffer* output, const void* data, size_t length) override;
    void reset() override;

    void registerMagic(uint32_t magic, uint32_t fixed_length);

private:
    std::map<uint32_t, uint32_t> _magic_to_fixed_length;  // magic -> 고정길이

    uint32_t _current_magic;
    uint32_t _expected_length;
    bool _reading_header;
};

#endif // PUBSUB_TOPIC_PROTOCOL_H