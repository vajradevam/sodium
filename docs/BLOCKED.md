# Blocked items

(none yet) Format: `- [TASK-ID] date — what blocked, what was tried, options, what you need.`

- [V2b] RESOLVED 2026-11-22 — see PROGRESS.md.
- [Y10] 2026-11-22 — Function pointers/indirect calls need an IR+backend ABI decision not covered by docs/DECISIONS.md (which only fixes the `fn(i64,i64)->i64` surface syntax). IR CALL currently carries a symbol name and Backend::call(const std::string&) emits a direct call; indirect calls need either a new call-through-register opcode threaded through the rewriter and both backends, or a thunk/trampoline scheme. Options are expensive to reverse. Need an explicit DECISIONS entry choosing the lowering before resuming.

- [Y8b] RESOLVED 2026-11-22 — ABI decided in D-012; see PLAN.md Y8b1..Y8b4.
