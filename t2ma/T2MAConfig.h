#ifndef T2MA_CONFIG_H
#define T2MA_CONFIG_H

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include "../HashMaster/HashTable.h"  // For LogLevel enum
#include "../pubsub/SequenceStorage.h"

using namespace SimplePubSub;

// Subscriber configuration
struct SubscriberConfig {
    int client_id;
    std::string name;
    int pub_id;             // publisher id
    std::string pub_name;   // publisher name
    std::string type;  // "tcp" or "unix"
    std::string host;  // for tcp
    int port;          // for tcp
    std::string socket_path;  // for unix
    bool enabled;
    uint32_t topic_mask;
};

// T2MA System Configuration Structure
struct T2MAConfig {
    int id;
    std::string name;

    // File paths
    struct {
        std::string spec_file = "./HashMaster/config/spec_sample2.txt";
        std::string master_file = "./HashMaster/config/MASTERs";  // MasterManager config directory
        std::string csv_file = "./trep_data/O_NASDAQ_EQUITY_B_20250728.csv";
        std::string database_path = "t2ma_master";
    } files;
    
    // Record layouts
    struct {
        std::string master = "MMP_EQUITY_BASIC_MASTER";
        std::string sise = "EQUITY_SISE";
        std::string hoga = "EQUITY_1HOGA";
    } layouts;
    
    StorageType storage_type;
    // Pub-Sub configuration
    struct {
        struct {
            std::string database_name = "t2ma_pubsub_db";
            std::string unix_socket_path = "/tmp/t2ma.sock";
            std::string tcp_host = "127.0.0.1";
            int tcp_port = 9999;
        } publisher;
        
        std::vector<SubscriberConfig> subscribers;
    } pubsub;
    
    // Message Queue configuration
    struct {
        std::string name = "/t2ma_mq";
        std::string fallback_name = "/nasdaq_demo_mq";
        int max_messages = 10;
        int message_size = 512;
        std::string mode = "read";
    } messagequeue;
    
    // Monitoring settings
    struct {
        int stats_interval = 30;
        int log_interval = 100;
    } monitoring;
    
    // System behavior
    struct {
        std::string event_loop_mode = "EVLOOP_ONCE";
        bool auto_load_csv = true;
        bool enable_periodic_stats = true;
        std::string symbol = "";
    } system;
    
    // Plugin configuration
    struct {
        std::string module = "";
        std::string search_path = "";
        std::string symbol = "";
    } plugin;
    
    // Master name configuration
    std::string master = "JAPAN_EQUITY_MASTER";  // Default master to use

    // 상속 클래스별 동적 확장 설정
    std::map<std::string, std::string> extensions;  // 클래스별 확장 설정
    
    // Handler 확장 설정
    struct HandlerConfig {
        std::map<std::string, std::map<std::string, std::string>> message_types;  // message_types 섹션
        std::map<std::string, std::map<std::string, std::string>> control_commands; // control_commands 섹션
    } handlers_ext;
    
    // Scheduler 확장 설정  
    struct SchedulerItem {
        std::string name;
        bool enabled = true;
        std::string type;           // "once", "interval"
        std::string run_at;         // once용 실행 시간
        std::string start_time;     // interval용 시작 시간
        std::string end_time;       // interval용 종료 시간
        int interval_sec = 0;       // interval용 주기(초)
        std::string handler_symbol; // 호출할 핸들러 심볼
    };
    std::vector<SchedulerItem> schedulers_ext;
};

// Simple YAML-like config parser
class T2MAConfigParser {
private:
    std::map<std::string, std::string> config_values;
    std::vector<std::map<std::string, std::string>> subscriber_configs;
    std::map<std::string, std::map<std::string, std::string>> handlers_configs;  // handlers 섹션 파싱용
    std::vector<std::map<std::string, std::string>> schedulers_configs;  // schedulers 섹션 파싱용
    
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
    
public:
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << filename << std::endl;
            return false;
        }
        
        std::string line;
        std::vector<std::string> section_stack;
        std::map<std::string, std::string> current_subscriber;
        std::map<std::string, std::string> current_scheduler;
        bool in_subscriber_list = false;
        bool in_scheduler_list = false;
        
        while (std::getline(file, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            
            // Calculate indentation level
            int current_indent = 0;
            for (char c : line) {
                if (c == ' ') current_indent++;
                else if (c == '\t') current_indent += 4;
                else break;
            }
            int level = current_indent / 2;
            
            // Adjust section stack based on indentation
            if (level < section_stack.size()) {
                section_stack.resize(level);
                if (in_subscriber_list && level <= 1) {
                    in_subscriber_list = false;
                }
                if (in_scheduler_list && level <= 1) {
                    in_scheduler_list = false;
                }
            }
            
            // Handle array items (subscribers and schedulers)
            if (trimmed[0] == '-') {
                // 현재 컨텍스트에 따라 subscriber 또는 scheduler 처리
                std::string current_section = section_stack.empty() ? "" : section_stack.back();
                
                if (current_section == "subscribers") {
                    if (!current_subscriber.empty()) {
                        subscriber_configs.push_back(current_subscriber);
                        current_subscriber.clear();
                    }
                    in_subscriber_list = true;
                } else if (current_section == "schedulers") {
                    if (!current_scheduler.empty()) {
                        schedulers_configs.push_back(current_scheduler);
                        current_scheduler.clear();
                    }
                    in_scheduler_list = true;
                }
                
                // Parse inline key-value if present
                size_t space_pos = trimmed.find(' ', 1);
                if (space_pos != std::string::npos) {
                    std::string remainder = trim(trimmed.substr(space_pos));
                    size_t colon_pos = remainder.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string key = trim(remainder.substr(0, colon_pos));
                        std::string value = trim(remainder.substr(colon_pos + 1));
                        
                        // Remove inline comments
                        size_t comment_pos = value.find('#');
                        if (comment_pos != std::string::npos) {
                            value = trim(value.substr(0, comment_pos));
                        }
                        
                        if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                            value = value.substr(1, value.length() - 2);
                        }
                        current_subscriber[key] = value;
                    }
                }
                continue;
            }
            
            // Process section headers
            if (trimmed.back() == ':' && trimmed.find(' ') == std::string::npos) {
                std::string section = trimmed.substr(0, trimmed.length() - 1);
                if (level < section_stack.size()) {
                    section_stack.resize(level);
                }
                section_stack.push_back(section);
                
                if (section == "subscribers") {
                    in_subscriber_list = true;
                } else if (section == "schedulers") {
                    in_scheduler_list = true;
                }
            } else {
                // Process key-value pairs
                size_t colon_pos = trimmed.find(':');
                if (colon_pos != std::string::npos) {
                    std::string key = trim(trimmed.substr(0, colon_pos));
                    std::string value = trim(trimmed.substr(colon_pos + 1));
                    
                    // Remove inline comments (everything after # character)
                    size_t comment_pos = value.find('#');
                    if (comment_pos != std::string::npos) {
                        value = trim(value.substr(0, comment_pos));
                    }
                    
                    // Remove quotes
                    if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                        value = value.substr(1, value.length() - 2);
                    }
                    
                    if (in_subscriber_list) {
                        // Add to current subscriber
                        current_subscriber[key] = value;
                    } else if (in_scheduler_list) {
                        // Add to current scheduler
                        current_scheduler[key] = value;
                    } else {
                        // Build full key path for regular config
                        std::string full_key = "";
                        for (size_t i = 0; i < section_stack.size(); ++i) {
                            if (i > 0) full_key += ".";
                            full_key += section_stack[i];
                        }
                        if (!full_key.empty()) full_key += ".";
                        full_key += key;
                        
                        config_values[full_key] = value;
                    }
                }
            }
        }
        
        // Add last subscriber if exists
        if (!current_subscriber.empty()) {
            subscriber_configs.push_back(current_subscriber);
        }
        
        // Add last scheduler if exists
        if (!current_scheduler.empty()) {
            schedulers_configs.push_back(current_scheduler);
        }
        
        return true;
    }
    
    std::string getString(const std::string& key, const std::string& default_value = "") {
        auto it = config_values.find(key);
        return (it != config_values.end()) ? it->second : default_value;
    }
    
    int getInt(const std::string& key, int default_value = 0) {
        auto it = config_values.find(key);
        if (it != config_values.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return default_value;
            }
        }
        return default_value;
    }
    
    bool getBool(const std::string& key, bool default_value = false) {
        auto it = config_values.find(key);
        if (it != config_values.end()) {
            std::string value = it->second;
            return (value == "true" || value == "1" || value == "yes");
        }
        return default_value;
    }
    
    T2MAConfig parseConfig() {
        T2MAConfig config;
        
        config.id = getInt("id", config.id);
        config.name = getString("name", config.name);
        
        // File paths
        config.files.spec_file = getString("files.spec_file", config.files.spec_file);
        config.files.master_file = getString("files.master_file", config.files.master_file);
        config.files.csv_file = getString("files.csv_file", config.files.csv_file);
        config.files.database_path = getString("files.database_path", config.files.database_path);
        
        // Layouts
        config.layouts.master = getString("layouts.master", config.layouts.master);
        config.layouts.sise = getString("layouts.sise", config.layouts.sise);
        config.layouts.hoga = getString("layouts.hoga", config.layouts.hoga);
        
        // Pub-Sub publisher settings
        config.pubsub.publisher.database_name = getString("pubsub.publisher.database_name", config.pubsub.publisher.database_name);
        config.pubsub.publisher.unix_socket_path = getString("pubsub.publisher.unix_socket_path", config.pubsub.publisher.unix_socket_path);
        config.pubsub.publisher.tcp_host = getString("pubsub.publisher.tcp_host", config.pubsub.publisher.tcp_host);
        config.pubsub.publisher.tcp_port = getInt("pubsub.publisher.tcp_port", config.pubsub.publisher.tcp_port);
        
        // Storage type
        std::string storage_type = getString("sequence_storage_type", "file");
        if(storage_type == "file") {
            config.storage_type = StorageType::FILE_STORAGE;
        } else if(storage_type == "hashmaster") {
            config.storage_type = StorageType::HASHMASTER_STORAGE;
        }
        
        // Parse subscribers
        for (const auto& sub_config : subscriber_configs) {
            SubscriberConfig subscriber;
            
            auto client_id_it = sub_config.find("client_id");
            if (client_id_it != sub_config.end()) {
                subscriber.client_id = std::stoi(client_id_it->second);
            }
            
            auto name_it = sub_config.find("name");
            if (name_it != sub_config.end()) {
                subscriber.name = name_it->second;
            }
            
            auto pub_id_it = sub_config.find("pub_id");
            if (pub_id_it != sub_config.end()) {
                subscriber.pub_id = std::stoi(pub_id_it->second);
            }
            
            auto pub_name_it = sub_config.find("pub_name");
            if (pub_name_it != sub_config.end()) {
                subscriber.pub_name = pub_name_it->second;
            }
            
            auto type_it = sub_config.find("type");
            if (type_it != sub_config.end()) {
                subscriber.type = type_it->second;
            }
            
            auto host_it = sub_config.find("host");
            if (host_it != sub_config.end()) {
                subscriber.host = host_it->second;
            }
            
            auto port_it = sub_config.find("port");
            if (port_it != sub_config.end()) {
                subscriber.port = std::stoi(port_it->second);
            }
            
            auto socket_path_it = sub_config.find("socket_path");
            if (socket_path_it != sub_config.end()) {
                subscriber.socket_path = socket_path_it->second;
            }
            
            auto enabled_it = sub_config.find("enabled");
            if (enabled_it != sub_config.end()) {
                subscriber.enabled = (enabled_it->second == "true");
            }

            auto topic_mask_it = sub_config.find("topic_mask");
            if (topic_mask_it != sub_config.end()) {
                subscriber.topic_mask = std::stoi(topic_mask_it->second);
                std::cout << "DEBUG: Loaded topic_mask for subscriber " << subscriber.name << ": " << subscriber.topic_mask << std::endl;
            }

            config.pubsub.subscribers.push_back(subscriber);
        }
        
        // Message Queue settings
        config.messagequeue.name = getString("messagequeue.name", config.messagequeue.name);
        config.messagequeue.fallback_name = getString("messagequeue.fallback_name", config.messagequeue.fallback_name);
        config.messagequeue.max_messages = getInt("messagequeue.max_messages", config.messagequeue.max_messages);
        config.messagequeue.message_size = getInt("messagequeue.message_size", config.messagequeue.message_size);
        config.messagequeue.mode = getString("messagequeue.mode", config.messagequeue.mode);
        
        // Monitoring settings
        config.monitoring.stats_interval = getInt("monitoring.stats_interval", config.monitoring.stats_interval);
        config.monitoring.log_interval = getInt("monitoring.log_interval", config.monitoring.log_interval);
        
        // System settings
        config.system.event_loop_mode = getString("system.event_loop_mode", config.system.event_loop_mode);
        config.system.auto_load_csv = getBool("system.auto_load_csv", config.system.auto_load_csv);
        config.system.enable_periodic_stats = getBool("system.enable_periodic_stats", config.system.enable_periodic_stats);
        config.system.symbol = getString("system.symbol", config.system.symbol);
        
        // Plugin settings
        config.plugin.module = getString("plugin.module", config.plugin.module);
        config.plugin.search_path = getString("plugin.search_path", config.plugin.search_path);
        config.plugin.symbol = getString("plugin.symbol", config.plugin.symbol);

        // Master configuration
        config.master = getString("master", config.master);
        
        // Extensions settings (클래스별 확장 설정)
        for (const auto& pair : config_values) {
            if (pair.first.substr(0, 11) == "extensions.") {
                std::string key = pair.first.substr(11); // "extensions." 제거
                config.extensions[key] = pair.second;
            }
        }
        
        // Handlers settings 파싱
        for (const auto& pair : config_values) {
            // Debug: Check handler keys again
            if (pair.first.find("handlers") == 0) {
                std::cout << "PARSECONFIG: Processing handler key: " << pair.first << " = " << pair.second << std::endl;
            }
            
            // handlers.message_types.TREP_DATA.enabled 형태 파싱
            if (pair.first.length() > 23 && pair.first.substr(0, 23) == "handlers.message_types.") {
                std::string remainder = pair.first.substr(23);
                size_t dot_pos = remainder.find('.');
                if (dot_pos != std::string::npos) {
                    std::string msg_type = remainder.substr(0, dot_pos);
                    std::string property = remainder.substr(dot_pos + 1);
                    std::cout << "PARSECONFIG: Adding message_type: " << msg_type << "." << property << " = " << pair.second << std::endl;
                    config.handlers_ext.message_types[msg_type][property] = pair.second;
                }
            }
            // handlers.control_commands.STATS.enabled 형태 파싱  
            else if (pair.first.length() > 26 && pair.first.substr(0, 26) == "handlers.control_commands.") {
                std::string remainder = pair.first.substr(26);
                size_t dot_pos = remainder.find('.');
                if (dot_pos != std::string::npos) {
                    std::string cmd_type = remainder.substr(0, dot_pos);
                    std::string property = remainder.substr(dot_pos + 1);
                    std::cout << "PARSECONFIG: Adding control_command: " << cmd_type << "." << property << " = " << pair.second << std::endl;
                    config.handlers_ext.control_commands[cmd_type][property] = pair.second;
                }
            }
        }
        
        std::cout << "=== PARSECONFIG FINAL DEBUG ===" << std::endl;
        std::cout << "ParseConfig: Final handlers_ext.message_types size: " << config.handlers_ext.message_types.size() << std::endl;
        std::cout << "ParseConfig: Final handlers_ext.control_commands size: " << config.handlers_ext.control_commands.size() << std::endl;
        for (const auto& msg_type : config.handlers_ext.message_types) {
            std::cout << "ParseConfig Message Type: " << msg_type.first << std::endl;
            for (const auto& prop : msg_type.second) {
                std::cout << "  " << prop.first << " = " << prop.second << std::endl;
            }
        }
        std::cout << "=== PARSECONFIG FINAL DEBUG END ===" << std::endl;
        
        // Schedulers settings 파싱 (schedulers_configs에서 파싱된 데이터 사용)
        for (const auto& sched_config : schedulers_configs) {
            T2MAConfig::SchedulerItem item;
            
            auto name_it = sched_config.find("name");
            if (name_it != sched_config.end()) {
                item.name = name_it->second;
            }
            
            auto enabled_it = sched_config.find("enabled");
            if (enabled_it != sched_config.end()) {
                item.enabled = (enabled_it->second == "true");
            }
            
            auto type_it = sched_config.find("type");
            if (type_it != sched_config.end()) {
                item.type = type_it->second;
            }
            
            auto run_at_it = sched_config.find("run_at");
            if (run_at_it != sched_config.end()) {
                item.run_at = run_at_it->second;
            }
            
            auto start_time_it = sched_config.find("start_time");
            if (start_time_it != sched_config.end()) {
                item.start_time = start_time_it->second;
            }
            
            auto end_time_it = sched_config.find("end_time");
            if (end_time_it != sched_config.end()) {
                item.end_time = end_time_it->second;
            }
            
            auto interval_sec_it = sched_config.find("interval_sec");
            if (interval_sec_it != sched_config.end()) {
                item.interval_sec = std::stoi(interval_sec_it->second);
            }
            
            auto handler_symbol_it = sched_config.find("handler_symbol");
            if (handler_symbol_it != sched_config.end()) {
                item.handler_symbol = handler_symbol_it->second;
            }
            
            config.schedulers_ext.push_back(item);
        }
        
        return config;
    }
    
    // Debug getter
    const std::map<std::string, std::string>& get_config_values() const { 
        return config_values; 
    }
    
    void printConfig() {
        std::cout << "=== Loaded Configuration ===" << std::endl;
        for (const auto& pair : config_values) {
            std::cout << pair.first << ": " << pair.second << std::endl;
        }
        
        std::cout << "\nSubscribers:" << std::endl;
        for (size_t i = 0; i < subscriber_configs.size(); ++i) {
            std::cout << "  Subscriber " << i << ":" << std::endl;
            for (const auto& pair : subscriber_configs[i]) {
                std::cout << "    " << pair.first << ": " << pair.second << std::endl;
            }
        }
    }
};

#endif // T2MA_CONFIG_H