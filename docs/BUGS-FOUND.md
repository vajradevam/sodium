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
