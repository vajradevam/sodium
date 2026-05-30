#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cassert>

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
    int scratch_reg = 0;         ///< scratch register for spills

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
        info.add(11, "r11", true, false);
        info.add(12, "r12", false, true);
        info.add(13, "r13", false, true);
        info.add(14, "r14", false, true);
        info.add(15, "r15", false, true);

        info.arg_regs = {5, 4, 3, 2, 8, 9};  // rdi, rsi, rdx, rcx, r8, r9
        info.ret_reg = 0;   // rax
        info.scratch_reg = 0;  // rax (also used for return, so be careful)
        // Use r11 as dedicated scratch for spills (caller-save, not an arg)
        info.scratch_reg = 11;  // r11

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

        info.add(5,  "t0", true, false);   // caller-save temp
        info.add(6,  "t1", true, false);
        info.add(7,  "t2", true, false);
        info.add(8,  "s0", false, true);   // callee-save / frame pointer
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
        info.scratch_reg = 5;  // t0

        return info;
    }
};
