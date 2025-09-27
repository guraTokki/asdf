#include "YAMLParser.h"
#include <algorithm>
#include <regex>
#include <stack>

namespace Core {

// 유틸리티 함수들
std::string YAMLParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool YAMLParser::is_integer(const std::string& str) const {
    if (str.empty()) return false;
    size_t start = (str[0] == '-') ? 1 : 0;
    return std::all_of(str.begin() + start, str.end(), ::isdigit);
}

bool YAMLParser::is_double(const std::string& str) const {
    try {
        std::stod(str);
        return str.find('.') != std::string::npos;
    } catch (...) {
        return false;
    }
}

bool YAMLParser::is_boolean(const std::string& str) const {
    return str == "true" || str == "false" || str == "True" || str == "False" ||
           str == "TRUE" || str == "FALSE" || str == "yes" || str == "no" ||
           str == "Yes" || str == "No" || str == "YES" || str == "NO";
}

YAMLValue YAMLParser::parse_value(const std::string& value_str) const {
    std::string cleaned_value = trim(value_str);
    
    if (cleaned_value.empty()) {
        return YAMLValue("");
    }
    
    // Boolean 체크
    if (is_boolean(cleaned_value)) {
        bool bool_val = (cleaned_value == "true" || cleaned_value == "True" || 
                        cleaned_value == "TRUE" || cleaned_value == "yes" || 
                        cleaned_value == "Yes" || cleaned_value == "YES");
        return YAMLValue(bool_val);
    }
    
    // Integer 체크
    if (is_integer(cleaned_value)) {
        return YAMLValue(std::stoi(cleaned_value));
    }
    
    // Double 체크
    if (is_double(cleaned_value)) {
        return YAMLValue(std::stod(cleaned_value));
    }
    
    // String (따옴표 제거)
    return YAMLValue(remove_quotes(cleaned_value));
}

int YAMLParser::get_indent_level(const std::string& line) const {
    int indent = 0;
    for (char c : line) {
        if (c == ' ') indent++;
        else if (c == '\t') indent += 4; // 탭을 4칸으로 계산
        else break;
    }
    return indent;
}

std::string YAMLParser::remove_quotes(const std::string& str) const {
    if (str.size() >= 2 && 
        ((str.front() == '"' && str.back() == '"') ||
         (str.front() == '\'' && str.back() == '\''))) {
        return str.substr(1, str.size() - 2);
    }
    return str;
}

std::string YAMLParser::remove_comment(const std::string& str) const {
    size_t comment_pos = str.find('#');
    if (comment_pos != std::string::npos) {
        return str.substr(0, comment_pos);
    }
    return str;
}

// 파일에서 YAML 로드
bool YAMLParser::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "YAMLParser: Failed to open file: " << filename << std::endl;
        return false;
    }
    
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return load_from_string(buffer.str());
}

// 문자열에서 YAML 로드
bool YAMLParser::load_from_string(const std::string& yaml_content) {
    root_values_.clear();
    
    std::istringstream stream(yaml_content);
    std::string line;
    std::stack<std::pair<std::string, int>> section_stack; // (section_name, indent_level)
    
    while (std::getline(stream, line)) {
        // 인덴트 계산을 주석 제거 전에 수행
        int current_indent = get_indent_level(line);
        
        // 주석 제거
        line = remove_comment(line);
        
        // 빈 줄이나 주석 라인 건너뛰기
        std::string trimmed_line = trim(line);
        if (trimmed_line.empty()) {
            continue;
        }
        
        // std::cout << "YAMLParser Debug: Line='" << trimmed_line << "' indent=" << current_indent << std::endl;
        
        // 스택에서 현재 indent 레벨보다 깊거나 같은 레벨의 섹션들 제거
        while (!section_stack.empty() && section_stack.top().second >= current_indent) {
            section_stack.pop();
        }
        
        // key: value 형태 파싱
        size_t colon_pos = trimmed_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(trimmed_line.substr(0, colon_pos));
            std::string value_part = trim(trimmed_line.substr(colon_pos + 1));
            
            if (value_part.empty()) {
                // 섹션의 시작 (key: 다음에 값이 없음)
                section_stack.push({key, current_indent});
                
                // 최상위 레벨이면 root에 섹션 추가
                if (section_stack.size() == 1) {
                    root_values_[key] = YAMLValue();
                    root_values_[key].set_as_section();
                }
            } else {
                // key-value 쌍
                YAMLValue parsed_value = parse_value(value_part);
                
                if (section_stack.empty()) {
                    // 최상위 레벨
                    root_values_[key] = parsed_value;
                } else {
                    // 섹션 내부
                    std::string section_path = section_stack.top().first;
                    
                    // 중첩된 섹션 경로 구성
                    std::stack<std::pair<std::string, int>> temp_stack = section_stack;
                    std::vector<std::string> path_parts;
                    while (!temp_stack.empty()) {
                        path_parts.push_back(temp_stack.top().first);
                        temp_stack.pop();
                    }
                    std::reverse(path_parts.begin(), path_parts.end());
                    
                    // 중첩된 섹션에 값 추가
                    if (path_parts.size() == 1) {
                        // 1단계 중첩
                        root_values_[path_parts[0]].set_section_value(key, parsed_value);
                    } else {
                        // 다단계 중첩 (현재는 2단계까지 지원)
                        if (path_parts.size() >= 2) {
                            std::cerr << "YAMLParser: Warning - Deep nesting (>2) not fully supported yet" << std::endl;
                        }
                        root_values_[path_parts[0]].set_section_value(key, parsed_value);
                    }
                }
            }
        }
    }
    
    return true;
}

// 값 접근 메서드들
YAMLValue YAMLParser::get_value(const std::string& key) const {
    auto it = root_values_.find(key);
    return (it != root_values_.end()) ? it->second : YAMLValue();
}

YAMLValue YAMLParser::get_nested_value(const std::string& section, const std::string& key) const {
    auto section_it = root_values_.find(section);
    if (section_it != root_values_.end() && section_it->second.is_section()) {
        return section_it->second.get_section_value(key);
    }
    return YAMLValue();
}

YAMLValue YAMLParser::get_section(const std::string& section_name) const {
    return get_value(section_name);
}

bool YAMLParser::has_key(const std::string& key) const {
    return root_values_.find(key) != root_values_.end();
}

bool YAMLParser::has_section(const std::string& section_name) const {
    auto it = root_values_.find(section_name);
    return it != root_values_.end() && it->second.is_section();
}

// 디버그용 출력
void YAMLParser::print_all_values() const {
    std::cout << "=== YAML Parser Contents ===" << std::endl;
    for (const auto& pair : root_values_) {
        if (pair.second.is_section()) {
            std::cout << "[SECTION] " << pair.first << ":" << std::endl;
            const auto& section_values = pair.second.get_section_values();
            for (const auto& section_pair : section_values) {
                std::cout << "  " << section_pair.first << ": ";
                const YAMLValue& value = section_pair.second;
                if (value.is_string()) std::cout << "\"" << value.as_string() << "\"";
                else if (value.is_int()) std::cout << value.as_int();
                else if (value.is_double()) std::cout << value.as_double();
                else if (value.is_bool()) std::cout << (value.as_bool() ? "true" : "false");
                std::cout << std::endl;
            }
        } else {
            std::cout << pair.first << ": ";
            const YAMLValue& value = pair.second;
            if (value.is_string()) std::cout << "\"" << value.as_string() << "\"";
            else if (value.is_int()) std::cout << value.as_int();
            else if (value.is_double()) std::cout << value.as_double();
            else if (value.is_bool()) std::cout << (value.as_bool() ? "true" : "false");
            std::cout << std::endl;
        }
    }
    std::cout << "=========================" << std::endl;
}

void YAMLParser::print_section(const std::string& section_name) const {
    auto it = root_values_.find(section_name);
    if (it != root_values_.end() && it->second.is_section()) {
        std::cout << "=== Section: " << section_name << " ===" << std::endl;
        const auto& section_values = it->second.get_section_values();
        for (const auto& pair : section_values) {
            std::cout << "  " << pair.first << ": ";
            const YAMLValue& value = pair.second;
            if (value.is_string()) std::cout << "\"" << value.as_string() << "\"";
            else if (value.is_int()) std::cout << value.as_int();
            else if (value.is_double()) std::cout << value.as_double();
            else if (value.is_bool()) std::cout << (value.as_bool() ? "true" : "false");
            std::cout << std::endl;
        }
    } else {
        std::cout << "Section '" << section_name << "' not found or not a section" << std::endl;
    }
}

} // namespace Core