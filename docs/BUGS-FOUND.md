# Bugs found during Phase T (append-only)

Findings are reported, never fixed in this phase. Each entry has a minimal reproducer in
`tests/xfail/`, run by `scripts/xfail.sh` (not part of `check.sh`). Severity scale:
**critical** = wrong code silently, **high** = common construct broken, **medium** = narrow
edge case, **low** = cosmetic/diagnostic quality.

## BF-001 — `sizeof(struct)` returns the wrong size

* **Severity:** high
* **Targets affected:** x86_64 and riscv64 (identical wrong values)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af` (Phase T branch HEAD
  `256d389`, no compiler changes)
* **Minimal reproducer:** `tests/xfail/bf001_sizeof_struct.cyan`
  ```
  struct P { var a: i64; var b: i64; };
  function main() {
      print(sizeof(P));
      var s: P;
      print(sizeof(s));
      var t: P = { .a = 1, .b = 2 };
      print(sizeof(t));
      return(0);
  }
  ```
* **Expected (C reference, `sizeof(struct P)` with two `long` fields):** `16`, `16`, `16`
* **Actual:** `8`, `128`, `128`
  * `sizeof(P)` reports 8 — looks like the size of the *first field* or of one pointer word,
    not the sum of both i64 fields.
  * `sizeof(s)` and `sizeof(t)` report 128 — looks like an unrelated value (e.g. a frame
    offset or a byte-size calculation gone wrong), not the type size.
* **Notes:** reproduced by hand with `./build/sodium -O0` on both targets; also present at
  `-O1`. `sizeof` of scalar types (`i8` → 1, `i32` → 4, `u8*` → 8) is correct, as is
  `sizeof(S)` for `struct S { var a: i8; var b: i32; }` (8, which happens to match C).
  The bug is specific to struct types/variables whose true size differs from these values.
* **xfail test path:** `tests/xfail/bf001_sizeof_struct.cyan`

## BF-002 — Hex integer literal wider than 64 bits wraps to a small value

* **Severity:** critical (silently compiles wrong code)
* **Targets affected:** x86_64 and riscv64 (the tokenizer is shared)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf002_hex_literal_overflow.cyan`
  ```
  function main() {
      print(0x10000000000000000);
      return(0);
  }
  ```
* **Expected:** a compile error containing `out of range for 64-bit signed integer`, the same
  diagnostic the equivalent decimal literal produces.
* **Actual:** compiles successfully and prints `0`. The hex accumulator in the tokenizer is a
  `uint64_t` multiplied by 16 per digit with no overflow check; 17 digits wrap to zero, then the
  wrapped value is converted to the decimal string `"0"`. Longer hex strings can wrap to any
  value, so the bug is not limited to powers of two.
* **Notes:** decimal literals of the same magnitude are correctly rejected (`test_bad_literal_overflow`).
* **xfail test path:** `tests/xfail/bf002_hex_literal_overflow.cyan`

## BF-003 — Unterminated block comment is silently accepted

* **Severity:** medium (malformed source is accepted; code after the comment can disappear)
* **Targets affected:** x86_64 and riscv64
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf003_unterminated_block_comment.cyan`
  ```
  function main() {
      return(0);
  }

  /* this block comment is never closed
  ```
* **Expected:** a compile error naming the unterminated comment (the xfail test matches the
  case-insensitive substring `nterminated block comment`).
* **Actual:** the program compiles and exits 0. When the unclosed comment swallows a needed `}`,
  a later `Expected '}'` error at `0:0` appears instead, with no useful source location.
* **Notes:** unterminated *string* literals are correctly diagnosed. The tokenizer's `/*` branch
  consumes to EOF without checking that `*/` was seen.
* **xfail test path:** `tests/xfail/bf003_unterminated_block_comment.cyan`

## BF-004 — `#include` cycle crashes the compiler

* **Severity:** critical (compiler stack overflow / SIGSEGV)
* **Targets affected:** x86_64 and riscv64 (preprocessor is target-independent)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf004_include_cycle.cyan` with
  `tests/xfail/bf004_a.inc` (`#include "bf004_b.inc"`) and `tests/xfail/bf004_b.inc`
  (`#include "bf004_a.inc"`), no `#pragma once`.
  ```
  #include "bf004_a.inc"
  function main() { return(0); }
  ```
* **Expected:** a compile error naming the include cycle.
* **Actual:** the compiler recurses through the two files until the stack overflows; exit status
  139 (SIGSEGV) under a 10 s timeout. Files that use `#pragma once` or include guards are fine
  because the `visited` set is populated for those only.
* **xfail test path:** `tests/xfail/bf004_include_cycle.cyan`

## BF-005 — Empty included file reported as "could not read file"

* **Severity:** low (spurious compile error, misleading wording)
* **Targets affected:** x86_64 and riscv64
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf005_empty_include.cyan` with the zero-byte
  `tests/xfail/bf005_empty.inc`.
  ```
  #include "bf005_empty.inc"
  function main() { print(7); return(0); }
  ```
* **Expected:** compiles and prints `7`.
* **Actual:** `error: could not read file: tests/xfail/bf005_empty.inc`. `read_file` returns an
  empty string both for a missing file and for a legitimately empty file, and the caller treats
  either as failure.
* **xfail test path:** `tests/xfail/bf005_empty_include.cyan`

## BF-006 — Untyped (legacy) functions accept the wrong number of arguments

* **Severity:** high (silently compiles wrong code)
* **Targets affected:** x86_64 and riscv64
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf006_untyped_call_arity.cyan`
  ```
  function f(a, b) { return(a + b); }
  function main() { print(f(1)); return(0); }
  ```
* **Expected:** a compile error containing `wrong number of arguments` (typed functions already
  produce `wrong number of arguments to 'f': expected N, got M`).
* **Actual:** compiles and prints `1` — the missing parameter reads as zero. Calling with too
  many arguments (`f(1, 2, 3)`) also compiles and prints `3`, silently discarding the extra value.
* **Notes:** `Generator::check_call_args` is only called when a function has at least one typed
  parameter (`has_typed_params`), so every purely-untyped function skips arity and type checking.
* **xfail test path:** `tests/xfail/bf006_untyped_call_arity.cyan`

## BF-007 — Deep parenthesis nesting overflows the compiler stack

* **Severity:** medium (compiler crash on valid input; no diagnostic)
* **Targets affected:** x86_64 and riscv64 (parser is shared)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf007_deep_nesting_crash.cyan`, a `print` of `1` wrapped in
  5000 nested parentheses.
* **Expected:** compiles and prints `1` (1600 nested parentheses already do).
* **Actual:** the compiler dies with SIGSEGV (exit 139); 2500 levels compile, 3000 crash. Deep
  nested `if` blocks do not show the problem at the tested depths.
* **Notes:** a nesting-depth limit with a source diagnostic would also be acceptable; a crash is
  not. The passing regression uses 1600 levels (`tests/edge/test_parse_deep_nesting.cyan`).
* **xfail test path:** `tests/xfail/bf007_deep_nesting_crash.cyan`

## BF-008 — Cast to a signed narrow integer zero-extends instead of sign-extending

* **Severity:** high (silently wrong value and comparison results)
* **Targets affected:** x86_64 and riscv64
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf008_signed_cast_zero_extend.cyan`
  ```
  function main() { print("%u", -1 as i8); return(0); }
  ```
* **Expected:** `18446744073709551615` (C: `(uint64_t)(int8_t)-1`).
* **Actual:** `255`. The same happens for `as i16` (65535 instead of all ones) and `as i32`
  (4294967295). A 152-pair i8..u64 cast matrix against gcc found 24 mismatches, all of this form.
* **Notes:** a *typed variable* initialized from the cast (`var x: i8 = -1 as i8;`) reads back
  correctly, because the store/load path re-sign-extends. The bug is in the value produced by the
  cast expression itself, so untyped locals and direct comparisons are wrong.
* **xfail test path:** `tests/xfail/bf008_signed_cast_zero_extend.cyan`

## BF-009 — `INT64_MIN / -1` and `% -1` disagree between targets

* **Severity:** high (silent wrong result on one target)
* **Targets affected:** riscv64 (x86_64 traps as documented)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf009_min_div_negative_one.cyan`
  ```
  function main() {
      var m = -9223372036854775807 - 1;
      print(m / -1);
      return(0);
  }
  ```
* **Expected:** a trap (exit 136 / C signal), which is what x86_64 does and what D-023 states.
* **Actual:** riscv64 prints `-9223372036854775808` and exits 0. For `m % -1`, x86_64 traps while
  riscv64 prints `0` and continues. Reproduces at -O0 and -O1.
* **xfail test path:** `tests/xfail/bf009_min_div_negative_one.cyan`

## BF-010 — `u64` arithmetic and comparisons use signed `i64` semantics

* **Severity:** high (silently wrong code for half the u64 value range)
* **Targets affected:** x86_64 and riscv64 (identical wrong values)
* **Found on compiler commit:** `a1ec4d935f01b58a4a22d259101b7f2b2f7a78af`
* **Minimal reproducer:** `tests/xfail/bf010_u64_signed_semantics.cyan`
  ```
  function main() {
      var u: u64 = 0 as u64 - 1;
      print("%u", u >> 1);
      print("%u", u / 2);
      print(u < 1);
      return(0);
  }
  ```
* **Expected (C, `uint64_t`):** `9223372036854775807`, `9223372036854775807`, `0`.
* **Actual:** `18446744073709551615`, `0`, `1`. All u64 values with the high bit set behave as
  negative i64 in `>>`, `/`, `%` and ordered comparisons; `u8`/`u16`/`u32` are correct.
  Separately, `var u: u64 = 9223372036854775807 as u64 + 1;` is rejected at compile time with
  `Integer overflow in constant expression` even though the u64 result is representable.
* **xfail test path:** `tests/xfail/bf010_u64_signed_semantics.cyan`
