#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-omit-frame-pointer \
  -Iinclude -I../shared/include tests/test_remote.cpp -o build/test_remote
./build/test_remote
