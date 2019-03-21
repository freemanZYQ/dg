#!/bin/bash

set -e

TEST=$1

echo "Test with PTA FI & RDA data-flow"
DG_TESTS_PTA=fi DG_TESTS_RDA=dense ./$TEST

echo "Test with PTA FS & RDA data-flow"
DG_TESTS_PTA=fs DG_TESTS_RDA=dense ./$TEST

echo "Test with PTA FI & RDA ssa"
DG_TESTS_PTA=fi DG_TESTS_RDA=ss ./$TEST

echo "Test with PTA FS & RDA ssa"
DG_TESTS_PTA=fs DG_TESTS_RDA=ss ./$TEST
