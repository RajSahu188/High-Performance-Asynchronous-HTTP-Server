# High-Performance Asynchronous C++ Epoll Server

An asynchronous, event-driven HTTP server built in C++17 using Linux `epoll` with Edge-Triggered (`EPOLLET`) I/O multiplexing and a worker ThreadPool architecture.

## Features
- **Linux Epoll (EPOLLET)**: Non-blocking I/O event handling.
- **Worker ThreadPool**: Multithreaded execution prevents event-loop blocking.
- **Docker Support**: Containerized build and execution stage.
- **Benchmarking Included**: Automated testing scripts for `wrk` and `ab`.

## Quick Start
```bash
# Build & Run Locally
g++ -std=c++17 -pthread main.cpp -o server
./server

# Make benchmark scripts executable
chmod +x run_wrk_benchmark.sh run_ab_benchmark.sh