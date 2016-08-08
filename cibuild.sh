#!/bin/bash
set -e

mkdir ci && cd ci
mkdir debug
mkdir release
cd debug
cmake ../..
make -j4
cd ../release
cmake ../.. -DCMAKE_BUILD_TYPE=Release
make -j4
cd ../..

export JET_TEST_EXE=$(pwd)/ci/debug/src/jet
cd test
ruby run_tests.rb -v