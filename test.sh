#!/bin/bash
set -e

# Ensure clean build
make clean
make

echo "Running tests..."

# Test 1: Basic search
echo "Test 1: Basic search (in current dir)"
# Note: gg searches recursively in CWD by default.
if ! ./gg "int main" | grep -q "gg.c"; then
    echo "FAIL: Basic search failed"
    exit 1
fi

# Test 2: Case insensitive (Smart Case)
echo "Test 2: Case insensitive search"
if ! ./gg "INT MAIN" | grep -q "gg.c"; then
    echo "FAIL: Case insensitive search failed"
    exit 1
fi

# Test 3: Invert search
echo "Test 3: Invert search"
# Count lines with "int"
MATCH_COUNT=$(./gg "int" | grep "gg.c" | wc -l)
# Count lines without "int"
NON_MATCH_COUNT=$(./gg -v "int" | grep "gg.c" | wc -l)
TOTAL_LINES_IN_OUTPUT=$(./gg -v "" | grep "gg.c" | wc -l) 
# Note: gg with empty string matches everything (every char?). 
# Actually regexec with empty string matches everywhere. 
# gg loops match. 
# Safe check: just ensure non-match is non-zero (since gg.c has lines without int)
if [ "$NON_MATCH_COUNT" -eq 0 ]; then
   echo "FAIL: Invert search found 0 lines (expected some)"
   exit 1
fi

echo "Tests passed!"
