# Blocked items

(none yet) Format: `- [TASK-ID] date — what blocked, what was tried, options, what you need.`

- [V2b] RESOLVED 2026-11-22 — see PROGRESS.md.
- [Y8b] 2026-11-22 — Struct parameters/returns by value need an ABI decision that is expensive to reverse and is not covered by docs/DECISIONS.md. Options: (a) flatten small structs into consecutive integer argument registers at call and callee, which changes argument ordinals and interacts with existing register-argument/spill logic on both x86_64 and riscv64; (b) pass/return via a hidden pointer to caller/callee storage, which changes the call signature and IR CALL/RET handling. Local struct assignment (Y8a) works. Need an explicit DECISIONS entry choosing the ABI and a dedicated phase-sized change touching both backends before resuming.
- [Y10] 2026-11-22 — Function pointers/indirect calls need an IR+backend ABI decision not covered by docs/DECISIONS.md (which only fixes the `fn(i64,i64)->i64` surface syntax). IR CALL currently carries a symbol name and Backend::call(const std::string&) emits a direct call; indirect calls need either a new call-through-register opcode threaded through the rewriter and both backends, or a thunk/trampoline scheme. Options are expensive to reverse. Need an explicit DECISIONS entry choosing the lowering before resuming.
