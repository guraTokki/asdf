#pragma once

#include "SequenceStorage.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace SimplePubSub {

class FileSequenceStorage : public SequenceStorage {
private:
    std::string _storage_directory;
    // std::string _file_prefix;
    std::string _file_path;
    
    std::string get_file_path() const {
        // return _storage_directory + "/" + _file_prefix + "_" + publisher_name + ".seq";
        return _storage_directory + "/" + _file_path;
    }
    
    bool ensure_directory_exists() const {
        struct stat st = {0};
        if (stat(_storage_directory.c_str(), &st) == -1) {
            return mkdir(_storage_directory.c_str(), 0755) == 0;
        }
        return true;
    }

public:
    FileSequenceStorage(const std::string& storage_dir = "./sequence_data", 
                       const std::string& file_path = "subscriber")
        : _storage_directory(storage_dir), _file_path(file_path) {
    }
    
    virtual ~FileSequenceStorage() {
        cleanup();
    }
    
    bool save_sequences(const PublisherSequenceRecord& record) override {
        if (!ensure_directory_exists()) {
            std::cerr << "Failed to create storage directory: " << _storage_directory << std::endl;
            return false;
        }
        
        std::string file_path = get_file_path();
        std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
        
        if (!file.is_open()) {
            std::cerr << "Failed to open sequence file for writing: " << file_path << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(&record), sizeof(PublisherSequenceRecord));
        
        if (!file.good()) {
            std::cerr << "Failed to write sequence record to file: " << file_path << std::endl;
            return false;
        }
        
        file.close();
        return true;
    }
    
    bool load_sequences(const std::string& publisher_name, PublisherSequenceRecord* record) override {
        std::string file_path = get_file_path();
        std::ifstream file(file_path, std::ios::binary);
        
        if (!file.is_open()) {
            // File doesn't exist - initialize with default values
            // record = PublisherSequenceRecord();
            return false;
        }
        
        file.read(reinterpret_cast<char*>(record), sizeof(PublisherSequenceRecord));
        
        if (!file.good() || file.gcount() != sizeof(PublisherSequenceRecord)) {
            std::cerr << "Failed to read sequence record from file: " << file_path << std::endl;
            // Initialize with default values on read error
            // record = PublisherSequenceRecord();
            return false;
        }
        
        file.close();
        return true;
    }
    
    bool initialize() override {
        return ensure_directory_exists();
    }
    
    void clear() override {
        // Remove all sequence files in the storage directory
        std::string command = "find " + _storage_directory + " -name '" + _file_path + "' -delete 2>/dev/null";
        system(command.c_str());
    }
    
    void cleanup() override {
        // No specific cleanup needed for file-based storage
    }
    
    std::string get_storage_type() const override {
        return "file";
    }
    
    bool is_hashmaster_type() const override {
        return false;
    }
    
    // Additional file-specific methods
    void set_storage_directory(const std::string& dir) {
        _storage_directory = dir;
    }
    
    // void set_file_prefix(const std::string& prefix) {
    //     _file_prefix = prefix;
    // }
    
    std::string get_storage_directory() const {
        return _storage_directory;
    }
};

} // namespace SimplePubSub