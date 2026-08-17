#!/usr/bin/env bash

TARGET_URL="http://localhost:8080/"
DURATION="30s"
THREADS=8
CONNECTIONS=1000

echo "================================================="
echo "  EPOLL ENGINE HIGH-CONCURRENCY BENCHMARK (wrk)  "
echo "================================================="

if ! command -v wrk &> /dev/null; then
    echo "[-] wrk is not installed. Install via: sudo apt install wrk"
    exit 1
fi

wrk -t$THREADS -c$CONNECTIONS -d$DURATION --latency $TARGET_URL