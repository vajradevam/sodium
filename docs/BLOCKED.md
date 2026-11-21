# Blocked items

(none yet) Format: `- [TASK-ID] date — what blocked, what was tried, options, what you need.`

- [V2b] 2026-11-22 — Nested/recursive calls with ~20 live values return wrong results. Observations: a call result held in a caller-save argument register (e.g. riscv a7) is overwritten while the next call's arguments are set up; on x86_64 even `sum2(sum4(...), sum6(...))` yields 32 instead of 31. Tried making call-spanning ranges use callee-save registers only (spilling the rest) in LinearScanAllocator — it did not fix the composition case and regressed `test_many_args.cyan`; reverted. Likely needs argument-register-aware allocation or save/restore of live caller-save registers around each call. V2's other four spill bugs are fixed; V2b remains unchecked.
