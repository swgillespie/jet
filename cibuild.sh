#!/bin/bash
set -e

mkdir ci && cd ci
mkdir debug
mkdir release
cd debug
cmake ../..
make
cd ../release
cmake ../.. -DCMAKE_BUILD_TYPE=Release
make
cd ../..
