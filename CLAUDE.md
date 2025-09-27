# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**asdf** (all source data feed) is a C++ event-driven pub/sub system designed for high-performance market data distribution. It implements a gap-free recovery mechanism for reliable data streaming using libevent and supports both Unix domain sockets and TCP connections.

## Build System

This project uses CMake with C++17 standard:

```bash
# Create build directory and configure
mkdir build && cd build
cmake ..

# Build the project
make

# Build with demos enabled
cmake -DBUILD_DEMOS=ON ..
make
```

## Architecture

### Core Components

- **EventBase**: Abstract base class for event-driven programming using libevent
  - Supports TCP, UDP, Unix domain sockets, and message queues
  - Provides callbacks for read, write, connect, disconnect, timeout, and error events
  - Located in `eventBase/`

- **SimplePubSub Namespace**: Market data pub/sub system (`common/Common.h`)
  - **DataTopic enum**: Categorizes market data (TOPIC1, TOPIC2, MISC, ALL_TOPICS)
  - **Client Management**: Tracks connection status (CONNECTED, RECOVERING, CATCHING_UP, ONLINE, OFFLINE)
  - **Gap-Free Recovery**: Ensures no message loss during client reconnections
  - **Sequence Management**: Global and topic-specific sequence numbering

- **SequenceStorage**: Persistence layer for sequence tracking (`pubsub/SequenceStorage.h`)
  - Pluggable storage strategy interface
  - Supports HashMaster and file-based storage backends

### Key Data Structures

- **TopicMessage**: Core message format with magic number, topic, sequences, timestamp, and data
- **ClientInfo**: Tracks client state, subscriptions, and recovery progress
- **PendingMessage**: Buffers messages during client recovery
- **PublisherSequenceRecord**: Persistent sequence state per publisher

### Message Protocol

Uses magic number-based protocol with structures for:
- Subscription requests/responses
- Recovery requests/responses/completion
- Topic messages with sequence tracking

## Directory Structure

- `eventBase/`: Event handling abstraction layer
- `common/`: Core pub/sub definitions and utilities  
- `pubsub/`: Persistence and sequence management
- `demo/`: Example implementations
- `test/`: Test files (currently empty)
- `build/`: CMake build artifacts

## Dependencies

- **libevent**: Event-driven networking library (detected via pkg-config)
- **C++17**: Standard library features for modern C++

## Development Notes

- The codebase includes Korean comments explaining the event-driven pub/sub architecture
- Uses factory pattern for EventBase creation (`createEventBase` function)
- Implements Command pattern for single-threaded recovery processing
- Thread-safe design with atomic operations and proper synchronization