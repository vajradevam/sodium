# Blocked items

(none yet) Format: `- [TASK-ID] date — what blocked, what was tried, options, what you need.`

- [V2b] RESOLVED 2026-11-22 — see PROGRESS.md.
- [Y8b] 2026-11-22 — Struct parameters/returns by value need an ABI decision that is expensive to reverse and is not covered by docs/DECISIONS.md. Options: (a) flatten small structs into consecutive integer argument registers at call and callee, which changes argument ordinals and interacts with existing register-argument/spill logic on both x86_64 and riscv64; (b) pass/return via a hidden pointer to caller/callee storage, which changes the call signature and IR CALL/RET handling. Local struct assignment (Y8a) works. Need an explicit DECISIONS entry choosing the ABI and a dedicated phase-sized change touching both backends before resuming.
