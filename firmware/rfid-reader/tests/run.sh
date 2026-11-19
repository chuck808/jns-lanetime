#!/bin/sh
set -eu
cd "$(dirname "$0")"
out=$(mktemp)
trap 'rm -f "$out"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -I../include test_presentation.cpp -o "$out"
"$out"
