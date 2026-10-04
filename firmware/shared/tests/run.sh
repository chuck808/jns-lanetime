#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-omit-frame-pointer \
  -Iinclude tests/test_timebase.cpp -o build/test_timebase -lm
./build/test_timebase
