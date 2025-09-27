#ifndef IPC_HEADER_H
#define IPC_HEADER_H

// IPC 헤더 구조체
#pragma pack(push, 1)
struct ipc_header {
    char _msg_type;      // 메시지 타입 식별자
    char _reserved;      // 예약 필드
    short _msg_size;     // 메시지 크기 (헤더 포함)
};
#pragma pack(pop)

// 메시지 타입 정의
enum class MsgType : char {
    TREP_DATA = 'T',     // TREP 데이터
    DIST_DATA = 'D',     // 분배 데이터
    BIN_DATA = 'B',     // Binary 데이터
    CONTROL = 'C',       // 제어 메시지
    STATUS = 'X',        // 상태 메시지
    HEARTBEAT = 'H',     // 하트비트
    MASTER_UPDATE = 'M', // 마스터 업데이트
    SISE_DATA = 'S',     // 시세 데이터
    HOGA_DATA = 'Q'      // 호가 데이터
};

// 제어 명령어 정의
namespace ControlCommands {
    static const char* START = "START";
    static const char* STOP = "STOP";
    static const char* STATS = "STATS";
    static const char* RELOAD_MASTER = "RELOAD_MASTER";
    static const char* CLEAR_STATS = "CLEAR_STATS";
    static const char* HEARTBEAT = "HEARTBEAT";
}

// IPC 유틸리티 함수들
#include <string>

inline std::string msgTypeToString(MsgType type) {
    switch(type) {
        case MsgType::TREP_DATA: return "TREP_DATA";
        case MsgType::DIST_DATA: return "DIST_DATA";
        case MsgType::BIN_DATA: return "BIN_DATA";
        case MsgType::CONTROL: return "CONTROL";
        case MsgType::STATUS: return "STATUS";
        case MsgType::HEARTBEAT: return "HEARTBEAT";
        case MsgType::MASTER_UPDATE: return "MASTER_UPDATE";
        case MsgType::SISE_DATA: return "SISE_DATA";
        case MsgType::HOGA_DATA: return "HOGA_DATA";
        default: return "UNKNOWN";
    }
}

inline MsgType charToMsgType(char c) {
    switch(c) {
        case 'T': return MsgType::TREP_DATA;
        case 'D': return MsgType::DIST_DATA;
        case 'B': return MsgType::BIN_DATA;
        case 'C': return MsgType::CONTROL;
        case 'X': return MsgType::STATUS;
        case 'H': return MsgType::HEARTBEAT;
        case 'M': return MsgType::MASTER_UPDATE;
        case 'S': return MsgType::SISE_DATA;
        case 'Q': return MsgType::HOGA_DATA;
        default: return MsgType::TREP_DATA; // 기본값
    }
}

inline MsgType stringToMsgType(std::string type) {
    if(type == msgTypeToString(MsgType::TREP_DATA)) return MsgType::TREP_DATA;
    else if(type == msgTypeToString(MsgType::DIST_DATA)) return MsgType::DIST_DATA;
    else if(type == msgTypeToString(MsgType::BIN_DATA)) return MsgType::BIN_DATA;
    else if(type == msgTypeToString(MsgType::CONTROL)) return MsgType::CONTROL;
    else if(type == msgTypeToString(MsgType::STATUS)) return MsgType::STATUS;
    else if(type == msgTypeToString(MsgType::HEARTBEAT)) return MsgType::HEARTBEAT;
    else if(type == msgTypeToString(MsgType::MASTER_UPDATE)) return MsgType::MASTER_UPDATE;
    else if(type == msgTypeToString(MsgType::SISE_DATA)) return MsgType::SISE_DATA;
    else if(type == msgTypeToString(MsgType::HOGA_DATA)) return MsgType::HOGA_DATA;
    else return MsgType::TREP_DATA;
}

#endif // IPC_HEADER_H