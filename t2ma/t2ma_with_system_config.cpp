#include "T2MASystem.h"
#include <dlfcn.h>
#include <thread>

// 전역 시스템 인스턴스
std::unique_ptr<T2MASystem> g_t2ma_system;
volatile sig_atomic_t g_shutdown_requested = 0;

// 시그널 핸들러 (signal-safe)
void signal_handler(int sig) {
    g_shutdown_requested = 1;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "    T2MA (Trade & Market Access) System" << std::endl;
    std::cout << "    Config-Based Implementation" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Config 파일 로드
    // std::string config_file = "./HashMaster/config/t2ma_config.yaml";
    std::string config_file = "HashMaster/config/t2ma_japan_equity_config.yaml";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    T2MAConfigParser parser;
    if (!parser.loadFromFile(config_file)) {
        std::cerr << "Failed to load config file: " << config_file << std::endl;
        return 1;
    }
    
    // Debug: Check if handlers are in config_values right after loadFromFile
    std::cout << "=== AFTER LOADFROMFILE DEBUG ===" << std::endl;
    const auto& config_values = parser.get_config_values(); // Need to add this getter
    std::cout << "LoadFromFile: Total config_values: " << config_values.size() << std::endl;
    int handler_count = 0;
    for (const auto& pair : config_values) {
        if (pair.first.find("handlers") == 0) {
            std::cout << "LoadFromFile Handler: " << pair.first << " = " << pair.second << std::endl;
            handler_count++;
        }
    }
    std::cout << "LoadFromFile: Found " << handler_count << " handler keys" << std::endl;
    std::cout << "=== AFTER LOADFROMFILE DEBUG END ===" << std::endl;
    
    T2MAConfig config = parser.parseConfig();
    
    std::cout << "✓ Configuration loaded from: " << config_file << std::endl;
    
    // Debug: Check handlers_ext in main after parseConfig
    std::cout << "=== MAIN CONFIG DEBUG ===" << std::endl;
    std::cout << "Main: handlers.message_types size: " << config.handlers_ext.message_types.size() << std::endl;
    std::cout << "Main: handlers.control_commands size: " << config.handlers_ext.control_commands.size() << std::endl;
    for (const auto& msg_type : config.handlers_ext.message_types) {
        std::cout << "Main Message Type: " << msg_type.first << std::endl;
        for (const auto& prop : msg_type.second) {
            std::cout << "  " << prop.first << " = " << prop.second << std::endl;
        }
    }
    std::cout << "=== MAIN CONFIG DEBUG END ===" << std::endl;
    
    // 시그널 핸들러 등록
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 플러그인 동적 로딩
    std::string path = config.plugin.search_path;
    std::string module = config.plugin.module;
    std::string symbol = config.plugin.symbol;
    
    if (path.empty()) path = "./t2ma";
    if (module.empty()) module = "libT2MA_JAPAN_EQUITY.so";
    if (symbol.empty()) symbol = "create_t2ma_japan_equity";
    
    void* handle = dlopen((path + "/" + module).c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "Cannot load plugin: " << dlerror() << std::endl;
        return 1;
    }
    
    using CreateFunc = T2MASystem* (*)(const T2MAConfig&);
    CreateFunc create = reinterpret_cast<CreateFunc>(dlsym(handle, symbol.c_str()));
    if (!create) {
        std::cerr << "Cannot find symbol " << symbol << ": " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }
    
    // 플러그인 인스턴스 생성 (config 전달)
    g_t2ma_system.reset(create(config));
    if (!g_t2ma_system) {
        std::cerr << "Failed to create T2MA system instance" << std::endl;
        dlclose(handle);
        return 1;
    }
    
    // 시스템 초기화 (config 전달)
    if (!g_t2ma_system->initialize()) {
        std::cerr << "Failed to initialize T2MA System" << std::endl;
        return 1;
    }
    
    // Config에서 auto_load_csv가 설정된 경우 자동으로 CSV 로딩
    if (config.system.auto_load_csv) {
        if (!g_t2ma_system->load_symbols_from_csv()) {
            std::cerr << "Failed to load symbols from CSV: " << config.files.csv_file << std::endl;
        }
    }
    
    // Config에서 periodic stats가 활성화된 경우
    std::thread stats_thread;
    if (config.system.enable_periodic_stats) {
        stats_thread = std::thread([&]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(config.monitoring.stats_interval));
                g_t2ma_system->print_statistics();
            }
        });
        stats_thread.detach();
    }
    
    std::cout << "\n시스템 시작 완료. TREP 데이터 처리 대기 중..." << std::endl;
    std::cout << "- MQ: " << config.messagequeue.name << "에서 TREP 데이터 수신" << std::endl;
    std::cout << "- Publisher: Unix socket(" << config.pubsub.publisher.unix_socket_path 
              << "), TCP(" << config.pubsub.publisher.tcp_port << ")" << std::endl;
    
    // 메인 루프를 별도 스레드에서 실행
    std::thread main_thread([&]() {
        g_t2ma_system->run();
    });

    // 메인 스레드에서 shutdown signal 대기
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutdown signal received, stopping system..." << std::endl;
    g_t2ma_system->stop();

    // 메인 스레드 종료 대기
    if (main_thread.joinable()) {
        main_thread.join();
    }

    std::cout << "T2MA System stopped." << std::endl;
    
    // 플러그인 정리
    g_t2ma_system.reset();
    if (handle) {
        dlclose(handle);
    }
    
    return 0;
}