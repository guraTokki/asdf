#include "MasterManager.h"
#include "HashMaster.h"
#include "MemoryMaster.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <cstdarg>
#include <cstring>
#include <algorithm>

MasterManager::MasterManager(LogLevel log_level) : log_level_(log_level) {
    log(LOG_INFO, "MasterManager initialized with log level %d", static_cast<int>(log_level));
}

MasterManager::~MasterManager() {
    closeAllMasters();
    log(LOG_INFO, "MasterManager destroyed");
}

bool MasterManager::loadMasterConfigs(const std::string& config_directory) {
    config_directory_ = config_directory;
    master_infos_.clear();

    log(LOG_INFO, "Loading master configurations from directory: %s", config_directory.c_str());

    DIR* dir = opendir(config_directory.c_str());
    if (!dir) {
        log(LOG_ERROR, "Failed to open config directory: %s", config_directory.c_str());
        return false;
    }

    struct dirent* entry;
    int loaded_count = 0;

    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Skip non-YAML files
        if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".yaml") {
            continue;
        }

        std::string filepath = config_directory + "/" + filename;
        if (loadMasterConfigFile(filepath)) {
            loaded_count++;
        }
    }

    closedir(dir);

    log(LOG_INFO, "Loaded %d master configurations", loaded_count);
    return loaded_count > 0;
}

bool MasterManager::loadMasterConfigFile(const std::string& filepath) {
    try {
        std::cout << "Loading master config file: " << filepath << std::endl;
        // Parse YAML file into key-value map
        std::map<std::string, std::string> config_map = parseSimpleYAML(filepath);

        if (config_map.empty()) {
            log(LOG_ERROR, "Failed to parse YAML file: %s", filepath.c_str());
            return false;
        }

        // Parse basic info
        std::string name = config_map.count("name") ? config_map["name"] : "";
        std::string description = config_map.count("description") ? config_map["description"] : "";
        std::string layout = config_map.count("layout") ? config_map["layout"] : "";
        std::string type_str = config_map.count("master_type") ? config_map["master_type"] : "HashMaster";

        if (name.empty()) {
            log(LOG_ERROR, "Master name is required in %s", filepath.c_str());
            return false;
        }

        // Parse master type
        MasterType master_type = parseMasterType(type_str);

        // Parse master config
        MasterConfig config = parseMasterConfig(config_map);

        // Store master info
        MasterInfo info(name, description, layout, master_type, config);
        master_infos_[name] = info;

        log(LOG_INFO, "Loaded master config: %s (%s)", name.c_str(), info.getMasterTypeString().c_str());
        return true;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Exception while loading %s: %s", filepath.c_str(), e.what());
        return false;
    }
}

std::map<std::string, std::string> MasterManager::parseSimpleYAML(const std::string& filepath) {
    std::map<std::string, std::string> config_map;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        return config_map;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find colon separator
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);

        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Remove inline comments first
        size_t comment_pos = value.find('#');
        if (comment_pos != std::string::npos) {
            value = value.substr(0, comment_pos);
            value.erase(value.find_last_not_of(" \t") + 1);
        }

        // Remove quotes after removing comments
        if (value.length() >= 2 &&
            ((value[0] == '"' && value.back() == '"') ||
             (value[0] == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.length() - 2);
        }

        config_map[key] = value;
    }

    return config_map;
}

MasterConfig MasterManager::parseMasterConfig(const std::map<std::string, std::string>& config_map) {
    MasterConfig config;

    // Parse integer values
    auto parseInt = [&](const std::string& key, int& target) {
        if (config_map.count(key)) {
            target = std::stoi(config_map.at(key));
        }
    };

    // Parse boolean values
    auto parseBool = [&](const std::string& key, bool& target) {
        if (config_map.count(key)) {
            std::string value = config_map.at(key);
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            target = (value == "true" || value == "1" || value == "yes");
        }
    };

    // Parse string values
    auto parseString = [&](const std::string& key, std::string& target) {
        if (config_map.count(key)) {
            target = config_map.at(key);
        }
    };

    // Load all MasterConfig fields
    parseInt("max_record_count", config._max_record_count);
    parseInt("max_record_size", config._max_record_size);
    parseInt("hash_count", config._hash_count);
    parseInt("primary_field_len", config._primary_field_len);
    parseInt("secondary_field_len", config._secondary_field_len);
    parseBool("use_lock", config._use_lock);
    parseString("filename", config._filename);

    // Parse log level
    if (config_map.count("log_level")) {
        int log_level_int = std::stoi(config_map.at("log_level"));
        config._log_level = static_cast<LogLevel>(log_level_int);
    }

    // Calculate total size
    config._tot_size = config._max_record_count * config._max_record_size;

    return config;
}

MasterType MasterManager::parseMasterType(const std::string& type_str) {
    if (type_str == "MemoryMaster") {
        return MasterType::MEMORY_MASTER;
    } else if (type_str == "HashMaster") {
        return MasterType::HASH_MASTER;
    } else {
        log(LOG_WARNING, "Unknown master type '%s', defaulting to HashMaster", type_str.c_str());
        return MasterType::HASH_MASTER;
    }
}

std::unique_ptr<Master> MasterManager::createMasterInstance(const MasterInfo& info) {
    return createMaster(info.master_type, info.config);
}

std::unique_ptr<Master> MasterManager::createMaster(MasterType type, const MasterConfig& config) {
    switch (type) {
        case MasterType::HASH_MASTER: {
            // Convert MasterConfig to HashMasterConfig
            HashMasterConfig hash_config;
            hash_config._max_record_count = config._max_record_count;
            hash_config._max_record_size = config._max_record_size;
            hash_config._tot_size = config._tot_size;
            hash_config._hash_count = config._hash_count;
            hash_config._primary_field_len = config._primary_field_len;
            hash_config._secondary_field_len = config._secondary_field_len;
            hash_config._use_lock = config._use_lock;
            hash_config._filename = config._filename;
            hash_config._log_level = config._log_level;

            return std::make_unique<HashMaster>(hash_config);
        }

        case MasterType::MEMORY_MASTER: {
            // Convert MasterConfig to MemoryMasterConfig
            MemoryMasterConfig memory_config;
            memory_config._max_record_count = config._max_record_count;
            memory_config._max_record_size = config._max_record_size;
            memory_config._tot_size = config._tot_size;
            memory_config._hash_count = config._hash_count;
            memory_config._primary_field_len = config._primary_field_len;
            memory_config._secondary_field_len = config._secondary_field_len;
            memory_config._use_lock = config._use_lock;
            memory_config._filename = config._filename;
            memory_config._log_level = config._log_level;

            return std::make_unique<MemoryMaster>(memory_config);
        }

        default:
            return nullptr;
    }
}

void MasterManager::reload() {
    log(LOG_INFO, "Reloading master configurations");

    // Close existing masters
    closeAllMasters();

    // Reload configurations
    if (!config_directory_.empty()) {
        loadMasterConfigs(config_directory_);
    }
}

bool MasterManager::hasMaster(const std::string& name) const {
    return master_infos_.find(name) != master_infos_.end();
}

const MasterInfo* MasterManager::getMasterInfo(const std::string& name) const {
    auto it = master_infos_.find(name);
    return (it != master_infos_.end()) ? &it->second : nullptr;
}

std::vector<std::string> MasterManager::getMasterNames() const {
    std::vector<std::string> names;
    for (const auto& pair : master_infos_) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::string> MasterManager::getMasterNamesByType(MasterType type) const {
    std::vector<std::string> names;
    for (const auto& pair : master_infos_) {
        if (pair.second.master_type == type) {
            names.push_back(pair.first);
        }
    }
    return names;
}

Master* MasterManager::getMaster(const std::string& name) {
    // Check if master is already created and initialized
    auto it = masters_.find(name);
    if (it != masters_.end()) {
        return it->second.get();
    }

    // Create and initialize master
    return createMaster(name);
}

Master* MasterManager::createMaster(const std::string& name) {
    auto info_it = master_infos_.find(name);
    if (info_it == master_infos_.end()) {
        log(LOG_ERROR, "Master configuration not found: %s", name.c_str());
        return nullptr;
    }

    const MasterInfo& info = info_it->second;

    // Create master instance
    auto master = createMasterInstance(info);
    if (!master) {
        log(LOG_ERROR, "Failed to create master instance: %s", name.c_str());
        return nullptr;
    }

    // Initialize master
    int result = master->init();
    if (result != MASTER_OK) {
        log(LOG_ERROR, "Failed to initialize master %s: %d", name.c_str(), result);
        return nullptr;
    }

    log(LOG_INFO, "Created and initialized master: %s (%s)", name.c_str(), info.getMasterTypeString().c_str());

    Master* master_ptr = master.get();
    masters_[name] = std::move(master);

    return master_ptr;
}

bool MasterManager::initializeMaster(const std::string& name) {
    return getMaster(name) != nullptr;
}

void MasterManager::closeMaster(const std::string& name) {
    auto it = masters_.find(name);
    if (it != masters_.end()) {
        log(LOG_INFO, "Closing master: %s", name.c_str());
        masters_.erase(it);
    }
}

void MasterManager::closeAllMasters() {
    if (!masters_.empty()) {
        log(LOG_INFO, "Closing all masters (%zu)", masters_.size());
        masters_.clear();
    }
}

void MasterManager::displayAllMasterStats() const {
    std::cout << "=== MasterManager Statistics ===" << std::endl;
    std::cout << "Total configured masters: " << master_infos_.size() << std::endl;
    std::cout << "Active master instances: " << masters_.size() << std::endl;

    for (const auto& pair : masters_) {
        const std::string& name = pair.first;
        Master* master = pair.second.get();

        std::cout << "\n--- " << name << " ---" << std::endl;
        auto stats = master->get_statistics();
        std::cout << "Total records: " << stats.total_records << std::endl;
        std::cout << "Used records: " << stats.used_records << std::endl;
        std::cout << "Free records: " << stats.free_records << std::endl;
        std::cout << "Record utilization: " << stats.record_utilization << "%" << std::endl;
    }
}

void MasterManager::displayMasterInfo(const std::string& name) const {
    auto it = master_infos_.find(name);
    if (it == master_infos_.end()) {
        std::cout << "Master not found: " << name << std::endl;
        return;
    }

    const MasterInfo& info = it->second;
    std::cout << "=== Master Info: " << name << " ===" << std::endl;
    std::cout << "Description: " << info.description << std::endl;
    std::cout << "Type: " << info.getMasterTypeString() << std::endl;
    std::cout << "Layout: " << info.layout << std::endl;
    std::cout << "Max records: " << info.config._max_record_count << std::endl;
    std::cout << "Record size: " << info.config._max_record_size << std::endl;
    std::cout << "Hash count: " << info.config._hash_count << std::endl;
    std::cout << "Primary field length: " << info.config._primary_field_len << std::endl;
    std::cout << "Secondary field length: " << info.config._secondary_field_len << std::endl;
    std::cout << "Filename: " << info.config._filename << std::endl;

    // Check if instance is active
    auto master_it = masters_.find(name);
    if (master_it != masters_.end()) {
        std::cout << "Status: Active" << std::endl;
        auto stats = master_it->second->get_statistics();
        std::cout << "Used records: " << stats.used_records << std::endl;
        std::cout << "Free records: " << stats.free_records << std::endl;
    } else {
        std::cout << "Status: Not initialized" << std::endl;
    }
}

void MasterManager::displayMasterSummary() const {
    std::cout << "=== MasterManager Summary ===" << std::endl;
    std::cout << "Configuration directory: " << config_directory_ << std::endl;
    std::cout << "Total masters: " << master_infos_.size() << std::endl;
    std::cout << "Active masters: " << masters_.size() << std::endl;

    // Group by type
    int hash_count = 0, memory_count = 0;
    for (const auto& pair : master_infos_) {
        if (pair.second.master_type == MasterType::HASH_MASTER) {
            hash_count++;
        } else if (pair.second.master_type == MasterType::MEMORY_MASTER) {
            memory_count++;
        }
    }

    std::cout << "HashMaster configs: " << hash_count << std::endl;
    std::cout << "MemoryMaster configs: " << memory_count << std::endl;

    std::cout << "\nMaster list:" << std::endl;
    for (const auto& pair : master_infos_) {
        const std::string& name = pair.first;
        const MasterInfo& info = pair.second;
        bool active = (masters_.find(name) != masters_.end());

        std::cout << "  " << name << " (" << info.getMasterTypeString()
                  << ") " << (active ? "[ACTIVE]" : "[INACTIVE]") << std::endl;
    }
}

void MasterManager::log(LogLevel level, const char* format, ...) {
    if (level < log_level_) {
        return;
    }

    const char* level_str;
    switch (level) {
        case LOG_ERROR: level_str = "ERROR"; break;
        case LOG_WARNING: level_str = "WARNING"; break;
        case LOG_INFO: level_str = "INFO"; break;
        case LOG_DEBUG: level_str = "DEBUG"; break;
        default: level_str = "UNKNOWN"; break;
    }

    printf("[%s] MasterManager: ", level_str);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}