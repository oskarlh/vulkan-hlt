#!/bin/sh

# Exit on failure
set -e

cmake -S . --preset=release-config
cmake --build --preset=release-build
#./test-and-compare/test-and-compare.sh pool
./test-and-compare/test-and-compare.sh xmastree_tjb
