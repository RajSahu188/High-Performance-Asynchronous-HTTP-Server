#!/usr/bin/env bash

TARGET_URL="http://localhost:8080/"
TOTAL_REQUESTS=100000
CONCURRENCY=500

echo "================================================="
echo "   APACHE BENCHMARK (ab) HIGH LOAD TEST          "
echo "================================================="

if ! command -v ab &> /dev/null; then
    echo "[-] apache2-utils is not installed. Install via: sudo apt install apache2-utils"
    exit 1
fi

ab -n $TOTAL_REQUESTS -c $CONCURRENCY -k $TARGET_URL