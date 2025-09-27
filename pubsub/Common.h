#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <queue>
#include <stack>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>

namespace SimplePubSub {

// Data topics for market data classification
enum DataTopic : uint32_t {
    TOPIC1 = 1,      // ex:) 체결 데이터 (거래량, 가격 등)
    TOPIC2 = 2,      // ex:) 호가 데이터 (매수/매도 호가)
    MISC = 4,       // 기타 데이터 (회사명, 날짜, 메타데이터)
    ALL_TOPICS = 7  // 모든 토픽 (TRADE | QUOTE | MISC)
};

// Socket type enumeration
enum SocketType {
    UNIX_SOCKET,
    TCP_SOCKET
};

// Client status enumeration
enum ClientStatus {
    CLIENT_CONNECTED,   // 연결됨
    CLIENT_RECOVERY_NEEDED, // 복구 필요
    CLIENT_RECOVERING,  // DB에서 과거 데이터 복구 중
    CLIENT_CATCHING_UP, // Recovery 중 누락된 실시간 데이터 따라잡기
    CLIENT_ONLINE,      // 온라인 (실시간 데이터 수신)
    CLIENT_OFFLINE      // 오프라인 (연결 문제로 비활성)
};

// Message structures
struct TopicMessage {
    uint32_t magic;         // 0xTOPICMSG
    DataTopic topic;        // 메시지 토픽
    uint32_t global_seq;    // 전체 발행 시퀀스 번호 (DB 저장용)
    uint32_t topic_seq;     // 토픽별 시퀀스 번호 (클라이언트 수신용)
    uint64_t timestamp;     // 타임스탬프 (nanoseconds)
    uint32_t data_size;     // 데이터 크기
    char data[0];          // 실제 데이터
};

struct SubscriptionRequest {
    uint32_t magic;           // 0xSUBSCRIB
    uint32_t client_id;       // 클라이언트 식별자
    uint32_t topic_mask;      // 구독할 토픽 마스크 (비트 OR)
    uint32_t last_seq;        // 마지막 수신 시퀀스
    char client_name[64];     // 클라이언트 이름
};

struct SubscriptionResponse {
    uint32_t magic;           // 0xSUBSCOK
    uint32_t result;          // 0: 성공, 1: 실패
    uint32_t approved_topics; // 승인된 토픽 마스크
    uint32_t current_seq;     // 현재 global sequence 번호
};

struct RecoveryRequest {
    uint32_t magic;           // 0xRECOVREQ
    uint32_t client_id;       // 클라이언트 식별자
    uint32_t topic_mask;      // 복구할 토픽 마스크
    uint32_t last_seq;        // 마지막 수신한 global sequence 번호
};

struct RecoveryResponse {
    uint32_t magic;           // 0xRECOVRES
    uint32_t result;          // 0: 성공, 1: 실패
    uint32_t start_seq;       // 복구 시작 global sequence
    uint32_t end_seq;         // 복구 종료 global sequence
    uint32_t total_messages;  // 전송할 총 메시지 수
};

struct RecoveryComplete {
    uint32_t magic;           // 0xRECOVCMP
    uint32_t total_sent;      // 실제 전송된 메시지 수
    uint64_t timestamp;       // 복구 완료 시간
};

// Pending message structure for Gap-Free Recovery
struct PendingMessage {
    DataTopic topic;
    uint32_t global_seq;
    uint32_t topic_seq;
    std::vector<char> data;
    uint64_t timestamp;
    
    PendingMessage(DataTopic t, uint32_t g_seq, uint32_t t_seq, const char* msg_data, size_t size)
        : topic(t), global_seq(g_seq), topic_seq(t_seq), data(msg_data, msg_data + size), 
          timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count()) {}
};
// Forward declaration removed - defined in SimplePublisherV2.h
// Client information structure
struct ClientInfo {
/*
    int fd;
    SocketType type;
    std::string address;
    ClientStatus status;
    uint32_t client_id;
    std::string client_name;
    uint32_t topic_mask;      // 구독 토픽
    uint32_t last_sent_seq;   // 마지막 전송한 sequence
    uint64_t connect_time;
    uint64_t recovery_start_time;
    uint64_t messages_sent;
    uint64_t recovery_messages_sent;
    
    // Gap-Free Recovery fields (Command 패턴으로 single-thread 처리)
    uint32_t recovery_target_seq;  // Recovery 목표 sequence (atomic하게 캡처된 값)
    std::queue<PendingMessage> pending_messages;  // Recovery 중 누적된 메시지들
*/
    uint32_t client_id;
    int fd;
    bufferevent *bev;
    ClientStatus status;
    uint32_t topic_mask;
    uint32_t last_sent_seq;
    void* parent;
    std::mutex mu;

    // 복구 중 실시간 발행 메시지 버퍼
    std::queue<PendingMessage> pending_messages;
};

/* 미사용
// Recovery task structure
struct RecoveryTask {
    int client_fd;
    uint32_t client_id;
    uint32_t topic_mask;
    uint32_t start_seq;
    uint32_t end_seq;
    uint64_t request_time;
};
*/

// Message constants
constexpr uint32_t MAGIC_TOPIC_MSG = 0x544F5049;     // 'TOPI'
constexpr uint32_t MAGIC_SUBSCRIBE = 0x53554253;     // 'SUBS'
constexpr uint32_t MAGIC_SUB_OK = 0x53554F4B;        // 'SUOK'
constexpr uint32_t MAGIC_RECOVERY_REQ = 0x52454352;  // 'RECR'
constexpr uint32_t MAGIC_RECOVERY_RES = 0x52454353;  // 'RECS'
constexpr uint32_t MAGIC_RECOVERY_CMP = 0x52454343;  // 'RECC'

inline std::string magic_to_string(uint32_t magic) {
    switch(magic) {
        case MAGIC_TOPIC_MSG: return "TOPI";
        case MAGIC_SUBSCRIBE: return "SUBS";
        case MAGIC_SUB_OK:  return "SUOK";
        case MAGIC_RECOVERY_REQ: return "RECR";
        case MAGIC_RECOVERY_RES: return "RECS";
        case MAGIC_RECOVERY_CMP: return "RECC";
        default: return "UNKNOWN";
    }
}

// Utility functions
inline uint64_t get_current_timestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

inline std::string topic_to_string(DataTopic topic) {
    switch (topic) {
        case TOPIC1: return "TOPIC1";
        case TOPIC2: return "TOPIC2";
        case MISC: return "MISC";
        case ALL_TOPICS: return "ALL_TOPICS";
        default: return "UNKNOWN";
    }
}

inline bool is_topic_subscribed(uint32_t topic_mask, DataTopic topic) {
    return (topic_mask & static_cast<uint32_t>(topic)) != 0;
}



} // namespace SimplePubSub