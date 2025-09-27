#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>

namespace Core {

// YAML 값을 담는 클래스 (문자열, 숫자, 불린, 섹션 지원)
class YAMLValue {
public:
    enum Type {
        STRING,
        INTEGER,
        DOUBLE,
        BOOLEAN,
        SECTION  // 중첩된 key-value 섹션
    };
    
private:
    Type type_;
    std::string string_value_;
    int int_value_;
    double double_value_;
    bool bool_value_;
    std::map<std::string, YAMLValue> section_values_;
    
public:
    YAMLValue() : type_(STRING), string_value_(""), int_value_(0), double_value_(0.0), bool_value_(false) {}
    
    // 생성자들
    YAMLValue(const std::string& value) : type_(STRING), string_value_(value), int_value_(0), double_value_(0.0), bool_value_(false) {}
    YAMLValue(int value) : type_(INTEGER), string_value_(""), int_value_(value), double_value_(0.0), bool_value_(false) {}
    YAMLValue(double value) : type_(DOUBLE), string_value_(""), int_value_(0), double_value_(value), bool_value_(false) {}
    YAMLValue(bool value) : type_(BOOLEAN), string_value_(""), int_value_(0), double_value_(0.0), bool_value_(value) {}
    
    // 타입 확인
    Type get_type() const { return type_; }
    bool is_string() const { return type_ == STRING; }
    bool is_int() const { return type_ == INTEGER; }
    bool is_double() const { return type_ == DOUBLE; }
    bool is_bool() const { return type_ == BOOLEAN; }
    bool is_section() const { return type_ == SECTION; }
    
    // 값 가져오기
    std::string as_string() const { return string_value_; }
    int as_int() const { return int_value_; }
    double as_double() const { return double_value_; }
    bool as_bool() const { return bool_value_; }
    
    // 섹션 관리
    void set_as_section() { type_ = SECTION; }
    void set_section_value(const std::string& key, const YAMLValue& value) {
        type_ = SECTION;
        section_values_[key] = value;
    }
    
    YAMLValue get_section_value(const std::string& key) const {
        auto it = section_values_.find(key);
        return (it != section_values_.end()) ? it->second : YAMLValue();
    }
    
    const std::map<std::string, YAMLValue>& get_section_values() const {
        return section_values_;
    }
    
    // 유틸리티
    bool exists() const { return !string_value_.empty() || type_ != STRING; }
    
    // 섹션 내 키 존재 확인
    bool has_key(const std::string& key) const {
        return section_values_.find(key) != section_values_.end();
    }
};

// 간단한 YAML 파서 클래스
class YAMLParser {
private:
    std::map<std::string, YAMLValue> root_values_;
    
    // 유틸리티 함수들
    std::string trim(const std::string& str) const;
    bool is_integer(const std::string& str) const;
    bool is_double(const std::string& str) const;
    bool is_boolean(const std::string& str) const;
    YAMLValue parse_value(const std::string& value_str) const;
    int get_indent_level(const std::string& line) const;
    std::string remove_quotes(const std::string& str) const;
    std::string remove_comment(const std::string& str) const;
    
public:
    YAMLParser() = default;
    
    // YAML 파일 로드
    bool load_from_file(const std::string& filename);
    bool load_from_string(const std::string& yaml_content);
    
    // 값 접근
    YAMLValue get_value(const std::string& key) const;
    YAMLValue get_nested_value(const std::string& section, const std::string& key) const;
    
    // 섹션 접근
    YAMLValue get_section(const std::string& section_name) const;
    
    // 존재 확인
    bool has_key(const std::string& key) const;
    bool has_section(const std::string& section_name) const;
    
    // 디버그용 출력
    void print_all_values() const;
    void print_section(const std::string& section_name) const;
    
    // 전체 값 맵 접근 (필요시)
    const std::map<std::string, YAMLValue>& get_root_values() const { return root_values_; }
};

} // namespace Core