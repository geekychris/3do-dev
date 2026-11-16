#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Build the route tools with the port's src/game.c and src/levels.c for the host.
#   tools/route_check.sh              play tools/routes.json through all six caverns
#   tools/route_check.sh search N     shortest route through cavern N (1-6)
set -e
cd "$(dirname "$0")/.."
mkdir -p tools/build
CC=${CC:-cc}
CXX=${CXX:-c++}
INC="-Itools/hostinc -Isrc"
if [ "$1" = search ]; then
    $CC -O2 -w $INC -c src/game.c -o tools/build/game.o
    $CC -O2 -w $INC -c src/levels.c -o tools/build/levels.o
    $CXX -O2 -std=c++11 -w $INC -o tools/build/route_search tools/route_search.cpp tools/build/game.o tools/build/levels.o
    exec tools/build/route_search "$2"
fi
$CC -O2 -w $INC -o tools/build/route_check tools/route_check.c src/game.c src/levels.c
exec tools/build/route_check tools/routes.json
