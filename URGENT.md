# Urgent Fixes — Type Inference & Parameter Types

## Priority Order (top = fix first)

### ~~P0 — Fix mixed parameter element types in library functions~~ ✅ DONE

**Change**: `malloc` now returns `Ptr(u8)` instead of `Ptr(i64)`.
- All `*ptr = val` through malloc'd pointers use `store_8` (1 byte)
- All pointer arithmetic scales by 1 byte (sizeof(u8))
- All loads through malloc'd pointers use `load_u8`
- Library functions work correctly with both string literals and malloc'd buffers

**Tests updated**:
- `test_allocator_stress.cyan`: All values changed to <256 to fit in byte
- `test_ptr_arith.cyan`: Comment updated (sizeof(u8)=1)
- `test_string.cyan`: Now tests malloc+buffer combinations (strcpy, strcat, memcpy,
  memset, memcmp all tested with malloc'd dst + string literal src)

---

### ~~P0 — Library functions broken with malloc'd buffers~~ ✅ DONE

Fixed by the `malloc` → `Ptr(u8)` change above. All library functions now
use consistent byte-level pointer arithmetic and stores when called with
any combination of `Ptr(u8)` arguments.

---

### ~~P1 — Two-pass inference is fragile and incomplete~~ ✅ DONE

**Fixed**: Replaced two hard-coded passes with iterative fixed-point loop
(up to 10 iterations, exits early when stable). Handles arbitrary call depth.

---

### ~~P1 — Merge heuristic is arbitrary~~ ✅ DONE

**Fixed**: Simplified merge logic: first call site with a Ptr type wins.
Once a parameter is marked Ptr, it stays Ptr. i64 → Ptr upgrade is allowed
(first time a Ptr is seen). Ptr → Ptr changes are ignored (deterministic).
This avoids conflicts between `&i64_var` (Ptr(i64)) and string literals (Ptr(u8)).

---

### ~~P2 — Binary operators in infer_expr_type don't process children~~ ✅ DONE

All 16 binary operator handlers (`Add`, `Sub`, `Multi`, `Div`, `Mod`, `LT`, `GT`,
`LTE`, `GTE`, `EQ`, `NEQ`, `And`, `Or`, `BitAnd`, `BitOr`, `Xor`, `Shl`, `Shr`)
now call `infer_expr_type` on their child expressions.

### ~~P2 — Condition expressions not walked in stmt inference~~ ✅ DONE

- `if`: condition is inferred (was missing initially)
- `while`: condition is inferred (was missing)
- `do-while`: condition is inferred (was missing)
- `for`: condition and increment (`update->expr`) are inferred (was missing)
- `switch`: switch expression is inferred (was missing)
- `ternary`: condition, then-branch, else-branch all inferred

---

### P3 — Type-aware stores are incomplete (documented limitation)

**Problem**: `*ptr = val` now uses `store_8/16/32` based on element type, but:
- `NodeStmtArrAssign` (arr[i] = val) still uses 8-byte store unconditionally
- `NodeStmtFieldAssign` (struct.field = val) still uses 8-byte store

**Why not fixed**: In the current language design, stack arrays (`var arr[n]`)
are always `i64` elements — the `Var.type` field defaults to `i64` for arrays.
Struct fields are accessed at qword-aligned offsets. So 8-byte stores are
correct for all current use cases. Heap-allocated byte buffers use pointer
dereference (`*ptr = val`), which already uses `typed_store`.

**Long-term fix**: Add element-type tracking to `Var` (e.g., store the element
type in `Var.type.pointee`), then use `typed_store` in all codegen paths.

---

### ~~P3 — Default include path is fragile~~ ✅ DONE

**Fixed**:
- Added `--cyanlib-dir <path>` command-line flag
- Added `SODIUM_CYANLIB_DIR` environment variable
- Both override the auto-detection logic
- Priority: `--cyanlib-dir` flag > `SODIUM_CYANLIB_DIR` env > auto-detect

---

### ~~P3 — Test suite avoids the bugs~~ ✅ DONE

**Fixed**: `tests/unit/test_string.cyan` now tests:
- `strcpy`, `strcat`, `memcpy`, `memset`, `memcmp` with malloc'd buffers
- Mixed `Ptr(u8)` from both string literals and `malloc`
- Byte-level verification after `memset`
- 16 test cases total

---

### ~~P4 — Two inference passes are copy-pasted~~ ✅ DONE

**Fixed**: Replaced two hard-coded passes with iterative fixed-point loop
(up to 10 iterations, exits early on stable types). Single body-walk logic.

---

### ~~P4 — No error reporting for mismatched types~~ ✅ DONE

**Fixed**: Added a warning when the same parameter gets conflicting inferred
pointer types (e.g., `Ptr(i64)` vs `Ptr(u8)`). Fires once on the first
iteration, shows source location, callee name, parameter index, and both
types. Example:
```
warning: /tmp/test.cyan:10:5: parameter 1 of 'foo' has conflicting
  inferred pointer types Ptr(i64) vs Ptr(u8); using Ptr(i64)
```

---

## Summary of the real fix (long-term)

The fundamental issue is **lack of explicit parameter type annotations**.
Cyan needs:

```cyan
function strlen(s: u8*) -> i64 {
    var n = 0;
    while (*(s + n) != 0) { n = n + 1; }
    return(n);
}
```

With typed parameters, no inference is needed — the compiler knows `s` is
`Ptr(u8)` from the declaration. All the hacks above become unnecessary.

Until then, the heuristics will have gaps.
