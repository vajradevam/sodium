# AGENTS.md — Autonomous development guide for Sodium / Cyan

Sodium is a compiler (C++20) for **Cyan**, a small C-like systems language. It targets
x86_64 (NASM) and riscv64 (GAS) on Linux. The long-term goal is a self-hosted compiler
(PLAN.md, Phase 9). You are expected to work **autonomously for long stretches**: pick the next
task, do it, prove it, commit, repeat. Do not ask the human for routine decisions — the
defaults are in `docs/DECISIONS.md`. Ask only under the "Stop conditions" below.

## Sources of truth (in order)
1. The **source code and the test results**. Markdown can be stale. The old TODO/BUGS/audit
   files were deleted because they contradicted the code; `README.md` is also unverified
   until task D1 rewrites it. Never act on a claim in prose without reproducing it.
2. `PLAN.md` — the ordered checklist. The only task list.
3. `docs/DECISIONS.md` — language/design decisions already made. Append, do not relitigate.
4. `docs/PROGRESS.md` — append-only log of what you did. `docs/BLOCKED.md` — things you cannot do.

## Architecture (verify, don't assume)
`src/tokenization.*` → `src/preprocessor.*` → `src/parser.*` (AST) → `src/gen/*` (AST → IR via
`src/ir/builder.hpp`) → `src/ir/{liveness,linear_scan,rewriter}.hpp` (regalloc) →
`src/ir/emitter.hpp` → `src/backend/{x86_64,riscv64}/backend.cpp` → assembler/linker.
Runtime: `sodium-rt/` (crt0, print/read/exit asm per arch, `allocator.c`). Library in Cyan:
`cyanlib/`. LSP: `src/lsp*.cpp`. Tests: `tests/{unit,integration,include}` run by
`scripts/run_tests.sh`.

## Tools available
`nasm`, `ld`, `qemu-riscv64`, `riscv64-elf-*`/`riscv64-linux-gnu-as`, cmake, gcc/clang.
Use `$TMPDIR`/`mktemp -d` for scratch output, never write binaries into the repo.

## THE LOOP (follow exactly)
1. `git status` must be clean and `scripts/check.sh` must pass **before** you start. If not,
   fixing that is your task.
2. Take the **first unchecked, unblocked task** in `PLAN.md` (top to bottom). Do not skip
   ahead unless the task is in `docs/BLOCKED.md`.
3. **Reproduce / specify first.** For a bug: write a failing test that demonstrates it. For a
   feature: write the tests (positive, negative/compile-error, edge cases, both targets) that
   define "done" *before* the implementation. Confirm they fail for the right reason.
4. Implement the smallest change that makes them pass. One task = one focused change. If a
   task is bigger than ~400 changed lines, split it into sub-tasks in PLAN.md first and commit
   that split.
5. Run `scripts/check.sh`. Fix until green. Run `scripts/check.sh --full` (sanitizers) when
   the change touches parser, tokenizer, memory/arena, IR, or allocator — and at every phase end.
6. Self-review `git diff` (see "Review checklist"). Update docs touched by the change.
7. **Commit** (see "Commit protocol"), tick the box in `PLAN.md`, append a line to
   `docs/PROGRESS.md` (date, task ID, what, test count before→after). Commit that with the
   task commit (same commit).
8. Go to 1. Do not stop after one task. Keep going until the phase ends or a stop condition is
   hit; at a phase end, follow "Phase boundaries" below.

## Phase boundaries (overrides THE LOOP step 8 at a phase end)

Work autonomously *within* a phase, and stop at the end of it.

1. Work through the unchecked tasks of the CURRENT phase in PLAN.md, top to bottom, using
   THE LOOP. Do not ask the human for routine decisions inside a phase.
2. When the last task of the phase is done, run the phase-end checkpoint: `scripts/check.sh --full`,
   the regression sweep, a "state of the project" entry in PROGRESS.md re-verified against
   the code, and `git tag phase-N-done`.
3. Then STOP. Do not start any task from the next phase. Do not push unless the human asked.
4. Your final message for the phase must contain: tasks completed with commit hashes; test
   counts before and after (integration / unit / ABI / sweep / fuzz); anything blocked or
   deferred and why; known limits that still exist; the first unchecked task of the NEXT phase
   with your recommended plan. Then wait for the human to say "go" for the next phase.

The other stop conditions below still apply mid-phase.

## Checkpoints (hard gates)
- **Per commit:** `scripts/check.sh` = build with zero new warnings + all tests pass on
  x86_64 *and* riscv64 (the gate fails if riscv64 tests are skipped). Test count never decreases.
- **Per phase end** (last box of each phase in PLAN.md): `scripts/check.sh --full`, a
  `git tag phase-N-done`, a regression sweep (compile every file in `tests/` with `--emit-ir`
  and `-S` on both targets, none may crash), and a short "state of the project" entry in
  PROGRESS.md re-verified against the code.
- **Never** make a gate pass by: deleting/skipping/weakening a test, loosening an expected
  value to match wrong output, adding `|| true`, or disabling a target. If a test is truly
  wrong, fix it in its own commit with the reason in the message.

## Commit protocol
- Commit after **every** completed task and at least every ~60–90 minutes of work if a task
  is large (use `wip:` only on a feature branch, never on `main`).
- Work on `main` directly for small tasks; use branch `phase-N/<slug>` for risky refactors and
  merge with `--no-ff` only after `check.sh` passes on the branch.
- Message format: `<Area>: <imperative summary> (<TASK-ID>)` — body explains *why* and lists
  tests added. Example: `Parser: report multiple errors via synchronization (E1)`.
- Each commit must build and pass the gate on its own (bisectable). Never commit binaries,
  `build*/`, or temp files. Never `--no-verify`, never force-push, never rewrite pushed history.
- Do NOT add `Co-Authored-By` or any other attribution trailer to commit messages (owner's explicit instruction).
- Do not push unless the human asked; local commits and tags only.

## Testing rules
- Every language feature needs: ≥1 positive run test (exit code **and** stdout checked once
  task T0 lands), ≥1 compile-error test per new diagnostic, edge cases (zero, max/min values,
  empty, nesting), and must run on **both** targets.
- Every bug fix ships with a regression test that fails without the fix.
- Compile-error tests assert the diagnostic text (substring), not merely "failed to compile".
- Tests must be deterministic and fast (<2 s each). No reliance on host state.
- When adding C++ internals that tests can't reach through programs (tokenizer, parser,
  allocator, regalloc), add unit tests under `tests/cpp/` (task T4).

## Review checklist (before every commit)
- Did I read the code I changed, rather than guessing its behavior?
- Any `exit()`/global state/UB/unchecked `stoll`/`system()` with unescaped input added?
- Both backends updated? Calling convention, sign/zero extension, large immediates, alignment?
- Error messages have `file:line:col`, say what was expected, and don't cascade?
- Docs (`README.md` language reference once it exists, `docs/DECISIONS.md`) match behavior?
- No dead code, commented-out blocks, or stray debug prints. No unrelated reformatting.

## Stop conditions (the only reasons to stop or ask the human)
- A decision is needed that is **not** covered by `docs/DECISIONS.md` and is expensive to
  reverse (e.g. changing existing syntax in a breaking way). Record the options in
  BLOCKED.md, skip to the next unblocked task, and only stop if none remain.
- A required tool is missing and cannot be worked around.
- The gate was red at the start and you cannot fix it in a reasonable effort — revert to the
  last green commit and document why.
- You've been stuck on one task for more than ~3 distinct attempts: log what you learned in
  BLOCKED.md, revert uncommitted partial work, move on.
- All PLAN.md boxes are ticked: write the final status in PROGRESS.md and stop.

When you do stop, your last message states: tasks completed, tests before→after, anything
blocked, and what you'd do next. Do not claim anything you did not run.

## Defaults for ambiguity
Prefer: backward-compatible over breaking; explicit over inferred; a compile error over silent
wrong code; simple and readable over clever; matching C semantics (LP64) unless DECISIONS.md says otherwise.
