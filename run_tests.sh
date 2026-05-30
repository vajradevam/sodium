#!/bin/bash

COMPILER="./build/sodium"
PASS=0
FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "========================================"
echo "  Cyan Compiler Test Suite"
echo "========================================"
echo ""

# ── Unit tests (tests/unit/) ───────────────────────────────────────
declare -A UNIT_TESTS
UNIT_TESTS["test.cyan"]=10
UNIT_TESTS["test_arith.cyan"]=11
UNIT_TESTS["test_arith2.cyan"]=7
UNIT_TESTS["test_chain.cyan"]=47
UNIT_TESTS["test_cmp.cyan"]=2
UNIT_TESTS["test_if.cyan"]=100
UNIT_TESTS["test_nested_if.cyan"]=2
UNIT_TESTS["test_paren.cyan"]=16
UNIT_TESTS["test_while.cyan"]=5
UNIT_TESTS["test_logical.cyan"]=7
UNIT_TESTS["test_for.cyan"]=45
UNIT_TESTS["test_scoping.cyan"]=15
UNIT_TESTS["test_func_basic.cyan"]=10
UNIT_TESTS["test_func_multi.cyan"]=20
UNIT_TESTS["test_func_rec.cyan"]=176
UNIT_TESTS["test_arrays.cyan"]=29
UNIT_TESTS["test_lte_gte.cyan"]=4
UNIT_TESTS["test_unary.cyan"]=5
UNIT_TESTS["test_strings.cyan"]=0
UNIT_TESTS["test_standalone_block.cyan"]=6
UNIT_TESTS["test_else_if.cyan"]=0
UNIT_TESTS["test_else_if2.cyan"]=0
UNIT_TESTS["test_else_if3.cyan"]=0
UNIT_TESTS["test_else_if4.cyan"]=0
UNIT_TESTS["test_break.cyan"]=3
UNIT_TESTS["test_continue.cyan"]=43
UNIT_TESTS["test_break_for.cyan"]=10
UNIT_TESTS["test_continue_for.cyan"]=46
UNIT_TESTS["test_nested_break.cyan"]=9
UNIT_TESTS["test_compound_assign.cyan"]=6
UNIT_TESTS["test_compound_assign2.cyan"]=55
UNIT_TESTS["test_inc_dec.cyan"]=5
UNIT_TESTS["test_inc_dec_loop.cyan"]=55
UNIT_TESTS["test_inc_dec_loop2.cyan"]=55
UNIT_TESTS["test_mod.cyan"]=3
UNIT_TESTS["test_mod_loop.cyan"]=25
UNIT_TESTS["test_bitwise.cyan"]=26
UNIT_TESTS["test_bitwise2.cyan"]=6
UNIT_TESTS["test_ternary.cyan"]=30
UNIT_TESTS["test_ternary2.cyan"]=50
UNIT_TESTS["test_mod_assign.cyan"]=1
UNIT_TESTS["test_bitwise_assign.cyan"]=30
UNIT_TESTS["test_do_while.cyan"]=10
UNIT_TESTS["test_do_while_break.cyan"]=5
UNIT_TESTS["test_do_while_continue.cyan"]=12
UNIT_TESTS["test_switch.cyan"]=30
UNIT_TESTS["test_switch_break.cyan"]=20
UNIT_TESTS["test_switch_default.cyan"]=99
UNIT_TESTS["test_switch_continue.cyan"]=12
UNIT_TESTS["test_global.cyan"]=15
UNIT_TESTS["test_global_func.cyan"]=3
UNIT_TESTS["test_arr_lit.cyan"]=150
UNIT_TESTS["test_types.cyan"]=44
UNIT_TESTS["test_types_compound.cyan"]=15
UNIT_TESTS["test_static.cyan"]=123
UNIT_TESTS["test_for_compound_update.cyan"]=55
UNIT_TESTS["test_for_global_update.cyan"]=10
UNIT_TESTS["test_print.cyan"]=0
UNIT_TESTS["test_const_arr.cyan"]=55
UNIT_TESTS["test_static2.cyan"]=56
UNIT_TESTS["test_struct.cyan"]=91
UNIT_TESTS["test_struct_fields.cyan"]=91
UNIT_TESTS["test_struct_scope.cyan"]=20
UNIT_TESTS["test_ptr_basic.cyan"]=42
UNIT_TESTS["test_ptr_assign.cyan"]=94
UNIT_TESTS["test_malloc.cyan"]=123
UNIT_TESTS["test_ptr_global.cyan"]=250
UNIT_TESTS["test_ptr_double.cyan"]=14
UNIT_TESTS["test_ptr_struct.cyan"]=50
UNIT_TESTS["test_ptr_expr.cyan"]=15
UNIT_TESTS["test_ptr_func.cyan"]=42
UNIT_TESTS["test_allocator_stress.cyan"]=1
UNIT_TESTS["test_bitwise_not.cyan"]=80
UNIT_TESTS["test_deref_compound.cyan"]=1
UNIT_TESTS["test_div_mod.cyan"]=1
UNIT_TESTS["test_empty_blocks.cyan"]=1
UNIT_TESTS["test_forward_func.cyan"]=1
UNIT_TESTS["test_global_complex.cyan"]=1
UNIT_TESTS["test_inc_dec_edge.cyan"]=1
UNIT_TESTS["test_many_args.cyan"]=1
UNIT_TESTS["test_nested_control.cyan"]=1
UNIT_TESTS["test_string_escape.cyan"]=0
UNIT_TESTS["test_struct_compound.cyan"]=1
UNIT_TESTS["test_type_conv.cyan"]=1

# Unit tests that should fail to compile
declare -a COMPILE_FAIL_UNIT
COMPILE_FAIL_UNIT+=("test_arr_scalar_assign.cyan")
COMPILE_FAIL_UNIT+=("test_struct_bad_field.cyan")
COMPILE_FAIL_UNIT+=("test_struct_not_struct.cyan")
COMPILE_FAIL_UNIT+=("test_struct_unknown_type.cyan")
COMPILE_FAIL_UNIT+=("test_struct_bad_read.cyan")
COMPILE_FAIL_UNIT+=("test_ptr_bad_addr.cyan")
COMPILE_FAIL_UNIT+=("test_bad_const_assign.cyan")
COMPILE_FAIL_UNIT+=("test_bad_ops.cyan")

# Unit tests that check stdout
declare -A STDOUT_TESTS
STDOUT_TESTS["test_print.cyan"]=0

# Unit tests requiring stdin
declare -A STDIN_TESTS
STDIN_TESTS["test_read.cyan|42"]=42

for test_file in "${!UNIT_TESTS[@]}"; do
    filepath="tests/unit/$test_file"
    expected_exit=${UNIT_TESTS[$test_file]}

    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} unit/$test_file (not found)"
        continue
    fi

    echo -n "Testing unit/$test_file... "

    if ! $COMPILER "$filepath" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
        continue
    fi

    ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?

    exit_ok=0
    [ "$actual_exit" -eq "$expected_exit" ] && exit_ok=1

    stdout_ok=1
    if [ "${STDOUT_TESTS[$test_file]}" = "0" ]; then
        [ -s /tmp/cyan_stdout.txt ] && stdout_ok=1 || stdout_ok=0
    fi

    if [ "$exit_ok" -eq 1 ] && [ "$stdout_ok" -eq 1 ]; then
        echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        [ "$exit_ok" -ne 1 ] && echo "  Expected exit: $expected_exit, got: $actual_exit"
        [ "$stdout_ok" -ne 1 ] && echo "  Expected stdout non-empty, got: $(cat /tmp/cyan_stdout.txt)"
        FAIL=$((FAIL + 1))
    fi
done

# ── Unit compile-failure tests ─────────────────────────────────────
for test_file in "${COMPILE_FAIL_UNIT[@]}"; do
    filepath="tests/unit/$test_file"
    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} unit/$test_file (not found)"
        continue
    fi
    echo -n "Testing unit/$test_file (expect compile error)... "
    if $COMPILER "$filepath" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}FAIL — compiled successfully (expected error)${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
    else
        echo -e "${GREEN}PASS${NC} (correctly rejected)"
        PASS=$((PASS + 1))
    fi
done

# ── Unit stdin tests ───────────────────────────────────────────────
for stdin_entry in "${!STDIN_TESTS[@]}"; do
    IFS='|' read -r test_file stdin_value <<< "$stdin_entry"
    filepath="tests/unit/$test_file"
    expected_exit=${STDIN_TESTS["$stdin_entry"]}

    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} unit/$test_file (not found)"
        continue
    fi
    echo -n "Testing unit/$test_file (stdin: $stdin_value)... "
    if ! $COMPILER "$filepath" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
        continue
    fi
    echo "$stdin_value" | ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected exit: $expected_exit, got: $actual_exit"
        FAIL=$((FAIL + 1))
    fi
done

# ── Integration tests (tests/integration/) ─────────────────────────
declare -A INTEGRATION_TESTS
INTEGRATION_TESTS["test_nested_loops.cyan"]=225
INTEGRATION_TESTS["test_nested_if_while.cyan"]=15
INTEGRATION_TESTS["test_complex.cyan"]=120
INTEGRATION_TESTS["test_func_stress.cyan"]=1
INTEGRATION_TESTS["test_gcd.cyan"]=1
INTEGRATION_TESTS["test_sieve.cyan"]=1

for test_file in "${!INTEGRATION_TESTS[@]}"; do
    filepath="tests/integration/$test_file"
    expected_exit=${INTEGRATION_TESTS[$test_file]}

    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} integration/$test_file (not found)"
        continue
    fi
    echo -n "Testing integration/$test_file... "
    if ! $COMPILER "$filepath" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
        continue
    fi
    ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected exit: $expected_exit, got: $actual_exit"
        FAIL=$((FAIL + 1))
    fi
done

# ── Include tests (tests/include/) ─────────────────────────────────
declare -A INCLUDE_TESTS
INCLUDE_TESTS["test_include_basic.cyan"]=30
INCLUDE_TESTS["test_pragma_once.cyan"]=70

for test_file in "${!INCLUDE_TESTS[@]}"; do
    filepath="tests/include/$test_file"
    expected_exit=${INCLUDE_TESTS[$test_file]}

    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} include/$test_file (not found)"
        continue
    fi
    echo -n "Testing include/$test_file... "
    if ! $COMPILER "$filepath" -I "tests/include" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}COMPILE FAIL${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
        continue
    fi
    ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Expected exit: $expected_exit, got: $actual_exit"
        FAIL=$((FAIL + 1))
    fi
done

# Include compile-failure test
for test_file in "test_include_missing.cyan"; do
    filepath="tests/include/$test_file"
    if [ ! -f "$filepath" ]; then
        echo -e "${RED}[SKIP]${NC} include/$test_file (not found)"
        continue
    fi
    echo -n "Testing include/$test_file (expect compile error)... "
    if $COMPILER "$filepath" -I "tests/include" > /tmp/cyan_compile.log 2>&1; then
        echo -e "${RED}FAIL — compiled successfully (expected error)${NC}"
        cat /tmp/cyan_compile.log
        FAIL=$((FAIL + 1))
    else
        echo -e "${GREEN}PASS${NC} (correctly rejected)"
        PASS=$((PASS + 1))
    fi
done

# ── RISC-V tests ──────────────────────────────────────────────────
if command -v qemu-riscv64 &>/dev/null && command -v riscv64-elf-gcc &>/dev/null; then
    echo ""
    echo "  --- RISC-V backend ---"
    echo ""

    # Unit tests (all except stdin/stdout-checks)
    for test_file in "${!UNIT_TESTS[@]}"; do
        filepath="tests/unit/$test_file"
        expected_exit=${UNIT_TESTS[$test_file]}

        if [ ! -f "$filepath" ]; then
            echo -e "${RED}[SKIP]${NC} RV64 unit/$test_file (not found)"
            continue
        fi

        echo -n "Testing RV64 unit/$test_file... "

        if ! $COMPILER --target riscv64 "$filepath" > /tmp/cyan_compile.log 2>&1; then
            echo -e "${RED}COMPILE FAIL${NC}"
            cat /tmp/cyan_compile.log
            FAIL=$((FAIL + 1))
            continue
        fi

        qemu-riscv64 ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?

        if [ "$actual_exit" -eq "$expected_exit" ]; then
            echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
            PASS=$((PASS + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected exit: $expected_exit, got: $actual_exit"
            FAIL=$((FAIL + 1))
        fi
    done

    # Unit compile-failure tests
    for test_file in "${COMPILE_FAIL_UNIT[@]}"; do
        filepath="tests/unit/$test_file"
        if [ ! -f "$filepath" ]; then
            echo -e "${RED}[SKIP]${NC} RV64 unit/$test_file (not found)"
            continue
        fi
        echo -n "Testing RV64 unit/$test_file (expect compile error)... "
        if $COMPILER --target riscv64 "$filepath" > /tmp/cyan_compile.log 2>&1; then
            echo -e "${RED}FAIL — compiled successfully (expected error)${NC}"
            cat /tmp/cyan_compile.log
            FAIL=$((FAIL + 1))
        else
            echo -e "${GREEN}PASS${NC} (correctly rejected)"
            PASS=$((PASS + 1))
        fi
    done

    # Integration tests
    for test_file in "${!INTEGRATION_TESTS[@]}"; do
        filepath="tests/integration/$test_file"
        expected_exit=${INTEGRATION_TESTS[$test_file]}

        if [ ! -f "$filepath" ]; then
            echo -e "${RED}[SKIP]${NC} RV64 integration/$test_file (not found)"
            continue
        fi
        echo -n "Testing RV64 integration/$test_file... "
        if ! $COMPILER --target riscv64 "$filepath" > /tmp/cyan_compile.log 2>&1; then
            echo -e "${RED}COMPILE FAIL${NC}"
            cat /tmp/cyan_compile.log
            FAIL=$((FAIL + 1))
            continue
        fi
        qemu-riscv64 ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?
        if [ "$actual_exit" -eq "$expected_exit" ]; then
            echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
            PASS=$((PASS + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected exit: $expected_exit, got: $actual_exit"
            FAIL=$((FAIL + 1))
        fi
    done

    # Include tests
    for test_file in "${!INCLUDE_TESTS[@]}"; do
        filepath="tests/include/$test_file"
        expected_exit=${INCLUDE_TESTS[$test_file]}

        if [ ! -f "$filepath" ]; then
            echo -e "${RED}[SKIP]${NC} RV64 include/$test_file (not found)"
            continue
        fi
        echo -n "Testing RV64 include/$test_file... "
        if ! $COMPILER --target riscv64 "$filepath" -I "tests/include" > /tmp/cyan_compile.log 2>&1; then
            echo -e "${RED}COMPILE FAIL${NC}"
            cat /tmp/cyan_compile.log
            FAIL=$((FAIL + 1))
            continue
        fi
        qemu-riscv64 ./out > /tmp/cyan_stdout.txt 2>&1; actual_exit=$?
        if [ "$actual_exit" -eq "$expected_exit" ]; then
            echo -e "${GREEN}PASS${NC} (exit: $actual_exit)"
            PASS=$((PASS + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected exit: $expected_exit, got: $actual_exit"
            FAIL=$((FAIL + 1))
        fi
    done

    # Include compile-failure test
    for test_file in "test_include_missing.cyan"; do
        filepath="tests/include/$test_file"
        if [ ! -f "$filepath" ]; then
            continue
        fi
        echo -n "Testing RV64 include/$test_file (expect compile error)... "
        if $COMPILER --target riscv64 "$filepath" -I "tests/include" > /tmp/cyan_compile.log 2>&1; then
            echo -e "${RED}FAIL — compiled successfully (expected error)${NC}"
            cat /tmp/cyan_compile.log
            FAIL=$((FAIL + 1))
        else
            echo -e "${GREEN}PASS${NC} (correctly rejected)"
            PASS=$((PASS + 1))
        fi
    done
else
    echo ""
    echo "  --- RISC-V backend: SKIPPED (qemu-riscv64 not found) ---"
fi

# ── Summary ────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
