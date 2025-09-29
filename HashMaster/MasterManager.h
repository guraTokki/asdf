#ifndef MASTER_MANAGER_H
#define MASTER_MANAGER_H

#include "Master.h"
#include "../common/Compat.h"  // For GCC 4.8.5 compatibility
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>

// Supported Master implementation types
enum class MasterType {
    HASH_MASTER,    // File-based HashMaster
    MEMORY_MASTER   // In-memory MemoryMaster
};

// Master configuration information loaded from YAML files
struct MasterInfo {
    std::string name;
    std::string description;
    std::string layout;
    MasterType master_type;
    MasterConfig config;

    MasterInfo() : master_type(MasterType::HASH_MASTER) {}
    MasterInfo(const std::string& n, const std::string& desc, const std::string& lay,
               MasterType type, const MasterConfig& cfg)
        : name(n), description(desc), layout(lay), master_type(type), config(cfg) {}

    std::string getMasterTypeString() const {
        switch (master_type) {
            case MasterType::HASH_MASTER: return "HashMaster";
            case MasterType::MEMORY_MASTER: return "MemoryMaster";
            default: return "Unknown";
        }
    }
};

/**
 * @brief MasterManager - Manages multiple master configurations using Master interface
 *
 * This class loads master configurations from YAML files in a specified directory
 * and provides access to master information and Master instances by name.
 * Supports both HashMaster (file-based) and MemoryMaster (memory-based) implementations.
 *
 * Example YAML configuration:
 * name: "JAPAN_EQUITY_MASTER"
 * description: "일본 주식 마스터"
 * master_type: "HashMaster"  # or "MemoryMaster"
 * layout: "MMP_EQUITY_MASTER"
 * max_record_count: 50000
 * # ... other MasterConfig fields
 *
 * Example usage:
 * MasterManager manager;
 * manager.loadMasterConfigs("config/MASTERs");
 * Master* master = manager.getMaster("JAPAN_EQUITY_MASTER");
 */
class MasterManager {
private:
    std::string config_directory_;
    std::map<std::string, MasterInfo> master_infos_;
    std::map<std::string, std::unique_ptr<Master>> masters_;
    LogLevel log_level_;

    // Helper methods
    bool loadMasterConfigFile(const std::string& filepath);
    std::map<std::string, std::string> parseSimpleYAML(const std::string& filepath);
    MasterConfig parseMasterConfig(const std::map<std::string, std::string>& config_map);
    MasterType parseMasterType(const std::string& type_str);
    std::unique_ptr<Master> createMasterInstance(const MasterInfo& info);
    void log(LogLevel level, const char* format, ...);

public:
    MasterManager(LogLevel log_level = LOG_INFO);
    ~MasterManager();

    // Configuration loading
    bool loadMasterConfigs(const std::string& config_directory);
    void reload();

    // Master information access
    bool hasMaster(const std::string& name) const;
    const MasterInfo* getMasterInfo(const std::string& name) const;
    std::vector<std::string> getMasterNames() const;
    std::vector<std::string> getMasterNamesByType(MasterType type) const;

    // Master instance management
    Master* getMaster(const std::string& name);
    Master* createMaster(const std::string& name);
    bool initializeMaster(const std::string& name);
    void closeMaster(const std::string& name);
    void closeAllMasters();

    // Statistics and monitoring
    void displayAllMasterStats() const;
    void displayMasterInfo(const std::string& name) const;
    void displayMasterSummary() const;

    // Utility methods
    void setLogLevel(LogLevel level) { log_level_ = level; }
    LogLevel getLogLevel() const { return log_level_; }
    size_t getMasterCount() const { return master_infos_.size(); }
    size_t getActiveMasterCount() const { return masters_.size(); }

    // Factory method for creating Master instances
    static std::unique_ptr<Master> createMaster(MasterType type, const MasterConfig& config);
};

#endif // MASTER_MANAGER_H