/* 
 * EventBase/Protocol 클래스 사용법 데모 - Unix Domain Socket Echo Server/Client
 * 
 * 이 예제는 다음을 보여줍니다:
 * 1. EventBase 팩토리 패턴 사용법 (createEventBase)
 * 2. Protocol 클래스를 이용한 메시지 파싱 (LengthPrefixedProtocol)
 * 3. Zero-Copy evbuffer 기반 데이터 처리
 * 4. 비동기 이벤트 콜백 처리
 * 5. Unix Domain Socket 통신
 */
#include <iostream>
#include <string>
#include <eventBase/EventUnixDomainSocket.h>
#include <event2/event.h>

/* 
 * Unix Domain Socket 설정:
 * - 소켓 파일 경로: /tmp/echo.sock
 * - Server: 해당 경로에서 listen 대기
 * - Client: 해당 경로로 connect 시도
 * - Protocol: LengthPrefixedProtocol (4byte 길이 헤더 + 데이터)
 */

/**
 * EchoServer 클래스 - Unix Domain Socket 에코 서버 구현
 * 
 * EventBase와 Protocol 클래스 사용 예제:
 * 1. 팩토리 패턴으로 EventBase 생성 (createEventBase)
 * 2. Protocol 공유 설계로 메모리 효율성 확보
 * 3. 비동기 accept/read 콜백 처리
 * 4. Zero-Copy 메시지 파싱 및 전송
 */
class EchoServer {
private:
    EventBase* _server;           // 서버 소켓용 EventBase (listen 담당)
    EventBase* _client;           // 클라이언트 연결용 EventBase (accept된 연결 처리)
    LengthPrefixedProtocol _shared_protocol;  // 모든 연결이 공유하는 프로토콜 인스턴스
    
    struct event_base* _base;     // libevent의 메인 이벤트 루프
public:
    /**
     * EchoServer 생성자 - EventBase와 Protocol 초기화
     */
    EchoServer() {
        // 1. libevent 메인 루프 생성
        _base = event_base_new();
        
        // 2. 팩토리 패턴으로 Unix Domain Socket용 EventBase 생성
        _server = createEventBase("unix_domain_socket", _base, true);
        _server->setBase(_base);
        
        // 3. Accept 콜백 등록 - 새로운 클라이언트 연결 시 handle_accept 호출
        _server->setAcceptCallback([this](int fd, struct sockaddr* addr, int len) {
            handle_accept(fd, addr, len);
        });
        
        // 4. Protocol 설정 - 모든 연결이 LengthPrefixedProtocol 공유
        // 메모리 효율성: 하나의 Protocol 인스턴스를 모든 연결이 공유
        _server->setProtocol(&_shared_protocol);
    }
    ~EchoServer() {
        delete _server;
        event_base_free(_base);
    }
    /**
     * Accept 콜백 - 새로운 클라이언트 연결이 들어왔을 때 호출
     * @param fd 새로 accept된 소켓 파일 디스크립터
     * @param addr 클라이언트 주소 정보
     * @param len 주소 구조체 크기
     */
    void handle_accept(int fd, struct sockaddr* addr, int len) {
        std::cout << "EchoServer handle_accept" << std::endl;
        std::cout << "fd: " << fd << std::endl;
        std::cout << "addr: " << addr << std::endl;
        std::cout << "len: " << len << std::endl;
        
        // 1. accept된 연결을 처리할 새로운 EventBase 생성
        _client = createEventBase("unix_domain_socket", _server->getBase(), false);
        
        // 2. 동일한 Protocol 인스턴스 공유 (메모리 효율성)
        _client->setProtocol(&_shared_protocol);
        
        // 3. accept된 소켓 fd로 bufferevent 설정
        // setupBufferevent: libevent의 bufferevent를 fd와 연결
        if (_client->setupBufferevent(fd)) {
            // 4. 데이터 수신 콜백 등록 - Protocol이 완전한 메시지를 파싱하면 호출
            _client->setReadCallback([this](char *data, int size) {
                handle_read(data, size);
            });
        }
    }
    /**
     * 데이터 수신 콜백 - Protocol이 완전한 메시지를 파싱한 후 호출
     * @param data Zero-Copy로 직접 접근하는 메시지 데이터 (evbuffer 내부 포인터)
     * @param size 메시지 크기 (LengthPrefixedProtocol에 의해 파싱된 실제 데이터 크기)
     */
    void handle_read(char *data, int size) {
        std::cout << "EchoServer handle_read" << std::endl;
        std::cout << "\tdata: " << data << std::endl;
        std::cout << "\tsize: " << size << std::endl;
        
        // Protocol을 통한 Zero-Copy 에코 전송
        // trySend: Protocol의 encodeToBuffer를 사용하여 길이 헤더와 함께 전송
        _client->trySend(data, size);
    }
    
    /**
     * 서버 시작 - 지정된 경로에서 listen
     * @param path Unix Domain Socket 파일 경로
     */
    void listen(const std::string& path) {
        try {
            _server->listen(path, true, true);
        } catch (const std::exception& e) {
            std::cerr << "Listen failed: " << e.what() << std::endl;
        }
    }
    
    /**
     * 이벤트 루프 시작 - 블로킹 모드로 이벤트 처리
     */
    void run() {
        _server->start();
    }
};

/**
 * EchoClient 클래스 - Unix Domain Socket 에코 클라이언트 구현
 * 
 * 클라이언트에서의 EventBase와 Protocol 사용 예제:
 * 1. 서버 연결 및 Protocol 기반 메시지 송수신
 * 2. 연결 성공 시 자동으로 hello 메시지 전송
 * 3. Protocol을 통한 구조화된 메시지 처리
 */
class EchoClient {
private:
    std::string _name;                        // 클라이언트 식별용 이름
    EventBase* _client;                       // 클라이언트 연결용 EventBase
    LengthPrefixedProtocol _shared_protocol;  // 클라이언트용 프로토콜 인스턴스
    
    struct event_base* _base;                 // libevent의 메인 이벤트 루프
public:
    /**
     * EchoClient 생성자 - 클라이언트용 EventBase와 Protocol 초기화
     */
    EchoClient() {
        _name = "EchoClient";
        
        // 1. libevent 메인 루프 생성
        _base = event_base_new();
        
        // 2. 팩토리 패턴으로 Unix Domain Socket용 EventBase 생성
        _client = createEventBase("unix_domain_socket", _base, true);
        _client->setBase(_base);
        
        // 3. 데이터 수신 콜백 등록 - Protocol이 완전한 메시지를 파싱하면 호출
        _client->setReadCallback([this](char *data, int size) {
            handle_read(data, size);
        });
        
        // 4. 연결 성공 콜백 등록 - 서버 연결 성공 시 호출
        _client->setConnectCallback([this](char *data, int size) {
            handle_connected(data, size);
        });
        
        // 5. Protocol 설정 - LengthPrefixedProtocol 사용
        _client->setProtocol(&_shared_protocol);
    }
    ~EchoClient() {
        delete _client;
        event_base_free(_base);
    }
    /**
     * 데이터 수신 콜백 - 서버로부터 에코 메시지를 받았을 때 호출
     * @param data Zero-Copy로 직접 접근하는 메시지 데이터 (evbuffer 내부 포인터)
     * @param size 메시지 크기 (LengthPrefixedProtocol에 의해 파싱된 실제 데이터 크기)
     */
    void handle_read(char *data, int size) {
        std::cout << _name << " handle_read" << std::endl;
        std::cout << "data: " << data << std::endl;
        std::cout << "size: " << size << std::endl;
        // 서버로부터의 에코 응답 처리 완료
    }
    
    /**
     * 연결 성공 콜백 - 서버 연결 성공 시 호출
     * @param data 사용하지 않음 (연결 이벤트이므로 데이터 없음)
     * @param size 사용하지 않음
     */
    void handle_connected(char *data, int size) {
        std::cout << "EchoClient connected! Sending hello message..." << std::endl;
        
        // Protocol을 통한 구조화된 메시지 전송
        // trySend: LengthPrefixedProtocol이 4byte 길이 헤더를 자동으로 추가
        std::string hello_msg = "hello";
        _client->trySend(hello_msg.c_str(), hello_msg.length());
        
        (void)data; (void)size;  // 미사용 매개변수 경고 제거
    }
    
    /**
     * 서버 연결 시도
     * @param path Unix Domain Socket 파일 경로
     */
    void connect(const std::string& path) {
        try {
            _client->connect(path);
        } catch (const std::exception& e) {
            std::cerr << "Connect failed: " << e.what() << std::endl;
        }
    }
    
    /**
     * 이벤트 루프 시작 - 블로킹 모드로 이벤트 처리
     */
    void run() {
        _client->start();
    }
};


/**
 * 메인 함수 - EventBase/Protocol 데모 실행
 * 
 * 사용법:
 * - 서버 실행: ./demo_echo_uds server
 * - 클라이언트 실행: ./demo_echo_uds client
 * 
 * 실행 순서:
 * 1. 터미널1에서 서버 실행 (listen 대기)
 * 2. 터미널2에서 클라이언트 실행 (연결 및 hello 메시지 전송)
 * 3. 서버가 Protocol을 통해 메시지 파싱하여 에코 응답
 * 4. 클라이언트가 에코 응답을 Protocol을 통해 수신
 * 
 * 데모가 보여주는 핵심 기능:
 * - EventBase 팩토리 패턴 사용법
 * - Protocol을 통한 구조화된 메시지 파싱
 * - Zero-Copy evbuffer 기반 데이터 처리
 * - 비동기 이벤트 드리븐 아키텍처
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <server|client>" << std::endl;
        std::cerr << "Example:" << std::endl;
        std::cerr << "  Terminal 1: " << argv[0] << " server" << std::endl;
        std::cerr << "  Terminal 2: " << argv[0] << " client" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    const std::string socket_path = "/tmp/echo.sock";
    
    if (mode == "server") {
        std::cout << "=== EventBase/Protocol Demo - Echo Server ===" << std::endl;
        std::cout << "Socket path: " << socket_path << std::endl;
        std::cout << "Protocol: LengthPrefixedProtocol (4byte header + data)" << std::endl;
        std::cout << "Waiting for client connections..." << std::endl;
        
        EchoServer server;
        server.listen(socket_path);
        server.run();
        
    } else if (mode == "client") {
        std::cout << "=== EventBase/Protocol Demo - Echo Client ===" << std::endl;
        std::cout << "Connecting to: " << socket_path << std::endl;
        std::cout << "Protocol: LengthPrefixedProtocol (4byte header + data)" << std::endl;
        std::cout << "Will send 'hello' message after connection..." << std::endl;
        
        EchoClient client;
        client.connect(socket_path);
        client.run();
        
    } else {
        std::cerr << "Error: Invalid mode '" << mode << "'" << std::endl;
        std::cerr << "Usage: " << argv[0] << " <server|client>" << std::endl;
        return 1;
    }
    
    return 0;
}