#!/bin/bash

COMPILER="./build/sodium"
PASS=0
FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

# Tests that check only exit code
declare -A EXIT_TESTS
EXIT_TESTS["test.cyan"]=10
EXIT_TESTS["test_arith.cyan"]=11
EXIT_TESTS["test_arith2.cyan"]=7
EXIT_TESTS["test_chain.cyan"]=47
EXIT_TESTS["test_cmp.cyan"]=2
EXIT_TESTS["test_complex.cyan"]=120
EXIT_TESTS["test_if.cyan"]=100
EXIT_TESTS["test_nested_if.cyan"]=2
EXIT_TESTS["test_paren.cyan"]=16
EXIT_TESTS["test_while.cyan"]=5
EXIT_TESTS["test_logical.cyan"]=7
EXIT_TESTS["test_for.cyan"]=45
EXIT_TESTS["test_scoping.cyan"]=15
EXIT_TESTS["test_func_basic.cyan"]=10
EXIT_TESTS["test_func_multi.cyan"]=20
EXIT_TESTS["test_func_rec.cyan"]=176
EXIT_TESTS["test_arrays.cyan"]=29
EXIT_TESTS["test_lte_gte.cyan"]=4
EXIT_TESTS["test_unary.cyan"]=5
EXIT_TESTS["test_strings.cyan"]=0
EXIT_TESTS["test_standalone_block.cyan"]=6
EXIT_TESTS["test_nested_loops.cyan"]=225
EXIT_TESTS["test_nested_if_while.cyan"]=15
EXIT_TESTS["test_else_if.cyan"]=0
EXIT_TESTS["test_else_if2.cyan"]=0
EXIT_TESTS["test_else_if3.cyan"]=0
EXIT_TESTS["test_else_if4.cyan"]=0
EXIT_TESTS["test_break.cyan"]=3
EXIT_TESTS["test_continue.cyan"]=43
EXIT_TESTS["test_break_for.cyan"]=10
EXIT_TESTS["test_continue_for.cyan"]=46
EXIT_TESTS["test_nested_break.cyan"]=9
EXIT_TESTS["test_compound_assign.cyan"]=6
EXIT_TESTS["test_compound_assign2.cyan"]=55
EXIT_TESTS["test_inc_dec.cyan"]=5
EXIT_TESTS["test_inc_dec_loop.cyan"]=55
EXIT_TESTS["test_inc_dec_loop2.cyan"]=55
EXIT_TESTS["test_mod.cyan"]=3
EXIT_TESTS["test_mod_loop.cyan"]=25
EXIT_TESTS["test_bitwise.cyan"]=26
EXIT_TESTS["test_bitwise2.cyan"]=6
EXIT_TESTS["test_ternary.cyan"]=30
EXIT_TESTS["test_ternary2.cyan"]=50
EXIT_TESTS["test_mod_assign.cyan"]=1

# Tests that also check stdout
declare -A STDOUT_TESTS
STDOUT_TESTS["test_print.cyan"]=0

echo "========================================"
echo "  Cyan Compiler Test Suite"
echo "========================================"
echo ""

for test_file in "${!EXIT_TESTS[@]}"; do
    expected_exit=${EXIT_TESTS[$test_file]}

    if [ ! -f "$test_file" ]; then
        echo -e "${RED}[SKIP]${NC} $test_file (not found)"
        continue
    fi

    echo -n "Testing $test_file... "

    if ! $COMPILER "$test_file" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
        continue
    fi

    ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?

    exit_ok=0
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        exit_ok=1
    fi

    stdout_ok=1
    if [ "${STDOUT_TESTS[$test_file]}" = "0" ]; then
        if [ -s /tmp/cyan_stdout.txt ]; then
            stdout_ok=1
        else
            stdout_ok=0
        fi
    fi

    if [ "$exit_ok" -eq 1 ] && [ "$stdout_ok" -eq 1 ]; then
        echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        if [ "$exit_ok" -ne 1 ]; then
            echo "  Expected exit: $expected_exit, got: $actual_exit"
        fi
        if [ "$stdout_ok" -ne 1 ]; then
            echo "  Expected stdout to be non-empty, but it was empty or had unexpected content"
            echo "  stdout: $(cat /tmp/cyan_stdout.txt)"
        fi
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "========================================"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
