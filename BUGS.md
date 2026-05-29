# Sodium / Cyan Compiler — Bug & Issue Tracker

## Fixed

---

### #1 — `const` members prevent move semantics (Critical, Perf)

**Files:** `src/tokenization.hpp`

The Tokenizer constructor took `const std::string src` then moved it into
`const std::string m_src`. Because the parameter was `const`, `std::move(src)`
produced `const std::string&&`, which cannot bind to the move constructor —
it silently fell back to a copy. The entire source text was copied instead of
moved.

**Fix:** Removed `const` from both the constructor parameter and the member
in `tokenization.hpp`, enabling the intended move semantics.

**Status:** ✅ Fixed in commit `5c6d9c4`

---

### #2 — `system()` calls without error checking (Critical, Reliability)

**File:** `src/main.cpp`

```cpp
system("nasm -felf64 ./out.asm");
system("ld -o out ./out.o");
```

If nasm or ld failed (syntax error, missing binary), the compiler exited 0
(success) and left a stale/broken `out` binary. No error message was shown.

**Fix:** Check `system()` return codes. If non-zero, print an error and exit
with `EXIT_FAILURE`.

**Status:** ✅ Fixed in commit `3fd1dc9`

---

### #4 — For-loop update doesn't accept compound assignment operators (Bug)

**File:** `src/parser.hpp` (`parse_for_stmt`)

The for-update parser only matched `ident = expr` (plain `TokenType::eq`),
silently rejecting `+=`, `-=`, `*=`, etc. with the error
`"Invalid for update (must be assignment)"`.

Additionally, the generator duplicated a simplified store that always did
plain assignment regardless of the operator.

**Fix:** The parser now matches all assignment operators and sets the
appropriate `AssignOp` on the update `NodeStmtAssign`. The generator
delegates to the full `NodeStmtAssign` handler (which correctly handles
compound operations: push current value, evaluate RHS, apply op, store).

**Status:** ✅ Fixed in commit `3905a85`

---

### #5 — For-loop update with global variables crashes (Bug)

**File:** `src/generation.hpp`

The for-update generator used `lookup_var()` which only searches local
scopes. If the update variable was a global, `lookup_var()` would print
"Undeclared identifier" and exit.

**Fix:** Delegating the for-update to the full `NodeStmtAssign` handler
naturally resolves this — that handler checks local scopes first, then
falls back to `m_globals`.

**Status:** ✅ Fixed (implicitly by #4) in commit `45b816e`

---

### #6 — Static variable initializers inside functions are silently lost (Bug)

**File:** `src/generation.hpp`

`gen_prog()` emits the global-init loop at the top of `_start`, **before**
`gen_func_def()` processes function bodies. A `static var x = 0;` inside a
function was added to `m_data_entries`/`m_global_inits` during
`gen_func_def()`, after the init loop had already run. The allocation and
initialization were silently lost, leaving the variable zero-initialized.

**Fix:** Added a pre-collection pass (`collect_globals`) that walks the
entire AST — top-level statements, all function bodies, and all nested
blocks (`if`/`else`, `while`, `do-while`, `for`, `switch`) — before any
code generation. This populates `m_globals`, `m_data_entries`,
`m_bss_entries`, and `m_global_inits` correctly. The `NodeStmtGlobal`
codegen visitor is simplified to just register in `m_globals`.

**Known limitation:** Non-constant initializers inside functions
(e.g. `static var x = param;`) cannot be evaluated at program start
because the expression references function parameters. These are
allocated zero-initialized but the init expression is not emitted
(the feature requires a guard-flag mechanism in the function prologue).

**Status:** ✅ Fixed in commit `c87989e`

---

### #7 — Dead exit syscall after top-level `return` (Minor)

**File:** `src/generation.hpp`

When `return(val)` is used at the top level, the `NodeStmtReturn` handler
emits `mov rax, 60; pop rdi; syscall`. After all top-level statements,
`gen_prog()` unconditionally emitted another `mov rax, 60; mov rdi, 0;
syscall` — dead code.

**Fix:** Added `bool m_emitted_exit` flag. The `NodeStmtReturn` handler
sets it. `gen_prog()` skips the final exit when the flag is true.

**Status:** ✅ Fixed in commit `0222519`

---

### #10 — Profane / unprofessional error messages (Design, UX)

**Throughout the codebase:**

- `"Holy shit. Yeh kya mazak hai?"` (wrong usage)
- `"Bruh moment!"` (tokenizer: unexpected `!` / unknown character)
- `"Invalidddd"` (parser: invalid statement/block)
- `"Just looking like a wow... maa chuda madarchod"` (parser: invalid expression in `let`)

**Fix:** Replaced with clear, professional descriptions indicating what
went wrong and, where possible, what was expected.

**Status:** ✅ Fixed in commit `f7a05d5`

---

### #13 — String literals placed in writable `.data` section (Security)

**File:** `src/generation.hpp`

Strings were placed in `section .data` (writable) whenever any global/
static data or BSS entries existed. Only when there were no data/BSS
entries at all did they go in `section .rodata` (read-only).

Additionally, string labels were local (`.strN`), making them scoped to
the preceding non-local label. When a function was defined before the
string section, the reference from `_start` (`_start.strN`) no longer
matched the definition (`function.strN`).

**Fix:** Strings always go in `section .rodata`. String labels use the
non-local form `strN` (no leading dot) so they are globally resolvable
regardless of intervening function definitions.

**Status:** ✅ Fixed in commit `5eeae7e`

---

### #14 — Duplicated function epilogue code (Code size)

**File:** `src/generation.hpp`

Every `return` statement inside a function emitted the full epilogue
(`mov rsp, rbp; pop rbp; ret`). Then the function end emitted the same
epilogue again. If all paths returned, the epilogue at the end was
dead code.

**Fix:** `return` statements in functions now emit `jmp .Lfunc_epilogue`,
jumping to a single shared epilogue label at the function end.

**Status:** ✅ Fixed in commit `1a53d68`

---

## Remaining

---

### #8 — `continue` inside `switch` inside a loop — fragile context search (Minor)

**File:** `src/generation.hpp`, `NodeStmtContinue` visitor (line ~942)

**Issue:**

The `continue` handler scans `m_loop_stack` from the top, skipping
entries with an empty `continue_label`. The switch statement pushes a
loop-context entry with `begin_label=""` and `continue_label=""`, relying
on the scanner to skip it and find the enclosing loop.

```cpp
// switch pushes:
gen->m_loop_stack.push_back({ .begin_label = "", .end_label = label_end, .continue_label = "" });

// continue scans:
for (auto it = gen->m_loop_stack.rbegin(); it != gen->m_loop_stack.rend(); ++it) {
    if (!it->continue_label.empty()) {
        gen->m_output << "    jmp " << it->continue_label << "\n";
        return;
    }
}
```

**Problems:**

1. **No explicit switch/break context** — the switch reuses the loop-stack
   mechanism with empty labels as sentinels. This is fragile: any code
   path that pushes a loop-context entry with empty labels for another
   reason would be silently skipped.

2. **`continue` outside a loop but inside a switch** — if a `continue`
   appears inside a switch that is NOT inside a loop, the scanner would
   skip the switch entry and then find some earlier stale entry (or crash
   if the stack is empty). The compiler currently prints "continue outside
   loop" only when `m_loop_stack` is empty — it doesn't distinguish
   "inside switch but not inside loop" from "inside a loop".

3. **`break` inside switch** — the `break` handler always jumps to
   `m_loop_stack.back().end_label`. For switch, this is the switch's
   end label (correct). But for a loop, it's the loop's end label. If
   `break` appears directly inside a loop (not inside a switch), it also
   uses `end_label` — which is correct. The ambiguity arises when a
   `break` appears inside a switch inside a loop: it should exit the
   switch, not the loop. Currently it exits the switch (because the
   switch is the top of the stack), which happens to be correct.

**Severity:** Minor. All existing tests pass. The implementation works
for the common case (switch inside a loop, `continue` targets the loop,
`break` targets the switch). But the design is fragile and would break
with any non-trivial nesting change.

**Suggested fix:**

Introduce a dedicated `break` context (separate from the loop stack)
so that `switch` does not abuse the loop stack for its own `break`.
The `continue` handler would then only search the loop stack (no
empty-label sentinels), and the `break` handler would search the
break-context stack for switch boundaries.

---

### #9 — Array assignment without literal falls through to scalar code (Design)

**File:** `src/generation.hpp`, `NodeStmtAssign` visitor (line ~514)

**Issue:**

When assigning to an array variable with an array literal:

```cyan
var arr = [1, 2, 3];
arr = [40, 50, 60];  // OK — size match checked
```

The generator checks `if (auto arr_lit = std::get_if<NodeExprArrLit*>(&stmt_assign->expr->var))`
and handles the per-element copy. If the RHS is NOT an array literal
(e.g., `arr = someVar`), it falls through to the scalar assignment code
which reads/writes `arr[0]`'s stack slot only — silently corrupting the
array.

```cyan
var arr[3];
var x = 42;
arr = x;   // ⚠️  writes 42 only to arr[0], arr[1..2] unchanged
```

This is a type mismatch: assigning a scalar to an array should be a
compile-time error, not silently produce wrong behaviour.

**Severity:** Design issue. Low priority because array-to-array
assignment with non-literal is unlikely in typical code, but the
silent misbehaviour could be confusing.

**Suggested fix:**

When `var.array_size > 0` and the expression is NOT an array literal,
emit an error: "Cannot assign scalar to array variable". Alternatively,
support element-wise copy from another array variable of the same size.

---

### #11 — No source locations in error messages (Design, UX)

**Throughout the codebase**

**Issue:**

Every error message prints only a description of what went wrong, never
*where* it happened. No file name, line number, or column is reported.
This makes debugging any non-trivial program tedious — the user must
search the source for the likely location of the error.

This is listed in `FEATURES.md` as a roadmap item.

**Severity:** Design / UX. High priority for usability but requires
threading token positions through the entire pipeline.

**Scope of work:**

1. **Tokenizer:** Record line/column for each token (add fields to `Token`).
2. **Parser:** Thread source locations into AST nodes, or at minimum
   report the token's position when an error is emitted.
3. **Generator:** When an error originates during codegen (e.g.,
   "Undeclared identifier"), report the source location if available
   (the AST node may carry a token reference).

Example desired output:
```
error: tests/test.cyan:12:5: Undeclared identifier: foo
```

---

### #12 — Fixed 4 MB arena allocator, no fallback (Robustness)

**File:** `src/parser.hpp`, line ~276

```cpp
m_allocator(1024 * 1024 * 4)  // 4 MB fixed
```

**Issue:**

The parser uses a fixed-size arena allocator. If a source file or the
AST for a complex program exceeds 4 MB, the allocator throws
`std::bad_alloc` (or crashes on the `new` that allocates the arena).

There is no fallback, no resizing, and no incremental allocation from
the heap. The arena is a single contiguous block allocated up front.

**Severity:** Design / Robustness. Low priority for typical programs
but a hard limit that will surprise users with large codebases.

**Suggested fixes (in increasing order of complexity):**

1. **Increase the fixed size** — 4 MB is generous for Cyan programs,
   but bumping to 16 MB or 64 MB kicks the can down the road.

2. **Make the arena growable** — when the current block is exhausted,
   allocate a new larger block and chain it. Allocation pointers into
   the arena are never invalidated (no compaction), so existing AST
   pointers remain valid.

3. **Replace with `std::make_unique` / heap allocation** — bypass the
   arena entirely and allocate each AST node individually. Simpler but
   loses the cache-locality and single-deallocation benefits of an arena.

---

## Investigated — Not a Bug

### #3 — Array index access with intermediate stack values

**File:** `src/generation.hpp`, `NodeExprIndex`

**Claim:** The base offset formula `(current_m_stack_size - stack_loc - 1) * 8`
doesn't account for temporary values pushed onto the stack between the
array declaration and the access expression, causing `arr[1]` to read
`arr[0]` when used in compound expressions like `arr[0] + arr[1]`.

**Analysis:** This is **not a bug**. The formula is mathematically correct
even with intermediate values on the stack.

For array element at logical position `V + i` (where `V` = recorded
`stack_loc` at declaration time):
- Actual offset from RSP = `(current_M - (V + i) - 1) * 8`
- = `(current_M - V - i - 1) * 8`
- = `(current_M - V - 1) * 8 - i * 8`
- = `base_offset - i * 8`

Since `current_m_stack_size` includes all intermediate values, the
base address of the array (element 0) shifts correctly, and subtracting
`i * 8` reaches the correct element. This holds for any number of
intermediate pushes.

**Verification:** A test with `arr[3] = [10, 20, 30]` and
`arr[0] + arr[1] + arr[2]` returned 60 (correct). Generated assembly
showed correct offsets.
