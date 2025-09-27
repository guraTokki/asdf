#include "HashmasterSequenceStorage.h"
#include <iostream>
#include <chrono>
#include <cstring>

namespace SimplePubSub {

HashmasterSequenceStorage::HashmasterSequenceStorage(const std::string& storage_path, const std::string& config_file_path)
    : storage_path_(storage_path), direct_record_ptr_(nullptr) {
    
    if (!config_file_path.empty()) {
        // TODO: YAML 파일에서 설정 로드 (현재는 기본 설정 사용)
        std::cout << "Config file path provided but not implemented, using default config: " << config_file_path << std::endl;
    }

    // 기본 설정 사용 (Publisher 시퀀스 전용)
    config_._max_record_count = 1000;  // 최대 1000개의 Publisher
    config_._max_record_size = sizeof(PublisherSequenceRecord);
    config_._hash_count = 1009;  // 소수 (1000보다 큰 첫 번째 소수)
    config_._primary_field_len = 64;  // publisher_name 길이
    config_._secondary_field_len = 32; // 보조 키 (사용하지 않지만 설정)
    config_._use_lock = true;  // Thread-safe
    config_._filename = storage_path;
    config_._log_level = LOG_INFO;
    config_._tot_size = config_._max_record_count * config_._max_record_size;
    std::cout << "HashMaster using default config for storage path: " << storage_path << std::endl;
    
    // 레코드 크기가 맞는지 확인 및 조정
    if (config_._max_record_size != sizeof(PublisherSequenceRecord)) {
        config_._max_record_size = sizeof(PublisherSequenceRecord);
        config_._tot_size = config_._max_record_count * config_._max_record_size;
        std::cout << "HashMaster record size adjusted to: " << sizeof(PublisherSequenceRecord) << " bytes" << std::endl;
    }
}

HashmasterSequenceStorage::~HashmasterSequenceStorage() {
    cleanup();
}

bool HashmasterSequenceStorage::initialize() {
    try {
        hashmaster_ = std::make_unique<HashMaster>(config_);
        
        if (hashmaster_->init() != 0) {
            std::cerr << "[HashMasterStorage] Failed to initialize HashMaster" << std::endl;
            return false;
        }
        
        std::cout << "[HashMasterStorage] Initialized with storage: " << storage_path_ << std::endl;
        std::cout << "[HashMasterStorage] Max publishers: " << config_._max_record_count << std::endl;
        std::cout << "[HashMasterStorage] Record size: " << config_._max_record_size << " bytes" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during initialization: " << e.what() << std::endl;
        return false;
    }
}

void HashmasterSequenceStorage::cleanup() {
    hashmaster_.reset();
}

std::string HashmasterSequenceStorage::get_storage_type() const {
    return "HashMasterStorage";
}

uint64_t HashmasterSequenceStorage::get_current_time_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}


bool HashmasterSequenceStorage::save_sequences(const PublisherSequenceRecord& record) {
    if (!hashmaster_) {
        std::cerr << "[HashMasterStorage] HashMaster not initialized in " << __FILE__ << ":" << __LINE__ << std::endl;
        return false;
    }
    
    try {
        char *found_record = hashmaster_->get_by_primary(record.publisher_name);
        if (found_record == nullptr) {
            std::cout << "[HashMasterStorage] Creating new record for publisher: " << record.publisher_name << std::endl;
            
            // 새 레코드 생성 및 저장
            PublisherSequenceRecord new_record = record; // 복사
            new_record.last_updated_time = get_current_time_ns();
            
            int result = hashmaster_->put(record.publisher_name, "",
                                         reinterpret_cast<const char*>(&new_record),
                                         sizeof(new_record));
            
            if (result != 0) {
                std::cerr << "[HashMasterStorage] Failed to create new record for publisher: " << record.publisher_name << std::endl;
                return false;
            }
        } else {
            // 기존 레코드 업데이트
            PublisherSequenceRecord *existing_record = reinterpret_cast<PublisherSequenceRecord*>(found_record);
            *existing_record = record; // 전체 복사
            existing_record->last_updated_time = get_current_time_ns();
        }
        
        std::cout << "[HashMasterStorage] Saved sequences for publisher: " << record.publisher_name
                  << " (ID: " << record.publisher_id << ", Date: " << record.publisher_date
                  << ", All_Topics_Seq: " << record.all_topics_sequence << ")" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during save: " << e.what() << std::endl;
        return false;
    }
}

bool HashmasterSequenceStorage::load_sequences(const std::string& publisher_name, 
                                              PublisherSequenceRecord* record) {
    if (!hashmaster_) {
        std::cerr << "[HashMasterStorage] HashMaster not initialized in " << __FILE__ << ":" << __LINE__ << std::endl;
        return false;
    }
#if  0    
    try {
        // HashMaster에서 조회
        char* found_record = hashmaster_->get_by_primary(publisher_name.c_str());
        
        if (found_record) {
            memcpy(&record, found_record, sizeof(record));
            
            std::cout << "[HashMasterStorage] Loaded sequences for publisher: " << record.publisher_name
                      << " (ID: " << record.publisher_id << ", Date: " << record.publisher_date
                      << ", All_Topics_Seq: " << record.all_topics_sequence << ")" << std::endl;
            
            return true;
        } else {
            std::cout << "[HashMasterStorage] No previous data found for publisher: " << publisher_name 
                      << ", starting with defaults" << std::endl;
            
            // 기본값으로 초기화
            memset(&record, 0, sizeof(record));
            strncpy(record.publisher_name, publisher_name.c_str(), sizeof(record.publisher_name) - 1);
            record.publisher_name[sizeof(record.publisher_name) - 1] = '\0';
            record.publisher_id = 0;
            record.publisher_date = 0;
            record.topic1_sequence = 0;
            record.topic2_sequence = 0;
            record.misc_sequence = 0;
            record.all_topics_sequence = 0;
            record.last_updated_time = get_current_time_ns();
            
            
            return true; // 새 Publisher는 정상 케이스
        }
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during load: " << e.what() << std::endl;
        return false;
    }
#endif
    try {
        // HashMaster에서 조회
        char* found_record = hashmaster_->get_by_primary(publisher_name.c_str());

        if(found_record == nullptr) {
            std::cout << "[HashMasterStorage] No previous data found for publisher: " << publisher_name
                      << ", starting with defaults" << std::endl;

            // 기본값으로 초기화
            PublisherSequenceRecord new_record;
            memset(&new_record, 0, sizeof(new_record));
            strncpy(new_record.publisher_name, publisher_name.c_str(), sizeof(new_record.publisher_name) - 1);
            new_record.publisher_name[sizeof(new_record.publisher_name) - 1] = '\0';
            new_record.publisher_id = 0;
            new_record.publisher_date = 0;
            new_record.topic1_sequence = 0;
            new_record.topic2_sequence = 0;
            new_record.misc_sequence = 0;
            new_record.all_topics_sequence = 0;
            new_record.last_updated_time = get_current_time_ns();

            if(hashmaster_->put(publisher_name.c_str(), "",
                               reinterpret_cast<const char*>(&new_record),
                               sizeof(new_record)) != 0) {
                std::cerr << "[HashMasterStorage] Failed to create new record for publisher: " << publisher_name << std::endl;
                return false;
            }

            // 새로 생성된 레코드를 다시 조회하여 mmap 포인터 획득
            found_record = hashmaster_->get_by_primary(publisher_name.c_str());
            if (found_record == nullptr) {
                std::cerr << "[HashMasterStorage] Failed to retrieve newly created record for publisher: " << publisher_name << std::endl;
                return false;
            }
        }

        // found_record가 있으면 데이터를 record에 복사 (기존 인터페이스 호환성 유지)
        memcpy(record, found_record, sizeof(PublisherSequenceRecord));

        std::cout << "[HashMasterStorage] Loaded sequences for publisher: " << record->publisher_name
                  << " (ID: " << record->publisher_id << ", Date: " << record->publisher_date
                  << ", All_Topics_Seq: " << record->all_topics_sequence << ")" << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during load: " << e.what() << std::endl;
        return false;
    }
}

PublisherSequenceRecord* HashmasterSequenceStorage::load_sequences_direct(const std::string& publisher_name) {
    if (!hashmaster_) {
        std::cerr << "[HashMasterStorage] HashMaster not initialized in " << __FILE__ << ":" << __LINE__ << std::endl;
        return nullptr;
    }

    try {
        // HashMaster에서 조회
        char* found_record = hashmaster_->get_by_primary(publisher_name.c_str());

        if(found_record == nullptr) {
            std::cout << "[HashMasterStorage] No previous data found for publisher: " << publisher_name
                      << ", creating with defaults" << std::endl;

            // 기본값으로 초기화
            PublisherSequenceRecord new_record;
            memset(&new_record, 0, sizeof(new_record));
            strncpy(new_record.publisher_name, publisher_name.c_str(), sizeof(new_record.publisher_name) - 1);
            new_record.publisher_name[sizeof(new_record.publisher_name) - 1] = '\0';
            new_record.publisher_id = 0;
            new_record.publisher_date = 0;
            new_record.topic1_sequence = 0;
            new_record.topic2_sequence = 0;
            new_record.misc_sequence = 0;
            new_record.all_topics_sequence = 0;
            new_record.last_updated_time = get_current_time_ns();

            if(hashmaster_->put(publisher_name.c_str(), "",
                               reinterpret_cast<const char*>(&new_record),
                               sizeof(new_record)) != 0) {
                std::cerr << "[HashMasterStorage] Failed to create new record for publisher: " << publisher_name << std::endl;
                return nullptr;
            }

            // 새로 생성된 레코드를 다시 조회하여 mmap 포인터 획득
            found_record = hashmaster_->get_by_primary(publisher_name.c_str());
            if (found_record == nullptr) {
                std::cerr << "[HashMasterStorage] Failed to retrieve newly created record for publisher: " << publisher_name << std::endl;
                return nullptr;
            }
        }

        // mmap 포인터 반환
        PublisherSequenceRecord* mmap_record = reinterpret_cast<PublisherSequenceRecord*>(found_record);

        std::cout << "[HashMasterStorage] Loaded sequences (direct) for publisher: " << mmap_record->publisher_name
                  << " (ID: " << mmap_record->publisher_id << ", Date: " << mmap_record->publisher_date
                  << ", All_Topics_Seq: " << mmap_record->all_topics_sequence << ")" << std::endl;

        return mmap_record;

    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during direct load: " << e.what() << std::endl;
        return nullptr;
    }
}

void HashmasterSequenceStorage::set_hashmaster_config(const HashMasterConfig& config) {
    config_ = config;
}

const HashMasterConfig& HashmasterSequenceStorage::get_hashmaster_config() const {
    return config_;
}

int HashmasterSequenceStorage::get_total_publishers() const {
    if (!hashmaster_) {
        return 0;
    }
    auto stats = hashmaster_->get_statistics();
    return stats.used_records;
}

int HashmasterSequenceStorage::get_max_publishers() const {
    return config_._max_record_count;
}

size_t HashmasterSequenceStorage::get_record_size() const {
    return sizeof(PublisherSequenceRecord);
}

bool HashmasterSequenceStorage::list_all_publishers(std::vector<std::string>& publisher_names) const {
    if (!hashmaster_) {
        return false;
    }
    
    publisher_names.clear();
    
    try {
        // 현재 HashMaster API에서 iterator가 지원되지 않으므로 순차적으로 체크
        int max_records = get_max_publishers();
        for (int i = 0; i < max_records; i++) {
            char* record_data = hashmaster_->get_record_by_seq(i + 1);  // 1-based index
            if (record_data) {
                // PublisherSequenceRecord로 캐스팅
                PublisherSequenceRecord* record = reinterpret_cast<PublisherSequenceRecord*>(record_data);

                // 유효한 레코드인지 확인 (publisher_name이 비어있지 않은지)
                if (strlen(record->publisher_name) > 0) {
                    std::cout << "[HashMasterStorage] Found publisher: " << record->publisher_name
                              << " (ID: " << record->publisher_id
                              << ", Date: " << record->publisher_date
                              << ", All_Topics_Seq: " << record->all_topics_sequence << ")" << std::endl;

                    publisher_names.emplace_back(record->publisher_name);
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Error iterating publishers: " << e.what() << std::endl;
        return false;
    }
}

// Direct mmap access 최적화 메서드 구현
bool HashmasterSequenceStorage::setup_direct_access(const std::string& publisher_name) {
    if (!hashmaster_) {
        std::cerr << "[HashMasterStorage] HashMaster not initialized for direct access" << std::endl;
        return false;
    }
    
    try {
        // Publisher 레코드의 직접 포인터 획득
        char* record_data = hashmaster_->get_by_primary(publisher_name.c_str());
        if (record_data) {
            direct_record_ptr_ = reinterpret_cast<PublisherSequenceRecord*>(record_data);
            cached_publisher_name_ = publisher_name;
            std::cout << "[HashMasterStorage] Direct access setup for publisher: " << publisher_name << std::endl;
            return true;
        }
        
        std::cerr << "[HashMasterStorage] Publisher record not found for direct access: " << publisher_name << std::endl;
        return false;
        
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during direct access setup: " << e.what() << std::endl;
        direct_record_ptr_ = nullptr;
        return false;
    }
}

bool HashmasterSequenceStorage::initialize_for_publisher(const std::string& publisher_name, uint32_t publisher_id, int publisher_date) {
    // 기본 초기화
    if (!initialize()) {
        return false;
    }
    
    try {
        // Publisher 레코드 생성/조회
        char* found_record = hashmaster_->get_by_primary(publisher_name.c_str());
        if (!found_record) {
            std::cout << "[HashMasterStorage] Creating new record for publisher: " << publisher_name << std::endl;
            
            // 새 레코드 생성
            PublisherSequenceRecord record;
            strncpy(record.publisher_name, publisher_name.c_str(), sizeof(record.publisher_name) - 1);
            record.publisher_name[sizeof(record.publisher_name) - 1] = '\0';
            record.publisher_id = publisher_id;
            record.publisher_date = publisher_date;
            record.topic1_sequence = 0;
            record.topic2_sequence = 0;
            record.misc_sequence = 0;
            record.all_topics_sequence = 0;
            record.last_updated_time = get_current_time_ns();
            memset(record.reserved, 0, sizeof(record.reserved));
            
            int result = hashmaster_->put(publisher_name.c_str(), "", 
                                         reinterpret_cast<const char*>(&record), 
                                         sizeof(record));
            
            if (result != 0) {
                std::cerr << "[HashMasterStorage] Failed to create publisher record for direct access" << std::endl;
                return false;
            }
        }
        
        // Direct access 설정
        return setup_direct_access(publisher_name);
        
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during publisher initialization: " << e.what() << std::endl;
        return false;
    }
}

bool HashmasterSequenceStorage::clear_publisher_record(const std::string& publisher_name, uint32_t publisher_id, int publisher_date) {
    if (!hashmaster_) {
        std::cerr << "[HashMasterStorage] HashMaster not initialized for clear operation" << std::endl;
        return false;
    }
    
    try {
        // Publisher 레코드 조회
        char* found_record = hashmaster_->get_by_primary(publisher_name.c_str());
        if (found_record) {
            // 기존 레코드의 시퀀스만 초기화 (publisher 정보는 유지)
            PublisherSequenceRecord* record = reinterpret_cast<PublisherSequenceRecord*>(found_record);
            
            // 기본 정보는 유지
            uint32_t publisher_id = record->publisher_id;
            int publisher_date = record->publisher_date;
            
            // 시퀀스 데이터만 초기화
            record->topic1_sequence = 0;
            record->topic2_sequence = 0;
            record->misc_sequence = 0;
            record->all_topics_sequence = 0;
            record->last_updated_time = get_current_time_ns();
            
            std::cout << "[HashMasterStorage] Cleared sequences for existing publisher: " << publisher_name 
                      << " (ID: " << publisher_id << ", Date: " << publisher_date << ")" << std::endl;
        } else {
            // 레코드가 없으면 새로 생성하고 초기화된 값으로 설정
            PublisherSequenceRecord record;
            strncpy(record.publisher_name, publisher_name.c_str(), sizeof(record.publisher_name) - 1);
            record.publisher_name[sizeof(record.publisher_name) - 1] = '\0';
            record.publisher_id = publisher_id;  
            record.publisher_date = publisher_date;  
            record.topic1_sequence = 0;
            record.topic2_sequence = 0;
            record.misc_sequence = 0;
            record.all_topics_sequence = 0;
            record.last_updated_time = get_current_time_ns();
            memset(record.reserved, 0, sizeof(record.reserved));
            
            int result = hashmaster_->put(publisher_name.c_str(), "", 
                                         reinterpret_cast<const char*>(&record), 
                                         sizeof(record));
            
            if (result != 0) {
                std::cerr << "[HashMasterStorage] Failed to create new record for publisher: " << publisher_name << std::endl;
                return false;
            }
            
            std::cout << "[HashMasterStorage] Created and cleared new record for publisher: " << publisher_name << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[HashMasterStorage] Exception during clear operation: " << e.what() << std::endl;
        return false;
    }
}

} // namespace SimplePubSub