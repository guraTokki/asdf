#include "T2MA_JAPAN_EQUITY.h"

// Factory function for creating T2MA_JAPAN_EQUITY instances
extern "C" T2MASystem* create_t2ma_japan_equity(const T2MAConfig& config) {
    return new T2MA_JAPAN_EQUITY(config);
}

// Export function for shared library
extern "C" void destroy_t2ma_japan_equity(T2MASystem* instance) {
    delete instance;
}
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <regex>
#include <event2/event.h>

T2MA_JAPAN_EQUITY::T2MA_JAPAN_EQUITY(const T2MAConfig& config) : T2MASystem(config) {
    std::cout << "=== T2MA_JAPAN_EQUITY 초기화 시작 ===" << std::endl;
    
    // 먼저 핸들러 등록 (스케줄러 설정 포함)
    regist_handlers();
    
    // 일본 주식 전용 설정 로드 및 출력
    std::cout << "\n=== 일본 주식 시장 전용 설정 ===" << std::endl;
    std::cout << "장 시작 시간: " << getJapanConfig("japan_market_open_time", "09:00:00") << std::endl;
    std::cout << "장 종료 시간: " << getJapanConfig("japan_market_close_time", "15:00:00") << std::endl;
    std::cout << "통화: " << getJapanConfig("japan_currency", "JPY") << std::endl;
    std::cout << "시간대: " << getJapanConfig("japan_timezone", "Asia/Tokyo") << std::endl;
    std::cout << "거래 단위: " << getJapanConfigInt("japan_lot_size", 100) << "주" << std::endl;
    std::cout << "결제일: T+" << getJapanConfigInt("japan_settlement_days", 2) << std::endl;
    
    // Handler 설정 확인
    std::cout << "\n=== Handler 설정 ===" << std::endl;
    std::cout << "TREP_DATA 핸들러 활성화: " << (isHandlerEnabled("message_types", "TREP_DATA") ? "예" : "아니오") << std::endl;
    std::cout << "TREP_DATA 핸들러 심볼: " << getHandlerSymbol("message_types", "TREP_DATA") << std::endl;
    std::cout << "CONTROL 핸들러 활성화: " << (isHandlerEnabled("message_types", "CONTROL") ? "예" : "아니오") << std::endl;
    
    // Scheduler는 이제 T2MASystem에서 관리됩니다
    
    std::cout << "=== T2MA_JAPAN_EQUITY 초기화 완료 ===" << std::endl;
}

T2MA_JAPAN_EQUITY::~T2MA_JAPAN_EQUITY() {
    // Scheduler cleanup은 이제 T2MASystem에서 처리됩니다
}


void T2MA_JAPAN_EQUITY::handle_trep_data_message(const char* data, size_t size) {
    std::cout << "T2MA_JAPAN_EQUITY::handle_trep_data_message called with size: " << size << std::endl;
    std::string trep_line(data, size);
    std::cout << "TREP DATA : " << trep_line << std::endl;

    if (trep_line.empty()) return;
        
    // TREP 데이터 파싱
    auto trepData = TrepParser::parseLine(trep_line);
    
    // RIC 코드 추출
    auto ricIt = trepData.find("0");
    if (ricIt == trepData.end()) {
        std::cout <<  "can not find ric fid " << std::endl;
        return;
    }
    
    std::string ric = ricIt->second;
    
    // 1. 일본 주식 마스터 업데이트
    update_japan_equity_master(ric, trepData);
    master_update_count_++;
    processed_count_++;
}

// 일본 주식 마스터 업데이트 (RAFR 코드 기반)
void T2MA_JAPAN_EQUITY::update_japan_equity_master(const std::string& ric, const std::map<std::string, std::string>& trepData) {
    BinaryRecord record(masterLayout_);
    
    char* result = active_master_->get_by_primary(ric.c_str());
    if (!result) {
        std::cout << "일본 주식 마스터에 없는 RIC: " << ric << std::endl;
        /* 마스터 추가 기능 주석처리
        record.setString("RIC_CD", ric);
        
        // 일본 주식 기본 정보 설정
        if (ric.find(".T") != std::string::npos) {
            record.setString("EXCHG_CD", "TSE");
        } else if (ric.find(".OS") != std::string::npos) {
            record.setString("EXCHG_CD", "OSE");
        }
        record.setString("CUR_CD", "JPY");
        if (active_master_->put(ric.c_str(), record.getBuffer(), record.getSize()) == 0) {
            std::cout << " 마스터 추가 성공 RIC: " << ric << std::endl;
            result = active_master_->get_by_primary(ric.c_str());
        } else {
            std::cout << " 마스터 추가 실패 RIC: " << ric << std::endl;
        }
        */
        return ;
    }
    record.setBuffer(result, false);

    // int trd_unit = record.getInt("TRD_UNIT"); // 사용하지 않으므로 주석처리
    
    bool svol_changed = false;
    bool trd_prc_changed = false;
    bool close_prc_updated = false;
    // 일본 주식 TREP FID 매핑 (RAFR 코드 기반)
    for (const auto& pair : trepData) {
        const std::string& fid = pair.first;
        const std::string& value = pair.second;
        
        if (value == "blank" || value.empty()) continue;
        
        // RAFR 코드의 FID 매핑 따름
        if (fid == "6") {          // 현재가 (일본 TREP)
            if(record.getString("TRD_PRC") != value) {
                record.setString("TRD_PRC", value);
                trd_prc_changed = true;
                std::cout << ric << " trd_prc: " << value << " :: " << record.getString("TRD_PRC") << std::endl;
            }
            std::cout << "\n\n\t\tMASTER TRD_PRC:" << record.getString("TRD_PRC") << " trep TRD_PRC:" << value << "\n\n" << std::endl;
        } else if (fid == "12") {  // 고가 (일본)
            record.setString("HIGH_PRC", value);
        } else if (fid == "13") {  // 저가 (일본)
            record.setString("LOW_PRC", value);
        } else if (fid == "19") {  // 시가 (일본)
            record.setString("OPEN_PRC", value);
        } else if (fid == "22") {  // 매수호가 (일본)
            record.setString("BID_PRC", value);
        } else if (fid == "25") {  // 매도호가 (일본)
            record.setString("ASK_PRC", value);
        } else if (fid == "30") {  // 매수잔량
            record.setString("BID_SIZE", value);
        } else if (fid == "31") {  // 매도잔량
            record.setString("ASK_SIZE", value);
        } else if (fid == "178") { // 체결량 (일본)
            record.setString("TRD_VOL", value);
        } else if (fid == "32") {  // 누적거래량 SVOL
            if(record.getString("SVOL") != value) {
                record.setString("SVOL", value);
                svol_changed =  true;
            }
        } else if (fid == "11") {  // 전일대비 (일본)
            record.setString("NET_CHNG", value);
        } else if (fid == "56") {  // 등락률 (일본)
            record.setString("PCT_CHNG", value);
        } else if (fid == "3372") { // 종가 (일본)
            record.setString("CLOSE_PRC", value);
            close_prc_updated = true;
        } else if (fid == "1465") { // 기준가
            record.setString("BASE_PRC", value);
        } else if (fid == "75") {   // 상한가
            record.setString("UPLIMIT", value);
        } else if (fid == "76") {   // 하한가
            record.setString("DNLIMIT", value);
        } else if (fid == "18") {   // 로컬시간
            record.setString("LOCAL_TM", value);
        } else if (fid == "379") {  // 체결시간
            record.setString("SAL_TM", value);
        } else if (fid == "32741") {    // 누적거래대금
            record.setString("SAMT", value);
        }
    }

    if(trepData.find("19") != trepData.end()) {
        std::string local_tm = set_time(record.getInt("SAL_TM"), 9*60*60);
        record.setString("OPEN_PRC_TM", local_tm);
    }
    if(trepData.find("12") != trepData.end()) {
        std::string local_tm = set_time(record.getInt("SAL_TM"), 9*60*60);
        record.setString("HIGH_PRC_TM", local_tm);
    }
    if(trepData.find("13") != trepData.end()) {
        std::string local_tm = set_time(record.getInt("SAL_TM"), 9*60*60);
        record.setString("LOW_PRC_TM", local_tm);
    }
    std::cout << " changed : " << trd_prc_changed << svol_changed << close_prc_updated << std::endl;
    if(trd_prc_changed || svol_changed) {
        send_japan_sise_data(ric, trepData);
    } else if(close_prc_updated) {
        send_japan_sise_data(ric, trepData);
    }
}

// 일본 주식 체결 데이터 송신 (RAFR process_sise_outfile 기반)
void T2MA_JAPAN_EQUITY::send_japan_sise_data(const std::string& ric, const std::map<std::string, std::string>& trepData) {
    BinaryRecord siseRecord(siseLayout_);
    
    BinaryRecord masterRecord(masterLayout_);
    
    char* masterResult = active_master_->get_by_primary(ric.c_str());
    if (!masterResult) {
        std::cout << "일본 주식 마스터에 없는 RIC: " << ric << std::endl;
        return ;
    }
    
    siseRecord.setString("DATA_GB", "A3");
    siseRecord.setString("INFO_GB", "22");
    siseRecord.setString("MKT_GB", "B");
    siseRecord.setString("EXCHG_CD", "TYO");
    siseRecord.setString("TRANS_TM", getDateTime());

    siseRecord.setString("RIC_CD", ric);
    siseRecord.setString("SYMBOL_CD", masterRecord.getString("SYMBOL_CD"));
    int trd_dt = masterRecord.getInt("TRD_DT");
    // std::cout << "TRD_DT : " << trd_dt << std::endl;
    
    const char *local_dt = masterRecord.getString("TRD_DT").c_str();
    const char *local_tm = masterRecord.getString("SAL_TM").c_str();
    siseRecord.setString("LOCAL_DT", cvt_gmt2local_ymd2((char*)local_dt, (char*)local_tm, nullptr, 32400));
    siseRecord.setString("LOCAL_TM", set_time(atoi(local_tm), 32400 /*9시*/));
    siseRecord.setString("KOR_DT", cvt_gmt2local_ymd2((char*)local_dt, (char*)local_tm, nullptr, 32400));
    siseRecord.setString("KOR_TM", set_time(atoi(local_tm), 32400 /*9시*/));
    
    siseRecord.setString("OPEN_PRC", masterRecord.getString("OPEN_PRC"));
    siseRecord.setString("HIGH_PRC", masterRecord.getString("HIGH_PRC"));
    siseRecord.setString("LOW_PRC", masterRecord.getString("LOW_PRC"));
    siseRecord.setString("TRD_PRC", masterRecord.getString("TRD_PRC"));

    double net_chng = masterRecord.getDouble("NET_CHNG");
    double pct_chng = masterRecord.getDouble("PCT_CHNG");
    double trd_prc = masterRecord.getDouble("TRD_PRC");
    if(net_chng == 0 || trd_prc == 0) {
        siseRecord.setString("NET_CHNG_SIGN", "3");
        siseRecord.setDouble("NET_CHNG", net_chng);
        siseRecord.setDouble("PCT_CHNG", pct_chng);
    } else if(net_chng > 0) {
        double uplimit = masterRecord.getDouble("UPLIMIT");
        if(trd_prc >= uplimit && uplimit > 0) {
            siseRecord.setString("NET_CHNG_SIGN", "1");
        } else {
            siseRecord.setString("NET_CHNG_SIGN", "2");
        }
        siseRecord.setDouble("NET_CHNG", net_chng);
        siseRecord.setDouble("PCT_CHNG", pct_chng);
    } else if(net_chng < 0) {
        double dnlimit = masterRecord.getDouble("DNLIMIT");
        if(trd_prc <= dnlimit && dnlimit > 0) {
            siseRecord.setString("NET_CHNG_SIGN", "4");
        } else {
            siseRecord.setString("NET_CHNG_SIGN", "5");
        }
        // net_chng *= (-1);
        siseRecord.setDouble("NET_CHNG", net_chng);
        siseRecord.setDouble("PCT_CHNG", pct_chng);
    } else {
        siseRecord.setString("NET_CHNG_SIGN", "3");
        siseRecord.setDouble("NET_CHNG", net_chng);
        siseRecord.setDouble("PCT_CHNG", pct_chng);
    }
    
    siseRecord.setString("OPEN_PRC_TM", masterRecord.getString("OPEN_PRC_TM"));
    siseRecord.setString("HIGH_PRC_TM", masterRecord.getString("HIGH_PRC_TM"));
    siseRecord.setString("LOW_PRC_TM", masterRecord.getString("LOW_PRC_TM"));

    siseRecord.setString("BID_PRC", masterRecord.getString("BID_PRC"));
    siseRecord.setString("ASK_PRC", masterRecord.getString("ASK_PRC"));
    siseRecord.setString("BID_SIZE", masterRecord.getString("BID_SIZE"));
    siseRecord.setString("ASK_SIZE", masterRecord.getString("ASK_SIZE"));

    siseRecord.setString("TRD_VOL", masterRecord.getString("TRD_VOL"));
    siseRecord.setString("SVOL", masterRecord.getString("SVOL"));
    long samt = masterRecord.getLong("SAMT");
    siseRecord.setInt("SAMT", (samt+500)/1000); // round

    siseRecord.setString("SESSION_GB", "0");
    if(trd_prc <= masterRecord.getDouble("BID_PRC")) {
        siseRecord.setString("TRAND_GB", "2");
    } else {
        siseRecord.setString("TRAND_GB", "1");
    }

    siseRecord.setString("TRD_GB", "0");

    siseRecord.init9Mode("AFTMKT_PRC", ' ');
    siseRecord.initXMode("TTYPE", ' ');
    siseRecord.initXMode("BASE_NET_CHNG_SIGN", ' ');
    siseRecord.init9Mode("BASE_NET_CHNG", ' ');
    siseRecord.init9Mode("BASE_PCT_CHNG", ' ');

    siseRecord.initXMode("FILLER", ' ');
    siseRecord.initXMode("FF", 0xff);

    // 기본 필드들 설정
    auto trdPrcIt = trepData.find("6");   // 현재가
    auto trdVolIt = trepData.find("178"); // 체결량
    auto netChngIt = trepData.find("11"); // 전일대비
    auto pctChngIt = trepData.find("56"); // 등락률
    // auto openPrcIt = trepData.find("19"); // 시가
    // auto highPrcIt = trepData.find("12"); // 고가
    // auto lowPrcIt = trepData.find("13");  // 저가
    auto svolIt = trepData.find("32");    // 누적거래량
    
    // if (trdPrcIt != trepData.end() && trdPrcIt->second != "blank") {
    //     siseRecord.setString("TRD_PRC", trdPrcIt->second);
    // }
    // if (trdVolIt != trepData.end() && trdVolIt->second != "blank") {
    //     siseRecord.setString("TRD_VOL", trdVolIt->second);
    // }
    // if (netChngIt != trepData.end() && netChngIt->second != "blank") {
    //     siseRecord.setString("NET_CHNG", netChngIt->second);
    // }
    // if (pctChngIt != trepData.end() && pctChngIt->second != "blank") {
    //     siseRecord.setString("PCT_CHNG", pctChngIt->second);
    // }
    // if (openPrcIt != trepData.end() && openPrcIt->second != "blank") {
    //     siseRecord.setString("OPEN_PRC", openPrcIt->second);
    // }
    // if (highPrcIt != trepData.end() && highPrcIt->second != "blank") {
    //     siseRecord.setString("HIGH_PRC", highPrcIt->second);
    // }
    // if (lowPrcIt != trepData.end() && lowPrcIt->second != "blank") {
    //     siseRecord.setString("LOW_PRC", lowPrcIt->second);
    // }
    // if (svolIt != trepData.end() && svolIt->second != "blank") {
    //     siseRecord.setString("SVOL", svolIt->second);
    // }
    
    // Publisher로 일본 주식 체결 데이터 송신
    publisher_->publish(DataTopic::TOPIC1, siseRecord.getBuffer(), siseRecord.getSize());
    siseRecord.dump();
    std::cout << "📈 일본주식 체결데이터 송신: " << ric;
    if (trdPrcIt != trepData.end()) {
        std::cout << " 가격=" << trdPrcIt->second << "¥";
    }
    if (trdVolIt != trepData.end()) {
        std::cout << " 량=" << trdVolIt->second;
    }
    std::cout << std::endl;
    std::cout << "SISE : " << std::string(siseRecord.getBuffer(), siseRecord.getSize()) << std::endl;
}
    
    

void T2MA_JAPAN_EQUITY::handle_control_message(const char* data, size_t size) {
    std::cout << "T2MA_JAPAN_EQUITY::handle_control_message called with size: " << size << std::endl;
    /* csv 문자열로 온다고 하고 첫번재 필드를 command로 약속하자 */
    std::string input_str(data, size);
    std::string control_cmd = input_str.substr(0, input_str.find(','));
    std::cout << "control_cmd: " << control_cmd << std::endl;
    if(control_cmd == ControlCommands::STATS) {
        std::cout << "STATS command received" << std::endl;
    } else if(control_cmd == ControlCommands::HEARTBEAT) {
        std::cout << "HEARTBEAT command received" << std::endl;
    } else {
        std::cerr << "Unknown command: " << control_cmd << std::endl;
    }
    // control command handler 호출
    auto handler_it = string_handlers_.find(control_cmd);
    if(handler_it != string_handlers_.end()) {
        handler_it->second(data, size);
    } else {
        std::cerr << "Unknown command: " << control_cmd << std::endl;
    }
}

void T2MA_JAPAN_EQUITY::handle_japan_equity(const char* data, size_t size) {
    std::cout << "T2MA_JAPAN_EQUITY::handle_japan_equity called with size: " << size << std::endl;
}

void T2MA_JAPAN_EQUITY::handle_german_equity(const char* data, size_t size) {
    std::cout << "T2MA_JAPAN_EQUITY::handle_german_equity called with size: " << size << std::endl;
}

// Japan-specific heartbeat implementation
void T2MA_JAPAN_EQUITY::control_heartbeat() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::cout << "💗 [JAPAN EQUITY HEARTBEAT " << timestamp << "] System Status:" << std::endl;
    std::cout << "   📊 Market: Tokyo Stock Exchange (TSE)" << std::endl;
    std::cout << "   🕐 Local Time: " << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S JST") << std::endl;

    // Market status based on Tokyo time
    auto tm_now = *std::localtime(&time_t_now);
    int current_time = tm_now.tm_hour * 100 + tm_now.tm_min;

    std::string market_status;
    if (current_time >= 900 && current_time <= 1130) {
        market_status = "🟢 OPEN (Morning Session)";
    } else if (current_time >= 1230 && current_time <= 1500) {
        market_status = "🟢 OPEN (Afternoon Session)";
    } else if (current_time > 1500 && current_time <= 1700) {
        market_status = "🟡 AFTER HOURS";
    } else {
        market_status = "🔴 CLOSED";
    }
    std::cout << "   📈 Market Status: " << market_status << std::endl;

    // Japan-specific configuration status
    std::cout << "   🏦 Currency: " << getJapanConfig("japan_currency", "JPY") << std::endl;
    std::cout << "   📦 Lot Size: " << getJapanConfigInt("japan_lot_size", 100) << " shares" << std::endl;
    std::cout << "   🗓️  Settlement: T+" << getJapanConfigInt("japan_settlement_days", 2) << std::endl;

    // System health info
    if (master_manager_) {
        std::cout << "   🗂️  Master Manager: READY" << std::endl;
    }
    if (publisher_) {
        std::cout << "   📡 Publisher: " << publisher_->get_client_count() << " clients connected" << std::endl;
        std::cout << "   🔢 Current Sequence: " << publisher_->get_current_sequence() << std::endl;
    }

    // Processing statistics
    std::cout << "   📈 Processed Messages: " << processed_count_ << std::endl;
    std::cout << "   🔄 Master Updates: " << master_update_count_ << std::endl;
    std::cout << "   📊 Market Data: " << sise_count_ << std::endl;

    std::cout << "   ✅ Japan Equity System ALIVE and HEALTHY" << std::endl;
}

// Japan-specific heartbeat function (separate from override)
void T2MA_JAPAN_EQUITY::control_heartbeat_japan() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::cout << "🇯🇵 [JAPAN SPECIFIC HEARTBEAT " << timestamp << "] Japan Market Monitor:" << std::endl;
    std::cout << "   🏛️  Exchange: Tokyo Stock Exchange (TSE)" << std::endl;
    std::cout << "   ⏰ JST Time: " << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S JST") << std::endl;

    // Enhanced Japan market status with more detail
    auto tm_now = *std::localtime(&time_t_now);
    int current_time = tm_now.tm_hour * 100 + tm_now.tm_min;

    std::string detailed_status;
    if (current_time >= 830 && current_time < 900) {
        detailed_status = "🔵 PRE-MARKET (Orders accepted)";
    } else if (current_time >= 900 && current_time <= 1130) {
        detailed_status = "🟢 MORNING SESSION (Active Trading)";
    } else if (current_time > 1130 && current_time < 1230) {
        detailed_status = "🟡 LUNCH BREAK";
    } else if (current_time >= 1230 && current_time <= 1500) {
        detailed_status = "🟢 AFTERNOON SESSION (Active Trading)";
    } else if (current_time > 1500 && current_time <= 1700) {
        detailed_status = "🟠 AFTER HOURS (ToSTNeT Trading)";
    } else {
        detailed_status = "🔴 MARKET CLOSED";
    }
    std::cout << "   📊 Trading Status: " << detailed_status << std::endl;

    // Japan-specific market indices (mock data for demo)
    std::cout << "   📈 Market Indices:" << std::endl;
    std::cout << "      - Nikkei 225: 33,486.89 (+0.25%)" << std::endl;
    std::cout << "      - TOPIX: 2,418.74 (+0.15%)" << std::endl;

    // Japan-specific configuration
    std::cout << "   ⚙️  Japan Config:" << std::endl;
    std::cout << "      - Currency: " << getJapanConfig("japan_currency", "JPY") << std::endl;
    std::cout << "      - Standard Lot: " << getJapanConfigInt("japan_lot_size", 100) << " shares" << std::endl;
    std::cout << "      - Settlement: T+" << getJapanConfigInt("japan_settlement_days", 2) << std::endl;
    std::cout << "      - Trading Hours: 09:00-11:30, 12:30-15:00 JST" << std::endl;

    // System status
    std::cout << "   🖥️  System Health:" << std::endl;
    if (master_manager_) {
        std::cout << "      - Japan Equity Master: ✅ READY" << std::endl;
    }
    if (publisher_) {
        std::cout << "      - Active Clients: " << publisher_->get_client_count() << std::endl;
        std::cout << "      - Current Sequence: " << publisher_->get_current_sequence() << std::endl;
    }

    // Processing statistics
    std::cout << "   📊 Processing Stats:" << std::endl;
    std::cout << "      - Total Messages: " << processed_count_ << std::endl;
    std::cout << "      - Master Updates: " << master_update_count_ << std::endl;
    std::cout << "      - SISE Data: " << sise_count_ << std::endl;

    std::cout << "   🎌 Japan Equity System - Operating Normally" << std::endl;
}

// Override scheduler handlers initialization to add Japan-specific handlers
void T2MA_JAPAN_EQUITY::init_scheduler_handlers() {
    std::cout << "🔧 Initializing Japan Equity scheduler handlers..." << std::endl;

    // Call parent implementation to get default handlers
    T2MASystem::init_scheduler_handlers();

    // Add Japan-specific scheduler handlers
    scheduler_handlers_["control_heartbeat_japan"] = [this]() { this->control_heartbeat_japan(); };

    std::cout << "✓ Japan Equity scheduler handlers registered: " << scheduler_handlers_.size() << " handlers" << std::endl;
}

