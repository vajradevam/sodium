#include "test_framework.hpp"
#include "parser.hpp"
#include "generation.hpp"

#include <cstdint>

TEST(types_equal_int_widths) {
    CHECK(Type::make_int(IntType::i32) == Type::make_int(IntType::i32));
    CHECK(Type::make_int(IntType::i32) != Type::make_int(IntType::i64));
    CHECK(Type::make_int(IntType::u8) != Type::make_int(IntType::i8));
}

TEST(types_pointer_equality_across_arenas) {
    ArenaAllocator a(256);
    ArenaAllocator b(256);
    Type* base_a = a.alloc<Type>();
    Type* base_b = b.alloc<Type>();
    *base_a = Type::make_int(IntType::i32);
    *base_b = Type::make_int(IntType::i32);
    Type pa = Type::make_ptr(base_a, a);
    Type pb = Type::make_ptr(base_b, b);
    CHECK(pa.kind == Type::Ptr);
    CHECK(pb.kind == Type::Ptr);
    CHECK(pa == pb); // distinct pointees, structurally equal types
}

TEST(types_pointer_equality_same_pointee) {
    ArenaAllocator arena(256);
    Type* base = arena.alloc<Type>();
    *base = Type::make_int(IntType::i64);
    Type p1 = Type::make_ptr(base);
    Type p2 = Type::make_ptr(base);
    CHECK(p1 == p2);
}

TEST(types_double_pointer_shape) {
    ArenaAllocator arena(256);
    Type* byte_type = arena.alloc<Type>();
    *byte_type = Type::make_int(IntType::i8);
    Type inner = Type::make_ptr(byte_type, arena);
    Type* inner_copy = arena.alloc<Type>();
    *inner_copy = inner;
    Type outer = Type::make_ptr(inner_copy, arena);
    const Type* t = &outer;
    int levels = 0;
    while (t->kind == Type::Ptr && levels < 8) { t = t->pointee; ++levels; }
    CHECK_EQ(levels, 4); // two logical pointers, each with a wrapper Type
    CHECK(t->int_type == IntType::i8);
}

TEST(types_single_vs_double_pointer_differ) {
    ArenaAllocator arena(256);
    Type* word_type = arena.alloc<Type>();
    *word_type = Type::make_int(IntType::i64);
    Type p1 = Type::make_ptr(word_type, arena);
    Type* p1_copy = arena.alloc<Type>();
    *p1_copy = p1;
    Type p2 = Type::make_ptr(p1_copy, arena);
    CHECK(p1 != p2);
}

TEST(types_size_of_integer_widths) {
    CHECK_EQ(type_size(Type::make_int(IntType::i8)), size_t(1));
    CHECK_EQ(type_size(Type::make_int(IntType::u8)), size_t(1));
    CHECK_EQ(type_size(Type::make_int(IntType::i16)), size_t(2));
    CHECK_EQ(type_size(Type::make_int(IntType::i32)), size_t(4));
    CHECK_EQ(type_size(Type::make_int(IntType::i64)), size_t(8));
}

TEST(types_size_of_pointer_is_word) {
    ArenaAllocator arena(256);
    Type* byte_type = arena.alloc<Type>();
    *byte_type = Type::make_int(IntType::i8);
    Type p = Type::make_ptr(byte_type, arena);
    CHECK_EQ(type_size(p), size_t(8));
}

TEST(struct_layout_follows_c_alignment_and_padding) {
    std::vector<Token> names;
    names.push_back({TokenType::ident, "a", {}});
    names.push_back({TokenType::ident, "b", {}});
    names.push_back({TokenType::ident, "c", {}});
    std::vector<std::optional<Type>> types = {
        Type::make_int(IntType::i8),
        Type::make_int(IntType::i32),
        Type::make_int(IntType::i16),
    };
    StructInfo info = StructInfo::compute_layout(names, types);
    CHECK_EQ(info.size, size_t(12));
    CHECK_EQ(info.field_offsets.at("a"), size_t(0));
    CHECK_EQ(info.field_offsets.at("b"), size_t(4));
    CHECK_EQ(info.field_offsets.at("c"), size_t(8));
}

TEST(struct_layout_packs_bytes_and_adds_trailing_padding) {
    std::vector<Token> names;
    names.push_back({TokenType::ident, "a", {}});
    names.push_back({TokenType::ident, "b", {}});
    std::vector<std::optional<Type>> types = {
        Type::make_int(IntType::i8),
        Type::make_int(IntType::i64),
    };
    StructInfo info = StructInfo::compute_layout(names, types);
    CHECK_EQ(info.field_offsets.at("a"), size_t(0));
    CHECK_EQ(info.field_offsets.at("b"), size_t(8));
    CHECK_EQ(info.size, size_t(16));
}

TEST(struct_layout_all_i8_has_no_internal_padding) {
    std::vector<Token> names;
    names.push_back({TokenType::ident, "a", {}});
    names.push_back({TokenType::ident, "b", {}});
    names.push_back({TokenType::ident, "c", {}});
    std::vector<std::optional<Type>> types = {
        Type::make_int(IntType::i8), Type::make_int(IntType::i8), Type::make_int(IntType::i8)
    };
    StructInfo info = StructInfo::compute_layout(names, types);
    CHECK_EQ(info.size, size_t(3));
    CHECK_EQ(info.field_offsets.at("c"), size_t(2));
}
