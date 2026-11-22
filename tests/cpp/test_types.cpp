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

TEST(types_struct_identity_and_pointer) {
    Type p = Type::make_struct("Point");
    CHECK(p.kind == Type::Struct);
    CHECK(p.struct_name == std::string("Point"));
    CHECK(p == Type::make_struct("Point"));
    CHECK(p != Type::make_struct("Other"));
    Type pp = Type::make_ptr(&p);
    CHECK(pp.kind == Type::Ptr);
    CHECK(pp.pointee != nullptr);
    if (pp.pointee) CHECK(*pp.pointee == p);
}

namespace {
// Builds `fn(params...) -> ret` (ret may be null) in the given arena.
Type make_fn_ptr(ArenaAllocator& arena, const std::vector<Type>& params, const Type* ret) {
    Type* array = params.empty() ? nullptr : arena.alloc_array<Type>(params.size());
    for (size_t i = 0; i < params.size(); i++) array[i] = params[i];
    Type* r = nullptr;
    if (ret) { r = arena.alloc<Type>(); *r = *ret; }
    Type* fn = arena.alloc<Type>();
    *fn = Type::make_func(array, static_cast<uint32_t>(params.size()), r);
    return Type::make_ptr(fn, arena);
}
} // namespace

TEST(types_function_pointer_equality_is_structural) {
    ArenaAllocator a(512);
    ArenaAllocator b(512);
    const Type i64 = Type::make_int(IntType::i64);
    const Type i32 = Type::make_int(IntType::i32);
    Type f1 = make_fn_ptr(a, {i64, i64}, &i64);
    Type f2 = make_fn_ptr(b, {i64, i64}, &i64);
    CHECK(f1 == f2);
    CHECK(f1 != make_fn_ptr(a, {i64}, &i64));          // arity
    CHECK(f1 != make_fn_ptr(a, {i64, i32}, &i64));     // parameter type
    CHECK(f1 != make_fn_ptr(a, {i64, i64}, &i32));     // return type
    CHECK(f1 != make_fn_ptr(a, {i64, i64}, nullptr));  // annotated vs unannotated return
    CHECK(make_fn_ptr(a, {}, nullptr) == make_fn_ptr(b, {}, nullptr));
    CHECK(type_size(f1) == 8);
}
