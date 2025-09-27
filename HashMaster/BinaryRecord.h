#ifndef BINARY_RECORD_H
#define BINARY_RECORD_H

#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <memory>

// 필드 타입 열거형
enum class FieldType {
    CHAR = 0,           // char 
    INT,                // int
    UINT,               // unsigned int
    SHORT,              // short
    USHORT,             // unsigned short
    LONG,               // long
    ULONG,              // unsigned long
    DOUBLE,             // double
    FLOAT,              // float
    X_MODE,             // X 모드 (고정길이 문자열)
    NINE_MODE           // 9 모드 (숫자)
};

// 타입 변환 함수
const char* fieldTypeToString(FieldType type);
FieldType stringToFieldType(const std::string& typeStr);

// 필드 스펙 (간소화)
struct FieldInfo {
    std::string name;       // 필드명
    FieldType type;         // 필드 타입 (enum)
    int offset;             // 버퍼 내 오프셋
    int length;             // 바이트 길이
    int decimal;            // 소수점 자리수 (9 모드용)
    bool isKey;             // 키 필드 여부
    
    FieldInfo() : type(FieldType::CHAR), offset(0), length(0), decimal(0), isKey(false) {}
    FieldInfo(const std::string& n, FieldType t, int len) 
        : name(n), type(t), offset(0), length(len), decimal(0), isKey(false) {}
};

// 바이너리 레코드 스펙
class RecordLayout {
private:
    std::string _recordType;
    std::vector<FieldInfo> _fields;
    std::map<std::string, int> _fieldIndex;
    int _recordSize;
    
public:
    RecordLayout(const std::string& recordType = "");
    
    // 필드 추가
    void addField(const std::string& name, FieldType type, int length, int decimal = 0, bool isKey = false);
    void addField(const FieldInfo& field);
    
    // 필드 정보
    const FieldInfo* getField(const std::string& name) const;
    const std::vector<FieldInfo>& getFields() const { return _fields; }
    int getRecordSize() const { return _recordSize; }
    const std::string& getRecordType() const { return _recordType; }
    
    // 레이아웃 계산
    void calculateLayout();
    void dump() const;  // 디버그용
    
private:
    void updateIndex();
};

// 바이너리 레코드 읽기/쓰기 클래스
class BinaryRecord {
private:
    std::shared_ptr<RecordLayout> _layout;
    char* _buffer;
    bool _ownBuffer;
    
public:
    // 생성자
    BinaryRecord(std::shared_ptr<RecordLayout> layout);
    BinaryRecord(std::shared_ptr<RecordLayout> layout, char* buffer);  // 외부 버퍼 사용
    
    // 복사 생성자 및 할당 연산자 (Rule of Three)
    BinaryRecord(const BinaryRecord& other);
    BinaryRecord& operator=(const BinaryRecord& other);
    
    ~BinaryRecord();
    
    // 버퍼 관리
    void allocateBuffer();
    void setBuffer(char* buffer, bool takeOwnership = false);
    char* getBuffer() const { return _buffer; }
    int getSize() const;
    void clear();
    
    // 데이터 쓰기
    bool setString(const std::string& fieldName, const std::string& value);
    bool setInt(const std::string& fieldName, int value);
    bool setLong(const std::string& fieldName, long long value);
    bool setDouble(const std::string& fieldName, double value);
    bool setXMode(const std::string& fieldName, const std::string& value);    // X 모드
    bool set9Mode(const std::string& fieldName, const std::string& value);    // 9 모드
    
    // 필드 초기화
    bool initXMode(const std::string& fieldName, char fillChar = ' ');       // X 모드 초기화 (기본값: 공백)
    bool init9Mode(const std::string& fieldName, char fillChar = '0');       // 9 모드 초기화 (기본값: 0)
    
    // 데이터 읽기
    std::string getString(const std::string& fieldName) const;
    int getInt(const std::string& fieldName) const;
    long long getLong(const std::string& fieldName) const;
    double getDouble(const std::string& fieldName) const;
    std::string getXMode(const std::string& fieldName) const;
    std::string get9Mode(const std::string& fieldName) const;
    
    // 범용 읽기/쓰기
    bool setValue(const std::string& fieldName, const std::string& value);
    std::string getValue(const std::string& fieldName) const;
    
    // 전체 데이터 처리
    void fromMap(const std::map<std::string, std::string>& data);
    std::map<std::string, std::string> toMap() const;
    
    // 키 필드 처리
    std::string getPrimaryKey() const;
    std::vector<std::string> getKeyValues() const;
    
    // 버퍼 직접 접근
    bool copyFrom(const char* source, int size);
    bool copyTo(char* dest, int size) const;
    
    // 검증
    bool validate() const;
    void dump() const;  // 디버그용
    
private:
    const FieldInfo* getFieldInfo(const std::string& name) const;
    bool writeField(const FieldInfo* field, const std::string& value);
    std::string readField(const FieldInfo* field) const;
    
    // X/9 모드 처리
    std::string formatXMode(const std::string& value, int length) const;
    std::string parseXMode(const char* data, int length) const;
    std::string format9Mode(const std::string& value, int length, int decimal) const;
    std::string parse9Mode(const char* data, int length, int decimal) const;
};

// 스펙 파일 파서
class SpecFileParser {
private:
    std::map<std::string, std::shared_ptr<RecordLayout>> _layouts;
    
public:
    // TSV 파일에서 스펙 로드
    bool loadFromFile(const std::string& filename);
    bool loadFromString(const std::string& content);
    
    // YAML 파일에서 스펙 로드
    bool loadFromYamlDirectory(const std::string& directory);
    
    // 레이아웃 접근
    std::shared_ptr<RecordLayout> getLayout(const std::string& recordType);
    std::vector<std::string> getRecordTypes() const;
    
    void dump() const;  // 디버그용
    
private:
    std::vector<std::string> parseTsvLine(const std::string& line);
    std::string trim(const std::string& str);
    int parseLength(const std::string& lengthStr);
    FieldType parseType(const std::string& typeStr);
    
    // YAML 파싱 헬퍼 함수들
    bool loadSingleYamlFile(const std::string& filepath);
    std::string parseYamlValue(const std::string& line);
    bool parseBool(const std::string& value);
};

#endif // BINARY_RECORD_H