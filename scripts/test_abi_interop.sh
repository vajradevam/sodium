#!/bin/bash
# C-interop ABI test: struct parameters/returns must match what gcc does (D-012).
# For every tests/abi/<name>.cyan with a <name>.c twin: compile the Cyan file to
# assembly, assemble it, compile the C file with the system C compiler for the same
# target, weaken in the Cyan object every symbol the C object also defines (so the C
# definition wins at link time), link with the Sodium runtime and run. The program's
# exit code must be 0 (a non-zero code N means check N of that test failed).
# Usage: scripts/test_abi_interop.sh [x86_64|riscv64|both]   (default: both)
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
case "${1:-both}" in
    x86_64) TARGETS="x86_64" ;;
    riscv64) TARGETS="riscv64" ;;
    both) TARGETS="x86_64 riscv64" ;;
    *) echo "usage: $0 [x86_64|riscv64|both]" >&2; exit 2 ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/abi.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

CFLAGS_COMMON="-O1 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -Wall -Werror -c"

pass=0
fail=0
for cyan in $(find tests/abi -type f -name '*.cyan' | sort); do
    c_source="${cyan%.cyan}.c"
    name="$(basename "$cyan" .cyan)"
    if [ ! -f "$c_source" ]; then
        echo "MISSING C TWIN: $c_source"
        fail=$((fail + 1))
        continue
    fi
    for target in $TARGETS; do
        label="$name [$target]"
        if [ "$target" = "x86_64" ]; then
            asm_cmd=(nasm -felf64)
            cc_cmd=(gcc $CFLAGS_COMMON)
            objcopy_cmd=(objcopy)
            nm_cmd=(nm)
            runner=()
        else
            asm_cmd=(riscv64-elf-as -march=rv64gc -mno-relax -o)
            cc_cmd=(riscv64-linux-gnu-gcc $CFLAGS_COMMON -march=rv64gc -mabi=lp64d -mno-relax)
            objcopy_cmd=(riscv64-elf-objcopy)
            nm_cmd=(riscv64-elf-nm)
            runner=(qemu-riscv64)
        fi

        # This harness rewrites `call SYM` lines in the generated assembly so
        # C definitions win at link time. Inlining would remove those calls,
        # so compile the Cyan side at -O0 explicitly (D-032 made -O1 default).
        if ! "$COMPILER" -O0 --target "$target" -w -S -o "$WORK/cy.s" "$cyan" > "$WORK/cy.log" 2>&1; then
            echo "FAIL $label: Cyan compile failed"; cat "$WORK/cy.log"; fail=$((fail + 1)); continue
        fi
        if ! "${cc_cmd[@]}" "$c_source" -o "$WORK/c.o" > "$WORK/cc.log" 2>&1; then
            echo "FAIL $label: C compile failed"; cat "$WORK/cc.log"; fail=$((fail + 1)); continue
        fi

        # Symbols the C object defines override the Cyan definitions of the same name.
        overrides=$("${nm_cmd[@]}" --defined-only -g "$WORK/c.o" | awk '{print $3}')
        extra_objs=()
        if [ "$target" = "x86_64" ]; then
            # NASM binds a call to a symbol defined in the same file at assembly time, so
            # weakening the definition alone would not redirect it. Calls to overridden
            # symbols are routed through an undefined alias SYM__ovr, which a tiny
            # trampoline object forwards (jmp keeps arguments and stack intact) to SYM.
            : > "$WORK/tramp.s"
            for sym in $overrides; do
                if grep -qE "^[[:space:]]*call[[:space:]]+${sym}[[:space:]]*$" "$WORK/cy.s"; then
                    sed -i -E "s/^([[:space:]]*call[[:space:]]+)${sym}[[:space:]]*$/\1${sym}__ovr/" "$WORK/cy.s"
                    sed -i "1i extern ${sym}__ovr" "$WORK/cy.s"
                    printf 'global %s__ovr\nextern %s\n%s__ovr: jmp %s\n' "$sym" "$sym" "$sym" "$sym" >> "$WORK/tramp.s"
                fi
            done
            nasm -felf64 "$WORK/tramp.s" -o "$WORK/tramp.o" || { echo "FAIL $label: trampolines"; fail=$((fail + 1)); continue; }
            extra_objs=("$WORK/tramp.o")
            "${asm_cmd[@]}" "$WORK/cy.s" -o "$WORK/cy.o" > "$WORK/as.log" 2>&1
        else
            "${asm_cmd[@]}" "$WORK/cy.o" "$WORK/cy.s" > "$WORK/as.log" 2>&1
        fi
        if [ $? -ne 0 ]; then
            echo "FAIL $label: assemble failed"; cat "$WORK/as.log"; fail=$((fail + 1)); continue
        fi

        # Weaken the overridden symbols in the Cyan object so the C definition wins.
        weaken=()
        for sym in $overrides; do
            if "${nm_cmd[@]}" --defined-only "$WORK/cy.o" | awk '{print $3}' | grep -qx "$sym"; then
                weaken+=(--weaken-symbol="$sym")
            fi
        done
        if [ ${#weaken[@]} -eq 0 ]; then
            echo "FAIL $label: C object overrides nothing in the Cyan object"; fail=$((fail + 1)); continue
        fi
        "${objcopy_cmd[@]}" "${weaken[@]}" "$WORK/cy.o"

        if [ "$target" = "x86_64" ]; then
            ld -o "$WORK/prog" "$WORK/cy.o" "$WORK/c.o" "${extra_objs[@]}" sodium-rt/sodium-rt.a > "$WORK/ld.log" 2>&1
        else
            riscv64-elf-gcc -nostdlib -static -Wl,--no-relax -o "$WORK/prog" "$WORK/cy.o" "$WORK/c.o" \
                sodium-rt/riscv64/sodium-rt.a > "$WORK/ld.log" 2>&1
        fi
        if [ $? -ne 0 ]; then
            echo "FAIL $label: link failed"; cat "$WORK/ld.log"; fail=$((fail + 1)); continue
        fi

        "${runner[@]}" "$WORK/prog" > "$WORK/run.out" 2>&1
        rc=$?
        if [ "$rc" -eq 0 ]; then
            pass=$((pass + 1))
        else
            echo "FAIL $label: exit code $rc (check number that failed)"
            fail=$((fail + 1))
        fi
    done
done

echo "ABI interop: $pass passed, $fail failed"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]
