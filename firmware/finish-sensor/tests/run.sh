#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-omit-frame-pointer \
  -Iinclude -I../shared/include -I../controller/include -I../lane-display/include \
  tests/test_finish.cpp -o build/test_finish -lm
./build/test_finish
