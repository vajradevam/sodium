#include "dump.hpp"
#include "block.hpp"
#include "function.hpp"
#include "module.hpp"
#include <sstream>
#include <cassert>

// ---- opcode names ----

const char* ir_opcode_name(IROpcode op) {
    switch (op) {
        case IROpcode::NOP:         return "nop";
        case IROpcode::LOAD_I64:    return "load_i64";
        case IROpcode::LOAD_I32:    return "load_i32";
        case IROpcode::LOAD_I8:     return "load_i8";
        case IROpcode::LOAD_U64:    return "load_u64";
        case IROpcode::LOAD_U32_IMM: return "load_u32_imm";
        case IROpcode::LOAD_U8_IMM:  return "load_u8_imm";
        case IROpcode::COPY:        return "copy";
        case IROpcode::FRAME_ADDR:  return "frame_addr";
        case IROpcode::NEG:         return "neg";
        case IROpcode::NOT_:        return "not";
        case IROpcode::ZEXT:        return "zext";
        case IROpcode::SEXT:        return "sext";
        case IROpcode::TRUNC:       return "trunc";
        case IROpcode::ADD:         return "add";
        case IROpcode::SUB:         return "sub";
        case IROpcode::MUL:         return "mul";
        case IROpcode::DIV:         return "div";
        case IROpcode::MOD:         return "mod";
        case IROpcode::AND:         return "and";
        case IROpcode::OR:          return "or";
        case IROpcode::XOR:         return "xor";
        case IROpcode::SHL:         return "shl";
        case IROpcode::SHR:         return "shr";
        case IROpcode::ASHR:        return "ashr";
        case IROpcode::CMP_EQ:      return "cmp_eq";
        case IROpcode::CMP_NE:      return "cmp_ne";
        case IROpcode::CMP_LT:      return "cmp_lt";
        case IROpcode::CMP_LE:      return "cmp_le";
        case IROpcode::CMP_GT:      return "cmp_gt";
        case IROpcode::CMP_GE:      return "cmp_ge";
        case IROpcode::LOAD:        return "load";
        case IROpcode::LOAD_S8:     return "load_s8";
        case IROpcode::LOAD_U8:     return "load_u8";
        case IROpcode::LOAD_S16:    return "load_s16";
        case IROpcode::LOAD_U16:    return "load_u16";
        case IROpcode::LOAD_S32:    return "load_s32";
        case IROpcode::LOAD_U32:    return "load_u32";
        case IROpcode::STORE:       return "store";
        case IROpcode::STORE_8:     return "store_8";
        case IROpcode::STORE_16:    return "store_16";
        case IROpcode::STORE_32:    return "store_32";
        case IROpcode::LEA:         return "lea";
        case IROpcode::LEA_LABEL:   return "lea_label";
        case IROpcode::JMP:         return "jmp";
        case IROpcode::BR:          return "br";
        case IROpcode::CALL:        return "call";
        case IROpcode::RET:         return "ret";
        case IROpcode::RET_VOID:    return "ret_void";
        case IROpcode::SYSCALL:     return "syscall";
        case IROpcode::PUSH:        return "push";
        case IROpcode::POP:         return "pop";
    }
    return "???";
}

// ---- IRValue ----

std::string IRValue::to_string() const {
    std::ostringstream ss;
    switch (width) {
        case IRWidth::I8:  ss << "i8:";  break;
        case IRWidth::I16: ss << "i16:"; break;
        case IRWidth::I32: ss << "i32:"; break;
        case IRWidth::I64: ss << "";     break;
    }
    if (kind == Kind::VREG) {
        ss << "%" << vreg_id;
    } else {
        ss << imm;
    }
    return ss.str();
}

// ---- IRInstruction ----

static void append_operands(std::ostringstream& ss, const std::vector<IRValue>& ops) {
    for (size_t i = 0; i < ops.size(); i++) {
        if (i > 0) ss << ", ";
        ss << ops[i].to_string();
    }
}

std::string IRInstruction::to_string() const {
    std::ostringstream ss;

    // Result (if any)
    if (dst != NONE_VREG) {
        ss << "%" << dst << " = ";
    }

    // Opcode
    ss << ir_opcode_name(op);

    // Operands
    switch (op) {
        case IROpcode::LOAD_I64:
        case IROpcode::LOAD_I32:
        case IROpcode::LOAD_I8:
        case IROpcode::LOAD_U64:
        case IROpcode::LOAD_U32_IMM:
        case IROpcode::LOAD_U8_IMM:
            ss << " " << imm_arg;
            break;

        case IROpcode::FRAME_ADDR:
            ss << " slot" << imm_arg;
            break;

        case IROpcode::LOAD:
        case IROpcode::LOAD_S8:
        case IROpcode::LOAD_U8:
        case IROpcode::LOAD_S16:
        case IROpcode::LOAD_U16:
        case IROpcode::LOAD_S32:
        case IROpcode::LOAD_U32:
            ss << " [";
            append_operands(ss, operands);
            if (imm_arg != 0) ss << " + " << imm_arg;
            ss << "]";
            break;

        case IROpcode::STORE:
        case IROpcode::STORE_8:
        case IROpcode::STORE_16:
        case IROpcode::STORE_32:
            ss << " [";
            if (!operands.empty()) ss << operands[0].to_string();
            if (imm_arg != 0) ss << " + " << imm_arg;
            ss << "], ";
            if (operands.size() > 1) ss << operands[1].to_string();
            break;

        case IROpcode::LEA:
            ss << " [";
            append_operands(ss, operands);
            if (imm_arg != 0) ss << " + " << imm_arg;
            ss << "]";
            break;

        case IROpcode::LEA_LABEL:
            ss << " " << label_true;
            break;

        case IROpcode::JMP:
            ss << " " << label_true;
            break;

        case IROpcode::PUSH:
        case IROpcode::POP:
            if (!operands.empty()) {
                ss << " " << operands[0].to_string();
            }
            break;

        case IROpcode::BR:
            if (!operands.empty()) ss << " " << operands[0].to_string();
            ss << ", " << label_true << ", " << label_false;
            break;

        case IROpcode::CALL:
            ss << " " << call_target << "(";
            append_operands(ss, operands);
            ss << ")";
            break;

        case IROpcode::RET:
            if (!operands.empty()) ss << " " << operands[0].to_string();
            break;

        case IROpcode::ZEXT:
        case IROpcode::SEXT:
            if (!operands.empty()) ss << " " << operands[0].to_string();
            ss << " to " << static_cast<int>(imm_arg) << " bits";
            break;

        case IROpcode::TRUNC:
            if (!operands.empty()) ss << " " << operands[0].to_string();
            ss << " to " << static_cast<int>(dst_width) << " bits";
            break;

        default:
            if (!operands.empty()) {
                ss << " ";
                append_operands(ss, operands);
            }
            break;
    }

    return ss.str();
}

size_t IRInstruction::operand_count() const {
    switch (op) {
        case IROpcode::LOAD_I64:
        case IROpcode::LOAD_I32:
        case IROpcode::LOAD_I8:
        case IROpcode::LOAD_U64:
        case IROpcode::LOAD_U32_IMM:
        case IROpcode::LOAD_U8_IMM:
        case IROpcode::NOP:
        case IROpcode::RET_VOID:
        case IROpcode::SYSCALL:
        case IROpcode::FRAME_ADDR:
        case IROpcode::LEA_LABEL:
        case IROpcode::JMP:
            return 0;
        case IROpcode::COPY:
        case IROpcode::PUSH:
        case IROpcode::POP:
        case IROpcode::NEG:
        case IROpcode::NOT_:
        case IROpcode::ZEXT:
        case IROpcode::SEXT:
        case IROpcode::TRUNC:
        case IROpcode::LOAD:
        case IROpcode::LOAD_S8:
        case IROpcode::LOAD_U8:
        case IROpcode::LOAD_S16:
        case IROpcode::LOAD_U16:
        case IROpcode::LOAD_S32:
        case IROpcode::LOAD_U32:
        case IROpcode::LEA:
        case IROpcode::RET:
            return 1;
        case IROpcode::STORE:
        case IROpcode::STORE_8:
        case IROpcode::STORE_16:
        case IROpcode::STORE_32:
            return 2;
        case IROpcode::ADD:
        case IROpcode::SUB:
        case IROpcode::MUL:
        case IROpcode::DIV:
        case IROpcode::MOD:
        case IROpcode::AND:
        case IROpcode::OR:
        case IROpcode::XOR:
        case IROpcode::SHL:
        case IROpcode::SHR:
        case IROpcode::ASHR:
        case IROpcode::CMP_EQ:
        case IROpcode::CMP_NE:
        case IROpcode::CMP_LT:
        case IROpcode::CMP_LE:
        case IROpcode::CMP_GT:
        case IROpcode::CMP_GE:
        case IROpcode::BR:
            return 2;
        case IROpcode::CALL:
            return operands.size();
    }
    return operands.size();
}

// ---- IRBlock ----

std::string IRBlock::to_string() const {
    std::ostringstream ss;
    ss << label << ":\n";
    for (const auto& insn : instructions) {
        ss << "    " << insn.to_string() << "\n";
    }
    return ss.str();
}

// ---- IRFunction ----

std::string IRFunction::to_string() const {
    std::ostringstream ss;
    ss << "function " << name << "(" << next_vreg << " vregs, "
       << stack_slots << " stack slots):\n";
    for (const auto& block : blocks) {
        auto block_str = block.to_string();
        std::istringstream stream(block_str);
        std::string line;
        while (std::getline(stream, line)) {
            ss << "  " << line << "\n";
        }
    }
    return ss.str();
}

// ---- IRModule ----

std::string IRModule::to_string() const {
    std::ostringstream ss;
    ss << "module:\n";
    for (const auto& d : data_entries) {
        ss << "  data " << d.name << " = " << d.value << "\n";
    }
    for (const auto& b : bss_entries) {
        ss << "  bss " << b.name << " (" << b.size << " qwords)\n";
    }
    for (const auto& s : strings) {
        ss << "  string " << s.label << " = \"" << s.value << "\"\n";
    }
    for (const auto& f : functions) {
        ss << f->to_string() << "\n";
    }
    return ss.str();
}
