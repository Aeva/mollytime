#!/usr/bin/env sh

cd `dirname $0`

cmake -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release

exec cmake --build build/Release
#exec cmake --build build/Debug