#include "BinaryRecord.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

// 타입 변환 함수 구현
const char* fieldTypeToString(FieldType type) {
    switch (type) {
        case FieldType::CHAR: return "char";
        case FieldType::INT: return "int";
        case FieldType::UINT: return "unsigned int";
        case FieldType::SHORT: return "short";
        case FieldType::USHORT: return "unsigned short";
        case FieldType::LONG: return "long";
        case FieldType::ULONG: return "unsigned long";
        case FieldType::DOUBLE: return "double";
        case FieldType::FLOAT: return "float";
        case FieldType::X_MODE: return "X";
        case FieldType::NINE_MODE: return "9";
        default: return "unknown";
    }
}

FieldType stringToFieldType(const std::string& typeStr) {
    std::string lower = typeStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "char") return FieldType::CHAR;
    if (lower == "int") return FieldType::INT;
    if (lower == "unsigned int") return FieldType::UINT;
    if (lower == "short") return FieldType::SHORT;
    if (lower == "unsigned short") return FieldType::USHORT;
    if (lower == "long") return FieldType::LONG;
    if (lower == "unsigned long") return FieldType::ULONG;
    if (lower == "double") return FieldType::DOUBLE;
    if (lower == "float") return FieldType::FLOAT;
    if (lower == "x") return FieldType::X_MODE;
    if (lower == "9") return FieldType::NINE_MODE;
    
    // 기본값
    return FieldType::CHAR;
}

// RecordLayout 구현
RecordLayout::RecordLayout(const std::string& recordType) 
    : _recordType(recordType), _recordSize(0) {}

void RecordLayout::addField(const std::string& name, FieldType type, int length, int decimal, bool isKey) {
    FieldInfo field;
    field.name = name;
    field.type = type;
    field.length = length;
    field.decimal = decimal;
    field.isKey = isKey;
    addField(field);
}

void RecordLayout::addField(const FieldInfo& field) {
    _fields.push_back(field);
    // calculateLayout()은 모든 필드 추가 후에 명시적으로 호출
}

const FieldInfo* RecordLayout::getField(const std::string& name) const {
    auto it = _fieldIndex.find(name);
    if (it != _fieldIndex.end()) {
        return &_fields[it->second];
    }
    return nullptr;
}

void RecordLayout::calculateLayout() {
    int offset = 0;
    for (auto& field : _fields) {
        field.offset = offset;
        offset += field.length;
    }
    _recordSize = offset;
    updateIndex();
}

void RecordLayout::updateIndex() {
    _fieldIndex.clear();
    for (size_t i = 0; i < _fields.size(); ++i) {
        _fieldIndex[_fields[i].name] = i;
    }
}

void RecordLayout::dump() const {
    std::cout << "=== Record Layout: " << _recordType << " (" << _recordSize << " bytes) ===" << std::endl;
    for (const auto& field : _fields) {
        std::cout << std::setw(20) << field.name 
                 << " | " << std::setw(8) << fieldTypeToString(field.type)
                 << " | offset=" << std::setw(3) << field.offset
                 << " | len=" << std::setw(3) << field.length;
        if (field.decimal > 0) {
            std::cout << " | dec=" << field.decimal;
        }
        if (field.isKey) {
            std::cout << " | KEY";
        }
        std::cout << std::endl;
    }
}

// BinaryRecord 구현
BinaryRecord::BinaryRecord(std::shared_ptr<RecordLayout> layout) 
    : _layout(layout), _buffer(nullptr), _ownBuffer(false) {
    allocateBuffer();
}

BinaryRecord::BinaryRecord(std::shared_ptr<RecordLayout> layout, char* buffer)
    : _layout(layout), _buffer(buffer), _ownBuffer(false) {}

// 복사 생성자
BinaryRecord::BinaryRecord(const BinaryRecord& other)
    : _layout(other._layout), _buffer(nullptr), _ownBuffer(false) {
    if (other._buffer && other._layout) {
        // 새로운 버퍼 할당 및 데이터 복사
        _buffer = new char[_layout->getRecordSize()];
        _ownBuffer = true;
        memcpy(_buffer, other._buffer, _layout->getRecordSize());
    }
}

// 복사 할당 연산자
BinaryRecord& BinaryRecord::operator=(const BinaryRecord& other) {
    if (this != &other) {  // 자기 할당 방지
        // 기존 버퍼 해제
        if (_ownBuffer && _buffer) {
            delete[] _buffer;
        }
        
        // 새로운 레이아웃 설정
        _layout = other._layout;
        _buffer = nullptr;
        _ownBuffer = false;
        
        // 버퍼 복사
        if (other._buffer && other._layout) {
            _buffer = new char[_layout->getRecordSize()];
            _ownBuffer = true;
            memcpy(_buffer, other._buffer, _layout->getRecordSize());
        }
    }
    return *this;
}

BinaryRecord::~BinaryRecord() {
    if (_ownBuffer && _buffer) {
        delete[] _buffer;
    }
}

void BinaryRecord::allocateBuffer() {
    if (_ownBuffer && _buffer) {
        delete[] _buffer;
    }
    
    _buffer = new char[_layout->getRecordSize()];
    _ownBuffer = true;
    clear();
}

void BinaryRecord::setBuffer(char* buffer, bool takeOwnership) {
    if (_ownBuffer && _buffer) {
        delete[] _buffer;
    }
    
    _buffer = buffer;
    _ownBuffer = takeOwnership;
}

int BinaryRecord::getSize() const {
    return _layout ? _layout->getRecordSize() : 0;
}

void BinaryRecord::clear() {
    if (_buffer && _layout) {
        memset(_buffer, 0, _layout->getRecordSize());
    }
}

// 필드 정보 가져오기
const FieldInfo* BinaryRecord::getFieldInfo(const std::string& name) const {
    return _layout ? _layout->getField(name) : nullptr;
}

// 문자열 쓰기
bool BinaryRecord::setString(const std::string& fieldName, const std::string& value) {
    const FieldInfo* field = getFieldInfo(fieldName);

    if (!field || !_buffer) return false;
    // std::cout << "setValue: " << fieldName << " " << fieldTypeToString(field->type) << std::endl;
    if (field->type == FieldType::CHAR) {
        if (field->length <= 0) {
            std::cerr << "ERROR: Invalid field length for " << fieldName << ": " << field->length << std::endl;
            return false;
        }
        int copyLen = std::min(static_cast<int>(value.length()), field->length - 1);
        if (copyLen < 0) copyLen = 0;  // Safety check
        memset(_buffer + field->offset, 0, field->length);
        if (copyLen > 0) {
            memcpy(_buffer + field->offset, value.c_str(), copyLen);
        }
        return true;
    }
    
    return setValue(fieldName, value);
}

// 정수 쓰기
bool BinaryRecord::setInt(const std::string& fieldName, int value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if (field->type == FieldType::INT && field->length >= 4) {
        memcpy(_buffer + field->offset, &value, sizeof(int));
        return true;
    }
    
    return setValue(fieldName, std::to_string(value));
}

// 롱 쓰기
bool BinaryRecord::setLong(const std::string& fieldName, long long value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if ((field->type == FieldType::ULONG || field->type == FieldType::LONG) && field->length >= 8) {
        memcpy(_buffer + field->offset, &value, sizeof(long long));
        return true;
    }
    
    return setValue(fieldName, std::to_string(value));
}

// 더블 쓰기
bool BinaryRecord::setDouble(const std::string& fieldName, double value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if (field->type == FieldType::DOUBLE && field->length >= 8) {
        memcpy(_buffer + field->offset, &value, sizeof(double));
        return true;
    }
    
    return setValue(fieldName, std::to_string(value));
}

// X 모드 쓰기
bool BinaryRecord::setXMode(const std::string& fieldName, const std::string& value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    std::string formatted = formatXMode(value, field->length);
    memcpy(_buffer + field->offset, formatted.c_str(), field->length);
    return true;
}

// 9 모드 쓰기
bool BinaryRecord::set9Mode(const std::string& fieldName, const std::string& value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    std::string formatted = format9Mode(value, field->length, field->decimal);
    memcpy(_buffer + field->offset, formatted.c_str(), field->length);
    return true;
}

// X 모드 초기화 (특정 문자로 필드를 채움)
bool BinaryRecord::initXMode(const std::string& fieldName, char fillChar) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if (field->type != FieldType::X_MODE) {
        std::cerr << "ERROR: Field " << fieldName << " is not X_MODE type" << std::endl;
        return false;
    }
    
    // 필드를 지정된 문자로 채움
    memset(_buffer + field->offset, fillChar, field->length);
    return true;
}

// 9 모드 초기화 (특정 문자로 필드를 채움)
bool BinaryRecord::init9Mode(const std::string& fieldName, char fillChar) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if (field->type != FieldType::NINE_MODE) {
        std::cerr << "ERROR: Field " << fieldName << " is not NINE_MODE type" << std::endl;
        return false;
    }
    
    // 필드를 지정된 문자로 채움
    memset(_buffer + field->offset, fillChar, field->length);
    return true;
}

// 문자열 읽기
std::string BinaryRecord::getString(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return "";
    
    if (field->type == FieldType::CHAR) {
        return std::string(_buffer + field->offset, field->length);
    }
    
    return getValue(fieldName);
}

// 정수 읽기
int BinaryRecord::getInt(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return 0;
    
    if (field->type == FieldType::INT && field->length >= 4) {
        int value;
        memcpy(&value, _buffer + field->offset, sizeof(int));
        return value;
    }
    
    std::string str = getValue(fieldName);
    return str.empty() ? 0 : std::atoi(str.c_str());
}

// 롱 읽기
long long BinaryRecord::getLong(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return 0;
    
    if ((field->type == FieldType::ULONG || field->type == FieldType::LONG) && field->length >= 8) {
        long long value;
        memcpy(&value, _buffer + field->offset, sizeof(long long));
        return value;
    }
    
    std::string str = getValue(fieldName);
    return str.empty() ? 0 : std::atoll(str.c_str());
}

// 더블 읽기
double BinaryRecord::getDouble(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return 0.0;
    
    if (field->type == FieldType::DOUBLE && field->length >= 8) {
        double value;
        memcpy(&value, _buffer + field->offset, sizeof(double));
        return value;
    }
    
    std::string str = getValue(fieldName);
    return str.empty() ? 0.0 : std::atof(str.c_str());
}

// X 모드 읽기
std::string BinaryRecord::getXMode(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return "";
    
    return parseXMode(_buffer + field->offset, field->length);
}

// 9 모드 읽기
std::string BinaryRecord::get9Mode(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return "";
    
    return parse9Mode(_buffer + field->offset, field->length, field->decimal);
}

// 범용 값 설정
bool BinaryRecord::setValue(const std::string& fieldName, const std::string& value) {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return false;
    
    if (field->type == FieldType::X_MODE) {
        return setXMode(fieldName, value);
    } else if (field->type == FieldType::NINE_MODE) {
        return set9Mode(fieldName, value);
    } else if (field->type == FieldType::CHAR) {
        return setString(fieldName, value);
    } else {
        // 바이너리 타입들은 문자열로 변환해서 저장
        std::string formatted = value;
        formatted.resize(field->length, '\0');
        memcpy(_buffer + field->offset, formatted.c_str(), field->length);
        return true;
    }
}

// 범용 값 가져오기
std::string BinaryRecord::getValue(const std::string& fieldName) const {
    const FieldInfo* field = getFieldInfo(fieldName);
    if (!field || !_buffer) return "";
    
    switch (field->type) {
        case FieldType::X_MODE:
            return getXMode(fieldName);
        case FieldType::NINE_MODE:
            return get9Mode(fieldName);
        case FieldType::CHAR: {
            std::string result(_buffer + field->offset, field->length);
            // null terminator 찾기
            size_t nullPos = result.find('\0');
            if (nullPos != std::string::npos) {
                result = result.substr(0, nullPos);
            }
            return result;
        }
        case FieldType::INT: {
            if (field->length >= 4) {
                int value;
                memcpy(&value, _buffer + field->offset, sizeof(int));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::UINT: {
            if (field->length >= 4) {
                unsigned int value;
                memcpy(&value, _buffer + field->offset, sizeof(unsigned int));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::SHORT: {
            if (field->length >= 2) {
                short value;
                memcpy(&value, _buffer + field->offset, sizeof(short));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::USHORT: {
            if (field->length >= 2) {
                unsigned short value;
                memcpy(&value, _buffer + field->offset, sizeof(unsigned short));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::LONG: {
            if (field->length >= 8) {
                long long value;
                memcpy(&value, _buffer + field->offset, sizeof(long long));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::ULONG: {
            if (field->length >= 8) {
                unsigned long long value;
                memcpy(&value, _buffer + field->offset, sizeof(unsigned long long));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::DOUBLE: {
            if (field->length >= 8) {
                double value;
                memcpy(&value, _buffer + field->offset, sizeof(double));
                return std::to_string(value);
            }
            break;
        }
        case FieldType::FLOAT: {
            if (field->length >= 4) {
                float value;
                memcpy(&value, _buffer + field->offset, sizeof(float));
                return std::to_string(value);
            }
            break;
        }
        default:
            break;
    }
    
    // 기본적으로 바이너리 데이터를 문자열로 반환
    return std::string(_buffer + field->offset, field->length);
}

// Map에서 데이터 로드
void BinaryRecord::fromMap(const std::map<std::string, std::string>& data) {
    if (!_layout || !_buffer) return;
    
    for (const auto& pair : data) {
        setValue(pair.first, pair.second);
    }
}

// Map으로 데이터 변환
std::map<std::string, std::string> BinaryRecord::toMap() const {
    std::map<std::string, std::string> result;
    
    if (!_layout || !_buffer) return result;
    
    for (const auto& field : _layout->getFields()) {
        result[field.name] = getValue(field.name);
    }
    
    return result;
}

// 키 값 가져오기
std::string BinaryRecord::getPrimaryKey() const {
    if (!_layout) return "";
    
    for (const auto& field : _layout->getFields()) {
        if (field.isKey) {
            return getValue(field.name);
        }
    }
    return "";
}

// 복사 작업
bool BinaryRecord::copyFrom(const char* source, int size) {
    if (!_buffer || !source || size != _layout->getRecordSize()) {
        return false;
    }
    
    memcpy(_buffer, source, size);
    return true;
}

bool BinaryRecord::copyTo(char* dest, int size) const {
    if (!_buffer || !dest || size < _layout->getRecordSize()) {
        return false;
    }
    
    memcpy(dest, _buffer, _layout->getRecordSize());
    return true;
}

// X 모드 포맷팅 (고정길이, 우측 패딩)
std::string BinaryRecord::formatXMode(const std::string& value, int length) const {
    // null 문자가 포함된 문자열을 정리 (첫 번째 null 문자까지만 사용)
    std::string cleanValue = value;
    size_t nullPos = cleanValue.find('\0');
    if (nullPos != std::string::npos) {
        cleanValue = cleanValue.substr(0, nullPos);
    }
    
    std::string result = cleanValue;
    if (result.length() > static_cast<size_t>(length)) {
        result = result.substr(0, length);
    } else {
        result.resize(length, ' ');  // 공백으로 패딩
    }
    return result;
}

// X 모드 파싱 (뒤쪽 공백 제거)
std::string BinaryRecord::parseXMode(const char* data, int length) const {
    std::string result(data, length);
    
    // null 문자가 있으면 그 앞까지만 사용
    size_t nullPos = result.find('\0');
    if (nullPos != std::string::npos) {
        result = result.substr(0, nullPos);
    }
    
    // 뒤쪽 공백 제거
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

// 9 모드 포맷팅 (소수점 포함, 앞쪽 0 패딩)
std::string BinaryRecord::format9Mode(const std::string& value, int length, int decimal) const {
    std::string result;
    bool isNegative = false;
    
    // null 문자가 포함된 문자열을 정리 (첫 번째 null 문자까지만 사용)
    std::string cleanValue = value;
    size_t nullPos = cleanValue.find('\0');
    if (nullPos != std::string::npos) {
        cleanValue = cleanValue.substr(0, nullPos);
    }
    
    std::string workValue = cleanValue;
    
    // 음수 처리
    if (!workValue.empty() && workValue[0] == '-') {
        isNegative = true;
        workValue = workValue.substr(1);  // '-' 제거
    }
    
    if (decimal > 0) {
        // 소수점이 있는 경우
        size_t dotPos = workValue.find('.');
        std::string intPart, decPart;
        
        if (dotPos != std::string::npos) {
            intPart = workValue.substr(0, dotPos);
            decPart = workValue.substr(dotPos + 1);
        } else {
            intPart = workValue;
            decPart = "";
        }
        
        // 소수점 자리수 맞추기 (뒤쪽에 0 패딩)
        decPart.resize(decimal, '0');
        
        // 정수부 + 소수점 + 소수부
        result = intPart + "." + decPart;
    } else {
        // 소수점이 없는 경우 그대로 사용
        result = workValue;
    }
    
    // 전체 길이 조정 (앞쪽 0 패딩)
    int result_length = result.length();
    int target_length = length;
    
    if (isNegative) {
        target_length = length - 1;  // '-' 기호 자리 확보
    }
    
    if (result_length < target_length) {
        result = std::string(target_length - result_length, '0') + result;
    } else if (result_length > target_length) {
        // 길이가 초과하면 뒤에서 잘라내기
        result = result.substr(result_length - target_length);
    }
    
    // 음수인 경우 '-' 기호 추가
    if (isNegative) {
        result = "-" + result;
    }
    
    return result;
}

// 9 모드 파싱 (소수점 포함 데이터 그대로 반환)
std::string BinaryRecord::parse9Mode(const char* data, int length, int decimal) const {
    std::string result(data, length);
    
    // 앞쪽 0 제거 (단, 소수점 앞의 마지막 0은 유지)
    if (decimal > 0 && result.find('.') != std::string::npos) {
        // 소수점이 포함된 경우: 앞쪽 불필요한 0만 제거
        size_t dotPos = result.find('.');
        std::string intPart = result.substr(0, dotPos);
        std::string decPart = result.substr(dotPos);
        
        // 정수부에서 앞의 0 제거 (단, 최소 1자리는 남김)
        while (intPart.length() > 1 && intPart[0] == '0') {
            intPart = intPart.substr(1);
        }
        
        result = intPart + decPart;
    } else {
        // 소수점이 없는 경우: 일반적인 앞쪽 0 제거
        while (result.length() > 1 && result[0] == '0') {
            result = result.substr(1);
        }
    }
    
    return result;
}

// 디버그 출력
void BinaryRecord::dump() const {
    if (!_layout || !_buffer) {
        std::cout << "Invalid record" << std::endl;
        return;
    }
    
    std::cout << "=== Binary Record: " << _layout->getRecordType() << " ===" << std::endl;
    for (const auto& field : _layout->getFields()) {
        std::cout << std::setw(20) << field.name << ": [" << getValue(field.name) << "]" << std::endl;
    }
}

// SpecFileParser 구현
bool SpecFileParser::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open spec file: " << filename << std::endl;
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    return loadFromString(content);
}

bool SpecFileParser::loadFromString(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    bool isFirstLine = true;
    
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        
        // 첫 번째 라인은 헤더로 스킵
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }
        
        std::vector<std::string> fields = parseTsvLine(line);
        if (fields.size() >= 8) {  // 최소 필드 수 (DC 포함)
            std::string specCode = fields[0];
            int sequence = std::atoi(fields[1].c_str());
            bool isKey = (fields[2] == "Y");
            std::string korName = fields[3];        // IEM_NM (한글명)
            std::string engName = fields[4];       // ENG_IEM_NM (영문명)  
            FieldType fieldType = parseType(fields[5]);  // CODE_NM (실제로는 타입)
            int length = parseLength(fields[6]);   // META_LT (길이)
            std::string desc = (fields.size() > 7) ? fields[7] : "";  // DC
            
            // 필드명으로 한글명 사용 (또는 영문명)
            std::string fieldName = engName.empty() ? korName : engName;
            
            // 레이아웃 가져오거나 생성
            if (_layouts.find(specCode) == _layouts.end()) {
                _layouts[specCode] = std::make_shared<RecordLayout>(specCode);
            }
            
            auto layout = _layouts[specCode];
            layout->addField(fieldName, fieldType, length, 0, isKey);
        }
    }
    
    // 모든 레이아웃의 오프셋 계산
    for (auto& pair : _layouts) {
        pair.second->calculateLayout();
    }
    
    return true;
}

std::shared_ptr<RecordLayout> SpecFileParser::getLayout(const std::string& recordType) {
    auto it = _layouts.find(recordType);
    if (it != _layouts.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> SpecFileParser::getRecordTypes() const {
    std::vector<std::string> types;
    for (const auto& pair : _layouts) {
        types.push_back(pair.first);
    }
    return types;
}

std::vector<std::string> SpecFileParser::parseTsvLine(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string field;
    
    while (std::getline(ss, field, '\t')) {
        result.push_back(trim(field));
    }
    
    return result;
}

std::string SpecFileParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

int SpecFileParser::parseLength(const std::string& lengthStr) {
    return lengthStr.empty() ? 0 : std::atoi(lengthStr.c_str());
}

FieldType SpecFileParser::parseType(const std::string& typeStr) {
    return stringToFieldType(typeStr);
}

void SpecFileParser::dump() const {
    for (const auto& pair : _layouts) {
        pair.second->dump();
        std::cout << std::endl;
    }
}

// YAML 디렉토리에서 모든 레이아웃 로드
bool SpecFileParser::loadFromYamlDirectory(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        std::cerr << "Cannot open directory: " << directory << std::endl;
        return false;
    }
    
    struct dirent* entry;
    bool success = true;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // .yaml 파일만 처리
        if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".yaml") {
            std::string filepath = directory + "/" + filename;
            
            std::cout << "Loading YAML file: " << filepath << std::endl;
            if (!loadSingleYamlFile(filepath)) {
                std::cerr << "Failed to load: " << filepath << std::endl;
                success = false;
            }
        }
    }
    
    closedir(dir);
    
    // 모든 레이아웃의 오프셋 계산
    for (auto& pair : _layouts) {
        pair.second->calculateLayout();
    }
    
    return success;
}

// 단일 YAML 파일 로드
bool SpecFileParser::loadSingleYamlFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Cannot open YAML file: " << filepath << std::endl;
        return false;
    }
    
    std::string line;
    std::string layoutName;
    std::string description;
    std::shared_ptr<RecordLayout> layout;
    bool inFieldsSection = false;
    int sequence = 1;
    
    // 현재 필드의 정보를 저장하는 변수들
    std::string currentFieldName;
    FieldType currentFieldType = FieldType::CHAR;
    int currentLength = 0;
    int currentDecimal = 0;
    bool currentIsKey = false;
    std::string currentDescription;
    bool hasFieldData = false;
    
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        
        // Layout name 파싱
        if (trimmed.find("layout_name:") == 0) {
            layoutName = parseYamlValue(trimmed.substr(12));
            layout = std::make_shared<RecordLayout>(layoutName);
        }
        // Description 파싱 (선택사항)
        else if (trimmed.find("description:") == 0) {
            description = parseYamlValue(trimmed.substr(12));
        }
        // Fields 섹션 시작
        else if (trimmed == "fields:") {
            inFieldsSection = true;
        }
        // 새로운 필드 시작 (- 로 시작)
        else if (inFieldsSection && trimmed[0] == '-') {
            // 이전 필드가 있다면 추가
            if (hasFieldData && !currentFieldName.empty()) {
                layout->addField(currentFieldName, currentFieldType, currentLength, currentDecimal, currentIsKey);
                sequence++;
            }
            
            // 새로운 필드 초기화
            currentFieldName = "";
            currentFieldType = FieldType::CHAR;
            currentLength = 0;
            currentDecimal = 0;
            currentIsKey = false;
            currentDescription = "";
            hasFieldData = true;
            
            // 인라인 key_field 값이 있는지 확인
            if (trimmed.find("key_field:") != std::string::npos) {
                size_t pos = trimmed.find("key_field:");
                std::string value = parseYamlValue(trimmed.substr(pos + 10));
                currentIsKey = parseBool(value);
            }
        }
        // 필드 속성들 파싱
        else if (inFieldsSection && trimmed.find(":") != std::string::npos) {
            size_t colonPos = trimmed.find(":");
            std::string key = trim(trimmed.substr(0, colonPos));
            std::string value = parseYamlValue(trimmed.substr(colonPos + 1));
            
            if (key == "key_field") {
                currentIsKey = parseBool(value);
            }
            else if (key == "english_name") {
                currentFieldName = value;
            }
            else if (key == "field_type") {
                currentFieldType = stringToFieldType(value);
            }
            else if (key == "length") {
                currentLength = std::atoi(value.c_str());
            }
            else if (key == "decimal") {
                currentDecimal = std::atoi(value.c_str());
            }
            else if (key == "description") {
                currentDescription = value;
            }
        }
    }
    
    // 마지막 필드 추가
    if (hasFieldData && !currentFieldName.empty()) {
        layout->addField(currentFieldName, currentFieldType, currentLength, currentDecimal, currentIsKey);
    }
    
    if (layout && !layoutName.empty()) {
        _layouts[layoutName] = layout;
        std::cout << "Successfully loaded layout: " << layoutName << " with " << layout->getFields().size() << " fields" << std::endl;
        return true;
    }
    
    return false;
}

// YAML 값 파싱 (따옴표 제거 등)
std::string SpecFileParser::parseYamlValue(const std::string& line) {
    std::string value = trim(line);
    
    // 따옴표 제거
    if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.length() - 2);
    }
    
    return value;
}

// boolean 값 파싱
bool SpecFileParser::parseBool(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "true" || lower == "1" || lower == "yes");
}