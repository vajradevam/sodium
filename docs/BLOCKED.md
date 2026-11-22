# Blocked items

(none yet) Format: `- [TASK-ID] date — what blocked, what was tried, options, what you need.`

- [V2b] RESOLVED 2026-11-22 — see PROGRESS.md.

- [Y8b] RESOLVED 2026-11-22 — ABI decided in D-012; see PLAN.md Y8b1..Y8b4.

- [Y10] RESOLVED 2026-11-22 — lowering chosen in D-014 (CALL_INDIRECT opcode).

- [X1] 2026-11-23 — Floats (`f32`/`f64`) need more than one phase. Verified against the code: `Type::Kind` has no Float case (`src/parser.hpp:24-34`), `IRValue` holds only vreg/int64 immediate (`src/ir/value.hpp:18-43`), no float opcodes exist (`src/ir/opcodes.hpp`), `TargetRegisterInfo` has a single integer register class with no XMM/f registers (`src/ir/target_regs.hpp`), neither backend emits SSE/F-extension instructions, and `print` has only integer runtime helpers. A proper implementation touches ~40 files and is ~900-1600 implementation lines before tests, with at least four interdependent pieces: (a) lexer/type/literal/cast front end, (b) IR float values/opcodes/passes/verifier, (c) FP register classes + System V/LP64D call ABI on both targets, (d) freestanding `%f` formatting runtime. Options requiring a decision: full C-compatible FP ABI (D-001/D-012 promise System V / LP64D; this is the correct long-term choice but needs real FP register classes and separate argument/return paths) versus a GPR bit-cast shortcut (keeps floats in integer vregs with `movq`/`fmv` at use sites; ~600-1100 lines but violates the documented C ABI and would break `tests/abi/`). Need: an explicit ABI decision in DECISIONS.md and a dedicated phase (4 sub-tasks) before starting. Skipping to X2 per PLAN.md.
