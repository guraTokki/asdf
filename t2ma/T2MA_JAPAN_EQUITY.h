#ifndef T2MA_JAPAN_EQUITY_H
#define T2MA_JAPAN_EQUITY_H

#include "T2MASystem.h"
#include "../HashMaster/HashMaster.h"
#include "../HashMaster/BinaryRecord.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <ctime>
#include <event2/event.h>

/**
 * @brief Japan Equity market data processing system
 * 
 * This class handles Japan equity market data processing by:
 * 1. Registering handlers for different message types (TREP_DATA, SISE_DATA, etc.)
 * 2. Registering pattern handlers for Japan equity RICs (*.T pattern)
 * 3. Registering scheduled events for statistics and monitoring
 * 4. Using business-specific variables for Japan equity processing
 */
class T2MA_JAPAN_EQUITY : public T2MASystem {
private:
    
public:
    T2MA_JAPAN_EQUITY(const T2MAConfig& config);
    ~T2MA_JAPAN_EQUITY() override;
    
    void handle_trep_data_message(const char* data, size_t size);
    void handle_control_message(const char* data, size_t size);

    void update_japan_equity_master(const std::string& ric, const std::map<std::string, std::string>& trepData) ;
    void send_japan_sise_data(const std::string& ric, const std::map<std::string, std::string>& trepData);

    /* message handler for trep data */
    void handle_japan_equity(const char* data, size_t size);
    void handle_german_equity(const char* data, size_t size);

    /* command_handler */
    void control_reload_master_command(const char* data, size_t size) {};
    void execute_helloworld(const char* data, size_t size) {
        std::cout << "execute_helloworld" << std::endl;
        std::cout << "size: " << size << std::endl;
        std::cout << "data: " << data << std::endl;
    };
    // handle_trep_data_message

    //
    void init_message_handlers() override {
        std::cout << "_msg_type별 핸들러 등록 at" << __FILE__ << ":" << __LINE__ << std::endl; 
        // _msg_type별 핸들러 등록
        // msg_type_handlers_[static_cast<char>(MsgType::TREP_DATA)] = 
        //     [this](const char* data, size_t size) { this->handle_trep_data_message(data, size); };
    }

    /* handlers_ 에 함수 등록, (MSG_TYPE별 handler 를 여기에서 등록한 함수중에서 선택) */
    void regist_handlers() override {
        REGISTER_MEMBER_HANDLER(handle_trep_data_message);
        REGISTER_MEMBER_HANDLER(handle_control_message);
        REGISTER_MEMBER_HANDLER(handle_japan_equity);
        REGISTER_MEMBER_HANDLER(handle_german_equity);

        REGISTER_MEMBER_HANDLER(execute_helloworld);
    }
    
    // Override initialize to setup schedulers after event_base is ready
    bool initialize() override {
        std::cout << "T2MA JAPAN EQUITY initialize()" << std::endl;
        // Call parent initialize first
        if (!T2MASystem::initialize()) {
            return false;
        }
        
        
        
        
        

        return true;
    }
    
    // 일본 주식 전용 설정 헬퍼 메소드
    std::string getJapanConfig(const std::string& key, const std::string& default_value = "") const {
        auto it = config_.extensions.find(key);
        return (it != config_.extensions.end()) ? it->second : default_value;
    }
    
    int getJapanConfigInt(const std::string& key, int default_value = 0) const {
        auto it = config_.extensions.find(key);
        if (it != config_.extensions.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return default_value;
            }
        }
        return default_value;
    }
    
    // Handler 설정 헬퍼 메소드
    bool isHandlerEnabled(const std::string& type, const std::string& handler_name) const {
        if (type == "message_types") {
            auto it = config_.handlers_ext.message_types.find(handler_name);
            if (it != config_.handlers_ext.message_types.end()) {
                auto enabled_it = it->second.find("enabled");
                return (enabled_it != it->second.end() && enabled_it->second == "true");
            }
        } else if (type == "control_commands") {
            auto it = config_.handlers_ext.control_commands.find(handler_name);
            if (it != config_.handlers_ext.control_commands.end()) {
                auto enabled_it = it->second.find("enabled");
                return (enabled_it != it->second.end() && enabled_it->second == "true");
            }
        }
        return false;
    }
    
    std::string getHandlerSymbol(const std::string& type, const std::string& handler_name) const {
        if (type == "message_types") {
            auto it = config_.handlers_ext.message_types.find(handler_name);
            if (it != config_.handlers_ext.message_types.end()) {
                auto symbol_it = it->second.find("symbol");
                if (symbol_it != it->second.end()) {
                    return symbol_it->second;
                }
            }
        } else if (type == "control_commands") {
            auto it = config_.handlers_ext.control_commands.find(handler_name);
            if (it != config_.handlers_ext.control_commands.end()) {
                auto symbol_it = it->second.find("symbol");
                if (symbol_it != it->second.end()) {
                    return symbol_it->second;
                }
            }
        }
        return "";
    }

    // Override scheduler handlers with Japan-specific implementations
    void control_heartbeat() override;

    // Override to add Japan-specific scheduler handlers
    void init_scheduler_handlers() override;

    // Japan-specific scheduler handler (not overriding base class)
    void control_heartbeat_japan();
};

// Factory function for creating T2MA_JAPAN_EQUITY instances  
extern "C" T2MASystem* create_t2ma_japan_equity(const T2MAConfig& config);

#endif // T2MA_JAPAN_EQUITY_H