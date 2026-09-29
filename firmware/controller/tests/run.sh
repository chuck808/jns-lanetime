#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-omit-frame-pointer \
  -Iinclude -I../shared/include tests/test_controller.cpp -o build/test_controller
./build/test_controller
