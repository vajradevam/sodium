# Phase T coverage inventory (T-1)

Snapshot of the repository at commit `256d389` (baseline `a1ec4d9`). Counts are from
`find tests -name '*.cyan'`: **399** sources, 776 passing run-equivalents per optimisation level
(x86_64 + riscv64, 18 skipped helpers). This document is an *inventory*, not a claim of correctness:
a test existing does not mean the edge it names is covered.

## Feature / diagnostic map

| Area | Existing coverage (examples) | Gaps that Phase T targets |
| --- | --- | --- |
| Integer lexing | `test_bad_literal_overflow`, `test_bad_hex_overflow`, `test_bad_float_exponent`, `test_bad_float_suffix_on_int` | exact i64/u64 boundary literals, overflow by one digit in hex/octal/binary, leading zeros, malformed prefixes, digit separators rejected |
| Strings / escapes | `test_string_escape`, `test_escapes`, `test_bad_unterminated_str`, `test_string_full` | all escape sequences incl. `\0`/`\x..`/octal forms if any, unknown escapes, backslash-at-EOF, raw NUL in output, unterminated char literal, multi-line string |
| Comments | many inline `//` comments; `test_bad_unterminated_str` | unterminated `/* … */` (if supported), nested comment markers, comment at EOF, comment inside string |
| Preprocessor | `test_preprocessor_macros`, `test_preprocessor_if`, `test_include_basic`, `test_bad_include_missing`, `test_bad_include_error_context`, `test_include_error`, `#pragma once` via cyanlib | include cycles, self-include, macro self-recursion and mutual recursion, macro producing a directive, empty include, very deep include chain, `#if` with undefined names/divide-by-zero, unbalanced `#endif`, huge macro expansion |
| Parser / diagnostics | `test_error_recovery.sh` (3 errors), `test_semantic_recovery.sh` (3 errors), `test_bad_*` across `tests/types` | EOF after every token kind and inside every construct, deep left/right nesting (1000+), precedence/associativity chains for every operator family, cascade-free recovery where one error does not produce a second diagnostic, error limit `-ferror-limit` |
| Integer arithmetic | `tests/oracle/{arith_*,shift_*,div*}`, `test_signed_unsigned`, `test_type_conv`, `test_inc_dec_edge` | shift counts 0/1/31/32/63/64/65/negative and variable counts, `MIN / -1`, `MIN % -1`, modulo sign matrix, negation of MIN, `abs(MIN)`, mixed-width compare matrix |
| Casts / width/sign | `test_cast`, `test_cast_widen`, `test_type_conv`, `test_bad_cast_type` | full i8/u8/i16/u16/i32/u32/i64/u64 × sign extension/truncation matrix, chained casts, cast of pointer to all integer widths and back |
| Floats | `test_float_print` (denormals, DBL_MAX), `test_float_compare`, `test_float_convert`, `tests/oracle/float_*` | NaN/inf produced by arithmetic and compared, `-0.0` sign bit through arithmetic, f32/f64 overflow and underflow on conversion, int↔float at i64 MIN/MAX and u64 MAX, `F2I` out-of-range, float as shift/cast operand |
| Pointers / aggregates | `test_ptr_arith`, `test_struct_ptr_arith`, `test_nested_struct`, `test_struct_param*`, `test_type_alias`, `test_enum_*`, `test_fnptr_*` | pointer arithmetic at MAX/MIN and on `T**`, `sizeof` of every aggregate (see BF-001), enum auto-increment past boundary/duplicate values, fn-pointer tables with mixed signatures, null fn pointer call |
| Codegen / regalloc | `test_spill`, `test_spill_pressure`, `test_call_pressure`, `test_many_args_stack`, `test_func_stress`, `bench_regalloc.sh` | 0..25 args with every position live, mixed int/float arg layouts (9+ FP args), 100+ deep expression trees, recursion until stack exhaustion, huge frames with `--stack-probe` just below/above the limit |
| Runtime checks | `test_bounds_*`, `test_null_*`, `test_stack_probe_*`, `test_read_eof`, `test_argc`, `test_argv_basic` | index == size / -1 / MIN, null check on write to offset 0 vs >0, stack probe exactly at the boundary, `print` of empty/NUL/very long strings, `read` of empty input / all input types, `argc`/`argv` with 0 and many args |
| CLI / misc | `tests/cli/test_cli.py`, `test_paths.sh`, `test_color.sh`, `fuzz_smoke.sh` | deterministic random-program differential testing against gcc (T-7) |

## Diagnostics inventory

Diagnostics observed in the tokenizer/parser sources include `Unexpected end of input`,
`Unterminated string literal`, `Expected expression`, `Expected expression after operator`,
`Expected expression after shift operator`, `Expected a type in function pointer parameter list`,
`Expected array dimension size`, `Expected field name in struct`, `Expected enumerator name`,
`Not enough arguments for print format string`, `Invalid expression in …`, `Invalid statement …`.
T-2/T-3 add assertions for the substrings of the malformed-input cases that currently have no test.

## Bug found during inventory

* **BF-001** — `sizeof` of a struct type is 8 instead of 16 and of a struct variable is 128
  instead of 16. Reproduced by hand on both targets; details in `docs/BUGS-FOUND.md`, xfail test
  in `tests/xfail/`. This is a compiler bug and is deliberately *not* fixed in Phase T.

## Test-placement rules for this phase

* A passing edge case goes under `tests/<area>/` and is picked up by `scripts/run_tests.sh`.
* A failing edge case caused by a compiler bug goes under `tests/xfail/` with the output it
  *should* produce. `scripts/run_tests.sh` excludes that directory so `check.sh` stays green.
* `scripts/xfail.sh` runs those tests separately and reports which still fail.

## Known target-specific behaviour (not filed as bugs)

* `(i64::MAX as f64) as i64` returns `i64::MIN` on x86_64 and `i64::MAX` on riscv64. D-038/D-039
  explicitly say out-of-range float→int results follow the target instruction and are not trapped,
  so the passing float test asserts only target-independent special values.
* Shift counts >= 64 use the hardware's masked count on both targets (C leaves this undefined).

## Known limit: stack probe checks entry SP only

`--stack-probe` compares SP at function entry against `initial SP - 1 MiB`, before the current
frame is allocated. Verified by hand: a single 20 MiB frame in `main` segfaults instead of
printing `stack overflow`, and compile time for such a frame is ~30 s (too slow for a test).
Recursive frames are detected correctly (existing `tests/checks/test_stack_probe_fail.cyan`).
This follows the documented "at function entry" design and is recorded here rather than filed as
a bug.

## Known limit: null checks test the final address only

`--check-null` compares the address actually dereferenced against zero. A null base plus a
nonzero offset (`*(p + 1)` where `p == 0`, or `p->b` with a nonzero field offset) produces a
nonzero address that passes the check and then segfaults. D-035 specifies a check on pointer
reads/writes but does not require base-pointer tracking, so this is recorded as a limit rather
than a bug. Direct `*p` reads/writes and offset-zero field writes are checked correctly.

## T-7 differential generator scope

`scripts/random_diff.py` generates integer-only expressions shared by C and Cyan (literals,
+ - * / %, bitwise ops, shifts 0..8, comparisons, ternary, unary ops). It deliberately excludes
constructs with known bugs (BF-001..BF-012) and implementation-defined C behaviour, so a
mismatch always indicates a new finding. Mismatches are written to `tests/xfail/diff_*.cyan`
with the gcc output as EXPECT-STDOUT/EXPECT-EXIT and are picked up by `scripts/xfail.sh`.
`check.sh` runs 8 fixed-seed programs per commit; larger sweeps are run manually with
`--count`/`--seed`/`--opt`.
