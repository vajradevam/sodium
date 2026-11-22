#include "test_framework.hpp"
#include "ir/builder.hpp"
#include "ir/linear_scan.hpp"
#include "ir/verify.hpp"
#include "emit_helpers.hpp"

#include <algorithm>
#include <string>

// Floating-point IR, register classes, allocation and lowering (D-038).

static IRValue f64v(uint32_t v) { return IRValue::fvreg(v, IRWidth::F64); }
static IRValue f32v(uint32_t v) { return IRValue::fvreg(v, IRWidth::F32); }

static bool has_line(const std::vector<std::string>& lines, const std::string& want) {
    return std::find(lines.begin(), lines.end(), want) != lines.end();
}

static bool has_prefix(const std::vector<std::string>& lines, const std::string& prefix) {
    for (const auto& l : lines) if (l.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

/// A function computing c = a + b on two constants and returning it, with every vreg homed
/// in its own stack slot (what the allocator does for floats).
static IRFunction add_function(IRWidth w, std::map<uint32_t, Home>& homes) {
    IRBuilder ir;
    ir.start_function("fadd_fn");
    uint32_t a = ir.fconst(w, w == IRWidth::F32 ? 0x3fc00000u : 0x3ff8000000000000ull);
    uint32_t b = ir.fconst(w, w == IRWidth::F32 ? 0x40000000u : 0x4000000000000000ull);
    uint32_t c = ir.fadd(IRValue::fvreg(a, w), IRValue::fvreg(b, w));
    ir.ret(IRValue::fvreg(c, w));
    IRFunction f = std::move(*ir.end_function());
    for (uint32_t v = 0; v < 3; v++) homes[v] = slot(static_cast<int>(v));
    return f;
}

TEST(float_builder_marks_fp_vregs_and_verifies) {
    IRBuilder ir;
    ir.start_function("f");
    uint32_t i = ir.load_i64(3);
    uint32_t x = ir.i2f(IRWidth::F64, IRValue::vreg(i));
    uint32_t y = ir.fconst(IRWidth::F64, 0x4000000000000000ull);
    uint32_t s = ir.fmul(f64v(x), f64v(y));
    uint32_t lt = ir.fcmp(IROpcode::FCMP_LT, f64v(x), f64v(s));
    uint32_t back = ir.f2i(f64v(s));
    uint32_t sum = ir.add(IRValue::vreg(lt), IRValue::vreg(back));
    ir.ret(IRValue::vreg(sum));
    auto f = ir.end_function();
    CHECK(!f->is_fp(i));
    CHECK(f->is_fp(x));
    CHECK(f->is_fp(y));
    CHECK(f->is_fp(s));
    CHECK(!f->is_fp(lt));
    CHECK(!f->is_fp(back));
    CHECK(verify_function(*f).ok());
}

TEST(float_verifier_rejects_class_mismatches) {
    {   // an integer vreg used as a float operand
        IRBuilder ir;
        ir.start_function("f");
        uint32_t i = ir.load_i64(1);
        uint32_t x = ir.fconst(IRWidth::F64, 0);
        ir.emit(IRInstruction::fbinary(IROpcode::FADD, 99, f64v(x), IRValue::fvreg(i, IRWidth::F64)));
        ir.ret_void();
        CHECK(!verify_function(*ir.end_function()).ok());
    }
    {   // a float vreg fed to an integer add
        IRBuilder ir;
        ir.start_function("f");
        uint32_t x = ir.fconst(IRWidth::F64, 0);
        uint32_t i = ir.load_i64(1);
        ir.add(IRValue::vreg(x), IRValue::vreg(i));
        ir.ret_void();
        CHECK(!verify_function(*ir.end_function()).ok());
    }
    {   // an operand width that disagrees with the vreg's class
        IRBuilder ir;
        ir.start_function("f");
        uint32_t x = ir.fconst(IRWidth::F64, 0);
        uint32_t y = ir.fconst(IRWidth::F64, 0);
        ir.fadd(IRValue::vreg(x), f64v(y));
        ir.ret_void();
        CHECK(!verify_function(*ir.end_function()).ok());
    }
}

TEST(float_dump_names_opcodes_and_widths) {
    std::map<uint32_t, Home> homes;
    IRFunction f = add_function(IRWidth::F64, homes);
    const std::string text = f.to_string();
    CHECK(text.find("fconst f64 0x3ff8000000000000") != std::string::npos);
    CHECK(text.find("fadd f64:%0, f64:%1") != std::string::npos);
    CHECK(text.find("ret f64:%2") != std::string::npos);
}

TEST(float_vregs_are_always_spilled_integers_are_not) {
    IRBuilder ir;
    ir.start_function("f");
    uint32_t i = ir.load_i64(4);
    uint32_t x = ir.fconst(IRWidth::F64, 0);
    uint32_t y = ir.fadd(f64v(x), f64v(x));
    ir.ret(IRValue::vreg(i));
    auto f = ir.end_function();
    const TargetRegisterInfo tri = TargetRegisterInfo::x86_64_systemv();   // the allocator keeps a reference
    LinearScanAllocator alloc(tri);
    RegisterAllocation result = alloc.allocate(*f);
    CHECK(result.is_physical(i));
    CHECK(result.is_spilled(x));
    CHECK(result.is_spilled(y));
}

TEST(float_add_lowers_through_scratch_registers_on_x86_64) {
    std::map<uint32_t, Home> homes;
    IRFunction f = add_function(IRWidth::F64, homes);
    auto lines = emit_lines(f, homes, TestTarget::X86_64);
    CHECK(has_prefix(lines, "mov r11, 4609434218613702656"));      // 1.5
    CHECK(has_line(lines, "movq xmm0, r11"));
    CHECK(has_line(lines, "movsd [rbp - 8], xmm0"));
    CHECK(contains_in_order(lines, {"movsd xmm0, [rbp - 8]", "movsd xmm1, [rbp - 16]",
                                    "addsd xmm0, xmm1", "movsd [rbp - 24], xmm0",
                                    "movsd xmm0, [rbp - 24]"}));
}

TEST(float_add_lowers_through_scratch_registers_on_riscv64) {
    std::map<uint32_t, Home> homes;
    IRFunction f = add_function(IRWidth::F64, homes);
    auto lines = emit_lines(f, homes, TestTarget::RiscV64);
    CHECK(has_line(lines, "fmv.d.x f0, t0"));
    CHECK(has_line(lines, "fsd f0, -24(s0)"));
    CHECK(contains_in_order(lines, {"fld f0, -24(s0)", "fld f1, -32(s0)", "fadd.d f0, f0, f1",
                                    "fsd f0, -40(s0)", "fld f10, -40(s0)"}));
}

TEST(float_single_precision_uses_single_instructions) {
    std::map<uint32_t, Home> homes;
    IRFunction fx = add_function(IRWidth::F32, homes);
    auto x86 = emit_lines(fx, homes, TestTarget::X86_64);
    CHECK(has_line(x86, "addss xmm0, xmm1"));
    CHECK(has_line(x86, "movd xmm0, r11d"));
    CHECK(has_prefix(x86, "movss"));
    homes.clear();
    IRFunction fr = add_function(IRWidth::F32, homes);
    auto rv = emit_lines(fr, homes, TestTarget::RiscV64);
    CHECK(has_line(rv, "fadd.s f0, f0, f1"));
    CHECK(has_line(rv, "fmv.w.x f0, t0"));
    CHECK(has_prefix(rv, "flw"));
}

static IRFunction compare_function(IROpcode op) {
    IRBuilder ir;
    ir.start_function("cmp");
    uint32_t a = ir.fconst(IRWidth::F64, 0);
    uint32_t b = ir.fconst(IRWidth::F64, 0);
    uint32_t r = ir.fcmp(op, f64v(a), f64v(b));
    ir.ret(IRValue::vreg(r));
    return std::move(*ir.end_function());
}

TEST(float_compare_is_false_for_nan_except_not_equal_on_x86_64) {
    std::map<uint32_t, Home> homes = {{0, slot(0)}, {1, slot(1)}, {2, reg(0)}};
    {
        IRFunction f = compare_function(IROpcode::FCMP_EQ);
        auto l = emit_lines(f, homes);
        CHECK(has_line(l, "ucomisd xmm0, xmm1"));
        CHECK(has_line(l, "sete al"));
        CHECK(has_line(l, "setnp r11b"));   // unordered sets ZF and PF: eq needs PF clear
        CHECK(has_line(l, "and al, r11b"));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_NE);
        auto l = emit_lines(f, homes);
        CHECK(has_line(l, "setne al"));
        CHECK(has_line(l, "setp r11b"));
        CHECK(has_line(l, "or al, r11b"));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_LT);   // a < b is b > a: operands swap
        auto l = emit_lines(f, homes);
        CHECK(has_line(l, "ucomisd xmm1, xmm0"));
        CHECK(has_line(l, "seta al"));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_GE);
        auto l = emit_lines(f, homes);
        CHECK(has_line(l, "ucomisd xmm0, xmm1"));
        CHECK(has_line(l, "setae al"));
    }
}

TEST(float_compare_uses_ordered_instructions_on_riscv64) {
    std::map<uint32_t, Home> homes = {{0, slot(0)}, {1, slot(1)}, {2, reg(10)}};
    {
        IRFunction f = compare_function(IROpcode::FCMP_EQ);
        CHECK(has_line(emit_lines(f, homes, TestTarget::RiscV64), "feq.d a0, f0, f1"));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_NE);
        auto l = emit_lines(f, homes, TestTarget::RiscV64);
        CHECK(contains_in_order(l, {"feq.d a0, f0, f1", "xori a0, a0, 1"}));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_GT);   // a > b is b < a
        CHECK(has_line(emit_lines(f, homes, TestTarget::RiscV64), "flt.d a0, f1, f0"));
    }
    {
        IRFunction f = compare_function(IROpcode::FCMP_LE);
        CHECK(has_line(emit_lines(f, homes, TestTarget::RiscV64), "fle.d a0, f0, f1"));
    }
}

TEST(float_conversions_lower_to_the_native_instructions) {
    IRBuilder ir;
    ir.start_function("conv");
    uint32_t i = ir.load_i64(7);
    uint32_t x = ir.i2f(IRWidth::F64, IRValue::vreg(i));
    uint32_t s = ir.f2f(IRWidth::F32, f64v(x));
    uint32_t w = ir.f2f(IRWidth::F64, f32v(s));
    uint32_t back = ir.f2i(f64v(w));
    ir.ret(IRValue::vreg(back));
    IRFunction f = std::move(*ir.end_function());
    std::map<uint32_t, Home> homes = {{0, reg(1)}, {1, slot(0)}, {2, slot(1)}, {3, slot(2)}, {4, reg(0)}};
    {
        IRFunction g = f;
        auto l = emit_lines(g, homes, TestTarget::X86_64);
        CHECK(has_line(l, "cvtsi2sd xmm0, rbx"));
        CHECK(has_line(l, "cvtsd2ss xmm0, xmm0"));
        CHECK(has_line(l, "cvtss2sd xmm0, xmm0"));
        CHECK(has_line(l, "cvttsd2si rax, xmm0"));
    }
    {
        IRFunction g = f;
        homes[0] = reg(9);
        homes[4] = reg(10);
        auto l = emit_lines(g, homes, TestTarget::RiscV64);
        CHECK(has_line(l, "fcvt.d.l f0, s1"));
        CHECK(has_line(l, "fcvt.s.d f0, f0"));
        CHECK(has_line(l, "fcvt.d.s f0, f0"));
        CHECK(has_line(l, "fcvt.l.d a0, f0, rtz"));
    }
}

TEST(float_negation_flips_the_sign_bit) {
    IRBuilder ir;
    ir.start_function("neg");
    uint32_t a = ir.fconst(IRWidth::F64, 0);
    uint32_t n = ir.fneg(f64v(a));
    ir.ret(f64v(n));
    IRFunction f = std::move(*ir.end_function());
    std::map<uint32_t, Home> homes = {{0, slot(0)}, {1, slot(1)}};
    IRFunction g = f;
    auto x = emit_lines(g, homes, TestTarget::X86_64);
    CHECK(contains_in_order(x, {"movq r11, xmm0", "btc r11, 63", "movq xmm0, r11"}));
    auto r = emit_lines(f, homes, TestTarget::RiscV64);
    CHECK(has_line(r, "fneg.d f0, f0"));
}

TEST(float_loads_and_stores_go_through_memory_operands) {
    IRBuilder ir;
    ir.start_function("mem");
    uint32_t base = ir.frame_addr(0);
    uint32_t v = ir.fload(IRWidth::F32, IRValue::vreg(base), 4);
    ir.fstore(IRValue::vreg(base), 8, f32v(v));
    ir.ret_void();
    IRFunction f = std::move(*ir.end_function());
    std::map<uint32_t, Home> homes = {{0, reg(2)}, {1, slot(0)}};
    IRFunction g = f;
    auto x = emit_lines(g, homes, TestTarget::X86_64);
    CHECK(has_line(x, "movss xmm0, [rcx + 4]"));
    CHECK(has_line(x, "movss [rcx + 8], xmm0"));
    auto r = emit_lines(f, homes, TestTarget::RiscV64);
    CHECK(has_prefix(r, "flw f0, 4("));
    CHECK(has_prefix(r, "fsw f0, 8("));
}

TEST(float_call_arguments_use_independent_register_counters) {
    // f(int a, double x, int b, double y): a->rdi, b->rsi on x86_64; x->xmm0, y->xmm1.
    IRBuilder ir;
    ir.start_function("caller");
    uint32_t a = ir.load_i64(5);
    uint32_t b = ir.load_i64(6);
    uint32_t x = ir.fconst(IRWidth::F64, 0);
    uint32_t y = ir.fconst(IRWidth::F64, 0);
    ir.call_f("callee", {IRValue::vreg(a), f64v(x), IRValue::vreg(b), f64v(y)}, IRWidth::F64);
    ir.ret_void();
    IRFunction f = std::move(*ir.end_function());
    std::map<uint32_t, Home> homes = {{0, reg(2)}, {1, reg(3)}, {2, slot(0)}, {3, slot(1)}, {4, slot(2)}};
    IRFunction g = f;
    auto l = emit_lines(g, homes, TestTarget::X86_64);
    CHECK(has_line(l, "mov rdi, rcx"));
    CHECK(has_line(l, "mov rsi, rdx"));
    CHECK(has_line(l, "movsd xmm0, [rbp - 8]"));
    CHECK(has_line(l, "movsd xmm1, [rbp - 16]"));
    CHECK(has_line(l, "mov eax, 2"));                    // vector register count for varargs
    CHECK(contains_in_order(l, {"call callee", "movsd [rbp - 24], xmm0"}));
    homes[0] = reg(10 + 1);   // t1.. any allocatable register; the point is the FP placement
    homes[1] = reg(12);
    auto r = emit_lines(f, homes, TestTarget::RiscV64);
    CHECK(has_line(r, "fld f10, -24(s0)"));
    CHECK(has_line(r, "fld f11, -32(s0)"));
    CHECK(contains_in_order(r, {"call callee", "fsd f10, -40(s0)"}));
}

TEST(float_parameters_are_stored_from_their_argument_registers) {
    IRBuilder ir;
    ir.start_function("callee");
    ir.store_param(FP_PARAM_ORDINAL_BASE + 0, 0, -8, IRWidth::F64);
    ir.store_param(FP_PARAM_ORDINAL_BASE + 1, 0, -16, IRWidth::F32);
    ir.store_param(0, 0, -24, IRWidth::I64);
    ir.ret_void();
    IRFunction f = std::move(*ir.end_function());
    IRFunction g = f;
    auto x = emit_lines(g, {}, TestTarget::X86_64);
    CHECK(has_line(x, "movsd [rbp - 8], xmm0"));
    CHECK(has_line(x, "movss [rbp - 16], xmm1"));
    CHECK(has_line(x, "mov [rbp - 24], rdi"));
    auto r = emit_lines(f, {}, TestTarget::RiscV64);
    CHECK(has_line(r, "fsd f10, -24(s0)"));
    CHECK(has_line(r, "fsw f11, -32(s0)"));
    CHECK(has_line(r, "sd a0, -40(s0)"));
}

TEST(float_stack_arguments_are_pushed_from_their_homes) {
    // Nine floats on System V: the ninth is a stack word pushed from its home slot.
    IRBuilder ir;
    ir.start_function("many");
    std::vector<IRValue> args;
    std::map<uint32_t, Home> homes;
    for (int i = 0; i < 9; i++) {
        uint32_t v = ir.fconst(IRWidth::F64, 0);
        args.push_back(f64v(v));
        homes[v] = slot(i);
    }
    ir.call("callee", args, 8);
    ir.ret_void();
    IRFunction f = std::move(*ir.end_function());
    // Homes of call results are irrelevant: the call has no float result here.
    auto l = emit_lines(f, homes, TestTarget::X86_64);
    CHECK(has_line(l, "movsd xmm7, [rbp - 64]"));
    CHECK(has_prefix(l, "push qword [rbp - 72]"));
    CHECK(has_line(l, "call callee"));
}

TEST(float_return_value_is_placed_in_the_result_register) {
    std::map<uint32_t, Home> homes;
    IRFunction f = add_function(IRWidth::F64, homes);
    auto x = emit_lines(f, homes, TestTarget::X86_64);
    CHECK(contains_in_order(x, {"movsd xmm0, [rbp - 24]", "mov rsp, rbp", "pop rbp", "ret"}));
    homes.clear();
    IRFunction g = add_function(IRWidth::F64, homes);
    auto r = emit_lines(g, homes, TestTarget::RiscV64);
    CHECK(has_line(r, "fld f10, -40(s0)"));
}
