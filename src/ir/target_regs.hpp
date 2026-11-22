#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cassert>

/// Marks an FP register number in contexts that also carry integer registers, such as the
/// (register, frame offset) pairs of IRInstruction::arg_loads.
constexpr int FP_REG_FLAG = 0x1000;
constexpr int FP_SINGLE_FLAG = 0x2000;   ///< with FP_REG_FLAG: the value is a 32-bit float
constexpr int FP_REG_MASK = 0xFFF;

/// Parameter ordinals at or above this value name FP argument register `ordinal - base`
/// (STORE_PARAM); smaller ordinals are the integer register / stack indices of D-012.
constexpr int64_t FP_PARAM_ORDINAL_BASE = 1000;

/// Describes a target's physical register file for the allocator.
struct TargetRegisterInfo {
    struct Reg {
        int num;            ///< Physical register number (0..N-1)
        std::string name;   ///< Assembly name (e.g., "rax")
        bool caller_save;   ///< Callee trashes this (caller must save)
        bool callee_save;   ///< Caller expects callee to preserve
        bool reserved;      ///< Never allocated (rsp, etc.)
        bool allocatable;   ///< Available for general allocation
    };

    std::vector<Reg> regs;

    /// Registers used for argument passing in calling convention.
    std::vector<int> arg_regs;   ///< physical register numbers for args

    int ret_reg = 0;             ///< return value register (typically rax)
    int ret_reg2 = 0;            ///< second return register for two-word results (rdx / a1)
    int scratch_reg = 0;         ///< scratch register for spills
    /// Register that holds the target of a CALL_INDIRECT (D-014): caller-saved and never an
    /// argument register, so argument setup cannot overwrite it.
    int call_target_reg = 0;
    int fp_reg = 0;              ///< frame pointer register
    /// A multi-word struct argument may straddle the last argument register and the stack
    /// (RISC-V LP64). System V AMD64 passes such a struct entirely on the stack instead.
    bool split_struct_args = false;
    /// Structs larger than 16 bytes are passed as a pointer to a caller-made copy (RISC-V LP64);
    /// System V AMD64 copies them onto the stack instead.
    bool large_struct_by_ref = false;
    int fp_bias = 0;             ///< added to raw fp-relative spill offsets (riscv64: s0 sits 16 bytes above the
                                 ///< x86 rbp-equivalent because ra/s0 are saved below it)

    // ---- floating point (D-038) ----
    // FP registers are numbered in their own file (xmm0-15 / f0-f31). Every FP register is
    // caller-saved and FP vregs live in stack homes, so only the ABI registers and two scratch
    // registers matter to the compiler.
    std::string fp_prefix;               ///< "xmm" or "f"; register n is named fp_prefix + n
    std::vector<int> fp_arg_regs;        ///< FP argument registers in order
    int fp_ret_reg = 0;                  ///< FP result register
    int fp_scratch0 = 0;                 ///< scratch FP registers used by lowered FP operations
    int fp_scratch1 = 1;
    /// When the FP argument registers run out, further FP arguments use the integer argument
    /// registers (RISC-V LP64D); otherwise they go on the stack (System V).
    bool fp_overflow_to_gpr = false;

    std::string fp_name(int n) const { return fp_prefix + std::to_string(n); }

    /// Add a register.
    void add(int num, const std::string& name,
             bool caller_save, bool callee_save,
             bool reserved = false, bool allocatable = true) {
        regs.push_back({ num, name, caller_save, callee_save, reserved, allocatable });
    }

    /// Get number of allocatable registers.
    int num_allocatable() const {
        int count = 0;
        for (auto& r : regs)
            if (r.allocatable) count++;
        return count;
    }

    /// Get list of allocatable register numbers.
    std::vector<int> allocatable_regs() const {
        std::vector<int> result;
        for (auto& r : regs)
            if (r.allocatable) result.push_back(r.num);
        return result;
    }

    /// Get caller-save allocatable registers.
    std::vector<int> caller_save_regs() const {
        std::vector<int> result;
        for (auto& r : regs)
            if (r.allocatable && r.caller_save)
                result.push_back(r.num);
        return result;
    }

    /// Get callee-save allocatable registers.
    std::vector<int> callee_save_regs() const {
        std::vector<int> result;
        for (auto& r : regs)
            if (r.allocatable && r.callee_save)
                result.push_back(r.num);
        return result;
    }

    /// Look up a register by name. Returns -1 if not found.
    int by_name(const std::string& name) const {
        for (auto& r : regs)
            if (r.name == name) return r.num;
        return -1;
    }

    /// Get name of register number.
    const std::string& name_of(int num) const {
        static std::string unknown = "???";
        for (auto& r : regs)
            if (r.num == num) return r.name;
        return unknown;
    }

    // ---- Predefined backends ----

    /// x86-64 System V ABI register file.
    static TargetRegisterInfo x86_64_systemv() {
        TargetRegisterInfo info;

        // General-purpose registers (16 total)
        // rax: caller-save, return value
        info.add(0, "rax", true, false);    // return value, scratch
        info.add(1, "rbx", false, true);    // callee-save
        info.add(2, "rcx", true, false);    // arg #4
        info.add(3, "rdx", true, false);    // arg #3
        info.add(4, "rsi", true, false);    // arg #2
        info.add(5, "rdi", true, false);    // arg #1
        info.add(6, "rbp", false, true, true, false);  // frame pointer (reserved)
        info.add(7, "rsp", false, false, true, false);  // reserved: stack pointer
        info.add(8, "r8",  true, false);    // arg #5
        info.add(9, "r9",  true, false);    // arg #6
        info.add(10, "r10", true, false);
        info.add(11, "r11", true, false, true, false);  // scratch (reserved)
        info.add(12, "r12", false, true);
        info.add(13, "r13", false, true);
        info.add(14, "r14", false, true);
        info.add(15, "r15", false, true);

        info.arg_regs = {5, 4, 3, 2, 8, 9};  // rdi, rsi, rdx, rcx, r8, r9
        info.ret_reg = 0;   // rax
        info.ret_reg2 = 3;  // rdx
        info.fp_reg = 6;     // rbp
        info.scratch_reg = 0;  // rax (also used for return, so be careful)
        // Use r11 as dedicated scratch for spills (caller-save, not an arg)
        info.scratch_reg = 11;  // r11
        info.call_target_reg = 0;  // rax

        info.fp_prefix = "xmm";
        info.fp_arg_regs = {0, 1, 2, 3, 4, 5, 6, 7};
        info.fp_ret_reg = 0;
        info.fp_scratch0 = 0;
        info.fp_scratch1 = 1;

        return info;
    }

    /// RISC-V LP64 calling convention register file.
    /// Registers defined per the RISC-V calling convention:
    /// x0=zero, x1=ra, x2=sp, x3=gp, x4=tp, x5-7/t0-2, x8-9/s0-1,
    /// x10-17/a0-a7, x18-27/s2-s11, x28-31/t3-t6.
    static TargetRegisterInfo riscv64_lp64() {
        TargetRegisterInfo info;

        // RISC-V has 32 integer registers x0-x31
        // x0 = zero (always 0, reserved)
        // x1 = ra (return address)
        // x2 = sp (stack pointer)
        // x3 = gp (global pointer)
        // x4 = tp (thread pointer)
        // x5-x7, x28-x31 = caller-save temporaries
        // x8-x9 = callee-save (s0/s1)
        // x10-x17 = argument registers (a0-a7), caller-save
        // x18-x27 = callee-save (s2-s11)

        // For now, just reserve x0, x1, x2, x3, x4
        info.add(0,  "zero", false, false, true, false);
        info.add(1,  "ra",   true,  false, true, false);   // return address
        info.add(2,  "sp",   false, false, true, false);   // stack pointer
        info.add(3,  "gp",   false, false, true, false);   // global pointer
        info.add(4,  "tp",   false, false, true, false);   // thread pointer

        info.add(5,  "t0", true, false, true, false);   // scratch (reserved)
        info.add(6,  "t1", true, false);
        info.add(7,  "t2", true, false);
        info.add(8,  "s0", false, true, true, false);   // frame pointer (reserved, not allocatable)
        info.add(9,  "s1", false, true);   // callee-save
        info.add(10, "a0", true, false);   // arg #1 / return value
        info.add(11, "a1", true, false);   // arg #2
        info.add(12, "a2", true, false);   // arg #3
        info.add(13, "a3", true, false);   // arg #4
        info.add(14, "a4", true, false);   // arg #5
        info.add(15, "a5", true, false);   // arg #6
        info.add(16, "a6", true, false);   // arg #7
        info.add(17, "a7", true, false);   // arg #8
        info.add(18, "s2", false, true);
        info.add(19, "s3", false, true);
        info.add(20, "s4", false, true);
        info.add(21, "s5", false, true);
        info.add(22, "s6", false, true);
        info.add(23, "s7", false, true);
        info.add(24, "s8", false, true);
        info.add(25, "s9", false, true);
        info.add(26, "s10", false, true);
        info.add(27, "s11", false, true);
        info.add(28, "t3", true, false);
        info.add(29, "t4", true, false);
        info.add(30, "t5", true, false);
        info.add(31, "t6", true, false);

        info.arg_regs = {10, 11, 12, 13, 14, 15, 16, 17};  // a0-a7
        info.ret_reg = 10;   // a0
        info.ret_reg2 = 11;  // a1
        info.fp_reg = 8;     // s0
        info.split_struct_args = true;
        info.large_struct_by_ref = true;
        info.fp_bias = -16;  // locals use addr_fp() (offset - 16); spill slots must match
        info.scratch_reg = 5;  // t0
        info.call_target_reg = 31;  // t6

        info.fp_prefix = "f";
        info.fp_arg_regs = {10, 11, 12, 13, 14, 15, 16, 17};  // fa0-fa7
        info.fp_ret_reg = 10;                                   // fa0
        info.fp_scratch0 = 0;                                   // ft0
        info.fp_scratch1 = 1;                                   // ft1
        info.fp_overflow_to_gpr = true;                         // LP64D

        return info;
    }

    /// Minimal dummy TargetRegisterInfo for LSP / testing.
    /// Provides just enough structure for codegen but no real registers.
    static TargetRegisterInfo dummy() {
        TargetRegisterInfo info;
        info.arg_regs = {};
        info.ret_reg = 0;
        info.scratch_reg = 0;
        info.fp_prefix = "f";
        return info;
    }
};

/// Where one register-passed call operand travels.
struct ArgRegAssign {
    bool fp = false;   ///< true: FP register `preg` of the FP file; false: integer register `preg`
    int preg = -1;
};

/// Assign argument registers to the register-passed operands of a call, in order. Integer
/// operands take the next integer argument register; a float takes the next FP argument
/// register, or, once those are used up on a target that allows it (LP64D), the next integer
/// one. The caller (ArgPlacer) guarantees the operands fit.
inline std::vector<ArgRegAssign> assign_arg_regs(const TargetRegisterInfo& tri,
                                                 const std::vector<bool>& is_fp) {
    std::vector<ArgRegAssign> out;
    size_t next_int = 0;
    size_t next_fp = 0;
    for (bool fp : is_fp) {
        if (fp && next_fp < tri.fp_arg_regs.size()) {
            out.push_back({true, tri.fp_arg_regs[next_fp++]});
        } else {
            assert(next_int < tri.arg_regs.size());
            out.push_back({false, tri.arg_regs[next_int++]});
        }
    }
    return out;
}
