cmake_minimum_required(VERSION 3.16)

project(asdf LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug flags for detailed debugging
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG -fno-omit-frame-pointer")
set(CMAKE_BUILD_TYPE Debug)

# Use pkg-config to find libevent
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)

# Library: eventbase
add_library(eventbase STATIC
    eventBase/EventBase.cpp
    eventBase/EventUnixDomainSocket.cpp
    eventBase/EventTimer.cpp
    eventBase/Protocol.cpp
)

# Include directories
# Expose project root so headers like <eventBase/EventUnixDomainSocket.h> resolve
# and libevent include dirs

target_include_directories(eventbase
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${LIBEVENT_INCLUDE_DIRS}
)

# Link directories (if needed by pkg-config)
link_directories(${LIBEVENT_LIBRARY_DIRS})

# Link libraries for eventbase
# Note: This will not fix current compile errors, but wires dependencies correctly.

target_link_libraries(eventbase
    PUBLIC
        ${LIBEVENT_LIBRARIES}
)

# Enable PIC for shared library compatibility
set_target_properties(eventbase PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Propagate any special cflags from pkg-config
if (LIBEVENT_CFLAGS_OTHER)
    target_compile_options(eventbase PRIVATE ${LIBEVENT_CFLAGS_OTHER})
endif()

# Common library for pubsub components
add_library(pubsub STATIC
    pubsub/Common.cpp
    common/db_sam.cpp
    pubsub/SimpleSubscriber.cpp
    pubsub/SimplePublisherV2.cpp
    pubsub/PubSubTopicProtocol.cpp
    pubsub/HashmasterSequenceStorage.cpp
)

target_include_directories(pubsub
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${LIBEVENT_INCLUDE_DIRS}
)

target_link_libraries(pubsub
    PUBLIC
        eventbase
        hashmaster
        ${LIBEVENT_LIBRARIES}
)

# Enable PIC for shared library compatibility
set_target_properties(pubsub PROPERTIES POSITION_INDEPENDENT_CODE ON)

# HashMaster library
add_library(hashmaster STATIC
    HashMaster/HashTable.cpp
    HashMaster/HashMaster.cpp
    HashMaster/BinaryRecord.cpp
    HashMaster/MemoryMaster.cpp
)

target_include_directories(hashmaster
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/HashMaster
)

target_link_libraries(hashmaster
    PUBLIC
        pthread
)

target_compile_options(hashmaster PRIVATE -g -O0 -DDEBUG -fno-omit-frame-pointer)

# Enable PIC for shared library compatibility
set_target_properties(hashmaster PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Test executables
option(BUILD_TESTS "Build test binaries" ON)
if (BUILD_TESTS)
    # add_executable(test_publisher
    #     test_publisher.cpp
    # )
    # target_link_libraries(test_publisher PRIVATE pubsub eventbase)
    # target_include_directories(test_publisher PRIVATE ${PROJECT_SOURCE_DIR})
    
    add_executable(test_subscriber
        test_subscriber.cpp
    )
    target_link_libraries(test_subscriber PRIVATE pubsub eventbase)
    target_include_directories(test_subscriber PRIVATE ${PROJECT_SOURCE_DIR})
    
# Temporarily disable test_pubsub_integration due to SimplePublisher dependency
    # add_executable(test_pubsub_integration
    #     test_pubsub_integration.cpp
    # )
    # target_link_libraries(test_pubsub_integration PRIVATE pubsub eventbase)
    # target_include_directories(test_pubsub_integration PRIVATE ${PROJECT_SOURCE_DIR})
    
# Temporarily disable test_modern_publisher due to ModernClient dependency
    # add_executable(test_modern_publisher
    #     test_modern_publisher.cpp
    # )
    # target_link_libraries(test_modern_publisher PRIVATE pubsub eventbase)
    # target_include_directories(test_modern_publisher PRIVATE ${PROJECT_SOURCE_DIR})
    
    # add_executable(test_stable_publisher
    #     test_stable_publisher.cpp
    # )
    # target_link_libraries(test_stable_publisher PRIVATE pubsub eventbase)
    # target_include_directories(test_stable_publisher PRIVATE ${PROJECT_SOURCE_DIR})
endif()

# Demo executable
option(BUILD_DEMOS "Build demo binaries" OFF)
if (BUILD_DEMOS)
    add_executable(demo_echo_uds
        demo/demo_event_unix_domain_socket.cpp
    )
    target_link_libraries(demo_echo_uds PRIVATE eventbase)
    target_include_directories(demo_echo_uds PRIVATE ${PROJECT_SOURCE_DIR})
    
    add_executable(demo_timer
        demo/demo_event_timer.cpp
    )
    target_link_libraries(demo_timer PRIVATE eventbase)
    target_include_directories(demo_timer PRIVATE ${PROJECT_SOURCE_DIR})
    
    add_executable(process3_subscriber1
        demo/process3_subscriber1.cpp
        pubsub/SimpleSubscriber.cpp
        pubsub/PubSubTopicProtocol.cpp
        pubsub/HashmasterSequenceStorage.cpp
    )
    target_link_libraries(process3_subscriber1 PRIVATE eventbase hashmaster)
    target_include_directories(process3_subscriber1 PRIVATE ${PROJECT_SOURCE_DIR})

    # HashMaster demo
    add_executable(demo_memory_master
        HashMaster/demo_memory_master.cpp
    )
    target_link_libraries(demo_memory_master PRIVATE hashmaster)
    target_include_directories(demo_memory_master PRIVATE ${PROJECT_SOURCE_DIR})

    # MasterManager demo
    add_executable(demo_master_manager
        HashMaster/demo_master_manager.cpp
        HashMaster/MasterManager.cpp
    )
    target_link_libraries(demo_master_manager PRIVATE hashmaster)
    target_include_directories(demo_master_manager PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_options(demo_master_manager PRIVATE -g -O0 -DDEBUG -fno-omit-frame-pointer)

    # Data generator publisher demo
    add_executable(process1_data_generator_pub
        demo/process1_data_generator_pub.cpp
    )
    target_link_libraries(process1_data_generator_pub PRIVATE pubsub eventbase)
    target_include_directories(process1_data_generator_pub PRIVATE ${PROJECT_SOURCE_DIR})

    # HashMaster viewer utility
    add_executable(hashmaster_viewer
        utils/hashmaster_viewer.cpp
    )
    target_link_libraries(hashmaster_viewer PRIVATE hashmaster)
    target_include_directories(hashmaster_viewer PRIVATE ${PROJECT_SOURCE_DIR})

    # DB_SAM viewer utility
    add_executable(dbsam_viewer
        utils/dbsam_viewer.cpp
    )
    target_link_libraries(dbsam_viewer PRIVATE pubsub hashmaster)
    target_include_directories(dbsam_viewer PRIVATE ${PROJECT_SOURCE_DIR})
endif() 
# SimplePublisherV2 test
add_executable(test_simple_publisher_v2
    test_simple_publisher_v2.cpp
    pubsub/SimplePublisherV2.cpp
    pubsub/PubSubTopicProtocol.cpp
    common/db_sam.cpp
)
target_link_libraries(test_simple_publisher_v2 PRIVATE pubsub eventbase)
target_include_directories(test_simple_publisher_v2 PRIVATE ${PROJECT_SOURCE_DIR})

# SimplePublisherV2 + SimpleSubscriber integration test
add_executable(test_pubsub_v2_integration
    test_pubsub_v2_integration.cpp
    pubsub/SimplePublisherV2.cpp
    pubsub/SimpleSubscriber.cpp
    pubsub/PubSubTopicProtocol.cpp
    common/db_sam.cpp
)
target_link_libraries(test_pubsub_v2_integration PRIVATE pubsub eventbase)
target_include_directories(test_pubsub_v2_integration PRIVATE ${PROJECT_SOURCE_DIR})

# T2MA library (static for internal use)
add_library(t2ma STATIC
    t2ma/T2MASystem.cpp
    common/MQReader.cpp
    common/YAMLParser.cpp
    HashMaster/MasterManager.cpp
)

# T2MA shared library for dynamic loading (contains all T2MA implementations)
add_library(T2MA SHARED
    t2ma/T2MASystem.cpp
    t2ma/T2MA_JAPAN_EQUITY.cpp
    common/MQReader.cpp
    common/YAMLParser.cpp
    HashMaster/MasterManager.cpp
    # Future: t2ma/T2MA_US_EQUITY.cpp
    # Future: t2ma/T2MA_GERMAN_EQUITY.cpp
    # Future: t2ma/T2MA_OPTIONS.cpp
)

target_include_directories(T2MA
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${LIBEVENT_INCLUDE_DIRS}
)

target_link_libraries(T2MA
    PUBLIC
        eventbase
        pubsub
        hashmaster
        ${LIBEVENT_LIBRARIES}
        dl
        pthread
)

target_include_directories(t2ma
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${LIBEVENT_INCLUDE_DIRS}
)

target_link_libraries(t2ma
    PUBLIC
        eventbase
        pubsub
        hashmaster
        ${LIBEVENT_LIBRARIES}
        dl  # For dynamic loading
        pthread
)

# T2MA demo executable
add_executable(t2ma_with_system_config
    t2ma/t2ma_with_system_config.cpp
)

target_link_libraries(t2ma_with_system_config PRIVATE t2ma)
target_include_directories(t2ma_with_system_config PRIVATE ${PROJECT_SOURCE_DIR})
