#include "T2MASystem.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <regex>
#include <event2/event.h>

void T2MASystem::setup_message_handlers() {
    // Debug: Print handler configuration
    const auto &handlers = config_.handlers_ext;
    std::cout << "=== HANDLERS DEBUG START ===" << std::endl;
    std::cout << "Message types count: " << handlers.message_types.size() << std::endl;
    std::cout << "Control commands count: " << handlers.control_commands.size() << std::endl;
    
    std::cout << "MESSAGE TYPES:" << std::endl;
    for(const auto& item: handlers.message_types) {
        std::cout << "  Type: " << item.first << std::endl;
        auto enabled_it = item.second.find("enabled");
        auto symbol_it = item.second.find("symbol");
        if(enabled_it != item.second.end() && enabled_it->second == "true") {
            std::string symbol = symbol_it->second;

            auto handler_it = handlers_.find(symbol);
            if(handler_it == handlers_.end()) {
                std::cerr << "❌ Handler symbol '" << symbol << "' not found for message handler '" << std::endl;
                continue;
            }
            auto msg_type = stringToMsgType(item.first);
            msg_type_handlers_[static_cast<char>(msg_type)] = handler_it->second ;
        }
        
        for(const auto& inner : item.second) {
            std::cout << "    " << inner.first << " = " << inner.second << std::endl;
        }
    }
}

void T2MASystem::setup_command_handlers() {
    const auto &handlers = config_.handlers_ext;
    std::cout << "CONTROL COMMANDS:" << std::endl;
    for(const auto& item: handlers.control_commands) {
        std::cout << "  Command: " << item.first << std::endl;
        auto enabled_it = item.second.find("enabled");
        auto symbol_it = item.second.find("symbol");
        if(enabled_it != item.second.end() && enabled_it->second == "true") {
            std::string symbol = symbol_it->second;

            auto handler_it = handlers_.find(symbol);
            if(handler_it == handlers_.end()) {
                std::cerr << "❌ Handler symbol '" << symbol << "' not found for command handler '" << std::endl;
                continue;
            }
            std::cout << " string handler 등록" << std::endl;
            std::cout << "item.first: " << item.first << std::endl;
            std::cout << "symbol: " << symbol << std::endl;
            string_handlers_[item.first] = handler_it->second ;
        }
        for(const auto& inner : item.second) {
            std::cout << "    " << inner.first << " = " << inner.second << std::endl;
        }
    }
    
    std::cout << "=== HANDLERS DEBUG END ===" << std::endl;
    
}

// Default scheduler handler functions (moved from T2MA_JAPAN_EQUITY)
void T2MASystem::init_scheduler_handlers() {
    std::cout << "🔧 Initializing scheduler handlers..." << std::endl;

    // Register default scheduler handler functions
    scheduler_handlers_["control_stats"] = [this]() { this->control_stats(); };
    scheduler_handlers_["control_reload_master"] = [this]() { this->control_reload_master(); };
    scheduler_handlers_["control_clear_stats"] = [this]() { this->control_clear_stats(); };
    scheduler_handlers_["control_heartbeat"] = [this]() { this->control_heartbeat(); };

    std::cout << "✓ Default scheduler handlers registered: " << scheduler_handlers_.size() << " handlers" << std::endl;
}

void T2MASystem::setup_schedulers() {
    std::cout << "⏰ Setting up schedulers..." << std::endl;

    const auto& schedulers = getSchedulers();
    for (const auto& sched_config : schedulers) {
        if (!sched_config.enabled) {
            std::cout << "⏸️  Scheduler '" << sched_config.name << "' is disabled, skipping" << std::endl;
            continue;
        }

        // Find handler
        auto handler_it = scheduler_handlers_.find(sched_config.handler_symbol);
        if (handler_it == scheduler_handlers_.end()) {
            std::cerr << "❌ Handler '" << sched_config.handler_symbol
                      << "' not found for scheduler '" << sched_config.name << "'" << std::endl;
            continue;
        }

        // Create scheduler data
        SchedulerData* sched_data = new SchedulerData;
        sched_data->instance = this;
        sched_data->config = sched_config;
        sched_data->handler = handler_it->second;

        // Create event based on scheduler type
        if (sched_config.type == "interval") {
            struct timeval interval;
            interval.tv_sec = sched_config.interval_sec;
            interval.tv_usec = 0;

            // Create persistent timer event
            sched_data->event_ptr = event_new(event_base_, -1, EV_PERSIST, scheduler_callback, sched_data);
            if (sched_data->event_ptr == nullptr) {
                std::cerr << "❌ Failed to create timer event for scheduler: " << sched_config.name << std::endl;
                delete sched_data;
                continue;
            }

            if (event_add(sched_data->event_ptr, &interval) < 0) {
                std::cerr << "❌ Failed to add timer event for scheduler: " << sched_config.name << std::endl;
                event_free(sched_data->event_ptr);
                delete sched_data;
                continue;
            }

            std::cout << "✅ Interval scheduler '" << sched_config.name
                      << "' set up with " << sched_config.interval_sec << "s interval" << std::endl;
        } else if (sched_config.type == "once") {
            // For one-time execution at a specific time
            auto next_time = getNextScheduleTime(sched_config);
            auto now = std::chrono::system_clock::now();
            auto delay_duration = std::chrono::duration_cast<std::chrono::seconds>(next_time - now);

            // Ensure delay is at least 1 second
            if (delay_duration.count() <= 0) {
                delay_duration = std::chrono::seconds(1);
            }

            struct timeval delay;
            delay.tv_sec = delay_duration.count();
            delay.tv_usec = 0;

            // Create one-time timer event
            sched_data->event_ptr = event_new(event_base_, -1, 0, scheduler_callback, sched_data);
            if (sched_data->event_ptr == nullptr) {
                std::cerr << "❌ Failed to create timer event for scheduler: " << sched_config.name << std::endl;
                delete sched_data;
                continue;
            }

            if (event_add(sched_data->event_ptr, &delay) < 0) {
                std::cerr << "❌ Failed to add timer event for scheduler: " << sched_config.name << std::endl;
                event_free(sched_data->event_ptr);
                delete sched_data;
                continue;
            }

            std::cout << "✅ Once scheduler '" << sched_config.name
                      << "' set up to run at " << sched_config.run_at
                      << " (in " << delay_duration.count() << " seconds)" << std::endl;
        } else if (sched_config.type == "cron") {
            // For cron-style scheduling, we'll use a 1-second timer and check if it's time to run
            std::cout << "⚠️  Cron scheduler type not fully implemented yet for '" << sched_config.name << "'" << std::endl;
            delete sched_data;
            continue;
        } else {
            std::cerr << "❌ Unknown scheduler type '" << sched_config.type
                      << "' for scheduler '" << sched_config.name << "'" << std::endl;
            delete sched_data;
            continue;
        }

        scheduled_data_.push_back(sched_data);
    }

    std::cout << "✓ Set up " << scheduled_data_.size() << " active schedulers" << std::endl;
}

void T2MASystem::cleanup_schedulers() {
    std::cout << "🧹 Cleaning up schedulers..." << std::endl;

    for (auto* sched_data : scheduled_data_) {
        if (sched_data->event_ptr) {
            event_del(sched_data->event_ptr);
            event_free(sched_data->event_ptr);
        }
        delete sched_data;
    }

    scheduled_data_.clear();
    scheduler_handlers_.clear();

    std::cout << "✓ Schedulers cleaned up" << std::endl;
}

// Schedule helper functions (moved from T2MA_JAPAN_EQUITY)
std::chrono::seconds T2MASystem::parseTimeToSeconds(const std::string& time_str) {
    std::regex time_regex(R"((\d{1,2}):(\d{2}):(\d{2}))");
    std::smatch match;

    if (std::regex_match(time_str, match, time_regex)) {
        int hours = std::stoi(match[1]);
        int minutes = std::stoi(match[2]);
        int seconds = std::stoi(match[3]);

        return std::chrono::seconds(hours * 3600 + minutes * 60 + seconds);
    }

    return std::chrono::seconds(0);
}

std::chrono::system_clock::time_point T2MASystem::getNextScheduleTime(const T2MAConfig::SchedulerItem& item) {
    auto now = std::chrono::system_clock::now();

    if (item.type == "interval") {
        return now + std::chrono::seconds(item.interval_sec);
    } else if (item.type == "once") {
        // Parse run_at time (HH:MM:SS format)
        auto target_seconds = parseTimeToSeconds(item.run_at);

        // Get today's date at 00:00:00
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto tm_now = *std::localtime(&time_t_now);
        tm_now.tm_hour = 0;
        tm_now.tm_min = 0;
        tm_now.tm_sec = 0;

        auto today_start = std::chrono::system_clock::from_time_t(std::mktime(&tm_now));
        auto target_time = today_start + target_seconds;

        // If target time is in the past, schedule for tomorrow
        if (target_time <= now) {
            target_time += std::chrono::hours(24);
        }

        return target_time;
    }

    // For other types, return next hour for now
    return now + std::chrono::hours(1);
}

bool T2MASystem::isWithinScheduleTime(const T2MAConfig::SchedulerItem& item) {
    // No time restriction if start_time is empty/immediate or end_time is empty/none
    if (item.start_time.empty() || item.start_time == "immediate" ||
        item.end_time.empty() || item.end_time == "none") {
        return true;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);

    auto current_seconds = std::chrono::seconds(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
    auto start_seconds = parseTimeToSeconds(item.start_time);
    auto end_seconds = parseTimeToSeconds(item.end_time);

    return current_seconds >= start_seconds && current_seconds <= end_seconds;
}

// Default scheduler handler implementations
void T2MASystem::control_stats() {
    std::cout << "📊 [Scheduler] Statistics report:" << std::endl;
    print_statistics();
}

void T2MASystem::control_reload_master() {
    std::cout << "🔄 [Scheduler] Reloading master data..." << std::endl;
    reload_master_data();
}

void T2MASystem::control_clear_stats() {
    std::cout << "🧹 [Scheduler] Clearing statistics..." << std::endl;
    clear_statistics();
}

void T2MASystem::control_heartbeat() {
    std::cout << "💗 [Scheduler] Heartbeat - System is running" << std::endl;
    // Additional heartbeat logic can be added here
}

// libevent callback wrapper (moved from T2MA_JAPAN_EQUITY)
void T2MASystem::scheduler_callback(evutil_socket_t /*fd*/, short /*what*/, void* arg) {
    SchedulerData* sched_data = static_cast<SchedulerData*>(arg);

    try {
        // Check if within schedule time
        if (sched_data->instance->isWithinScheduleTime(sched_data->config)) {
            // Execute handler
            sched_data->handler();
        } else {
            std::cout << "⏸️  Scheduler '" << sched_data->config.name
                      << "' skipped - outside scheduled time" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in scheduler '" << sched_data->config.name
                  << "': " << e.what() << std::endl;
    }
}