# Code style (observed conventions)

`scripts/check_style.sh` mechanically enforces only the two rules that are easy to
regress silently: **no trailing whitespace** and **no tabs in indentation** (tabs
are fine inside a literal or a Makefile). Everything below is a description of the
existing code, not a reformatting mandate — do not mass-reformat unrelated code.

- **Indentation:** 4 spaces, no tabs. Continuation lines add another 4 spaces.
- **Line length:** soft limit around 100 columns; long IR/emitter lines exceed it.
- **Braces:** control flow uses `if (...) { ... } else { ... }` on one line where
  short. Function definitions are mixed (some `{` on the signature line, some on
  the next); follow the surrounding file.
- **Naming:** `snake_case` for functions, locals and file names; `UpperCamelCase`
  for types/classes; private members are prefixed `m_`; globals in the compiler
  are avoided (see the standing chore in PLAN.md).
- **Headers:** `#pragma once`. Declarations in `.hpp`, definitions in `.cpp`.
- **Includes:** project-local first, then standard library; no enforced sort order.
- **Tests:** expectations are `// EXPECT-*` header comments (D-005); test files
  live under `tests/{unit,integration,include}/` and are discovered, never listed.
- **Diagnostics:** use the compiler's error helper with a `SourceLoc`; include
  `file:line:col` (D-004). Never `exit()` from a library/backend header.

When adding a new source file, match the nearest existing file in the same
directory rather than introducing a new convention.
