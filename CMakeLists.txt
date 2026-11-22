cmake_minimum_required(VERSION 3.20)

project(sodium)

set(CMAKE_CXX_STANDARD 20)

# ── Runtime libraries (x86-64) ─────────────────────────────────────
find_program(NASM nasm)
if(NASM)
    file(GLOB X86_RT_SOURCES
        ${CMAKE_SOURCE_DIR}/sodium-rt/Makefile
        ${CMAKE_SOURCE_DIR}/sodium-rt/*.c
        ${CMAKE_SOURCE_DIR}/sodium-rt/x86_64/*.asm)
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/sodium-rt/x86_64/sodium-rt.a
        COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/sodium-rt
        DEPENDS ${X86_RT_SOURCES}
        COMMENT "Building x86-64 runtime library..."
    )
    add_custom_target(sodium-rt-x86_64 DEPENDS
        ${CMAKE_SOURCE_DIR}/sodium-rt/x86_64/sodium-rt.a
    )
else()
    message(WARNING "nasm not found — x86-64 runtime must be built manually (make -C sodium-rt)")
endif()

# ── Runtime libraries (RISC-V) ─────────────────────────────────────
find_program(RISCV_CC riscv64-elf-gcc)
if(RISCV_CC)
    find_program(RISCV_AS riscv64-elf-as)
    if(RISCV_AS)
        file(GLOB RV_RT_SOURCES
            ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64/Makefile
            ${CMAKE_SOURCE_DIR}/sodium-rt/*.c
            ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64/*.asm)
        add_custom_command(
            OUTPUT ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64/sodium-rt.a
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64
            DEPENDS ${RV_RT_SOURCES}
            COMMENT "Building RISC-V runtime library..."
        )
        add_custom_target(sodium-rt-riscv64 DEPENDS
            ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64/sodium-rt.a
        )
    endif()
else()
    message(STATUS "riscv64-elf-gcc not found — RISC-V runtime must be built manually (make -C sodium-rt/riscv64)")
endif()

# Optionally build all runtimes
if(TARGET sodium-rt-x86_64 AND TARGET sodium-rt-riscv64)
    add_custom_target(sodium-rt ALL DEPENDS sodium-rt-x86_64 sodium-rt-riscv64)
elseif(TARGET sodium-rt-x86_64)
    add_custom_target(sodium-rt ALL DEPENDS sodium-rt-x86_64)
else()
    add_custom_target(sodium-rt ALL)
endif()

# ── Compiler sources ───────────────────────────────────────────────
set(BACKEND_SOURCES
    src/backend/x86_64/backend.cpp
    src/backend/riscv64/backend.cpp
)

set(IR_SOURCES
    src/ir/dump.cpp
)

# Collect all source files in src/gen/ (the split Generator modules)
file(GLOB GEN_SOURCES src/gen/*.cpp)

add_executable(sodium
    src/main.cpp
    src/arena.cpp
    src/tokenization.cpp
    src/parser.cpp
    src/ast_printer.cpp
    src/preprocessor.cpp
    ${GEN_SOURCES}
    ${BACKEND_SOURCES}
    ${IR_SOURCES}
)

add_executable(cyan-lsp
    src/lsp_main.cpp
    src/lsp.cpp
    src/arena.cpp
    src/tokenization.cpp
    src/parser.cpp
    src/ast_printer.cpp
    src/preprocessor.cpp
    ${GEN_SOURCES}
    ${BACKEND_SOURCES}
    ${IR_SOURCES}
)

# T1: strict warnings, fatal via -Werror for every build (including check.sh).
# Clang's -Wconversion implies -Wsign-conversion while GCC's does not; disable
# it here so both compilers enforce the same (currently clean) warning set.
set(SODIUM_WARNING_FLAGS
    -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wno-sign-conversion -Werror)
target_compile_options(sodium PRIVATE ${SODIUM_WARNING_FLAGS})
target_compile_options(cyan-lsp PRIVATE ${SODIUM_WARNING_FLAGS})

# O1: debug builds run the IR verifier on every function before allocation;
# release builds do so only when --verify-ir is passed.
target_compile_definitions(sodium PRIVATE $<$<CONFIG:Debug>:SODIUM_IR_VERIFY>)
target_compile_definitions(cyan-lsp PRIVATE $<$<CONFIG:Debug>:SODIUM_IR_VERIFY>)

# T4: native C++ unit tests (no network, no external framework).
file(GLOB UNIT_TEST_SOURCES tests/cpp/test_*.cpp)
add_executable(unit_tests
    tests/cpp/main.cpp
    ${UNIT_TEST_SOURCES}
    src/tokenization.cpp
    src/preprocessor.cpp
    src/parser.cpp
    src/ast_printer.cpp
    src/arena.cpp
    sodium-rt/allocator.c
    ${GEN_SOURCES}
    ${BACKEND_SOURCES}
    ${IR_SOURCES}
)
target_include_directories(unit_tests PRIVATE src tests/cpp)
target_compile_options(unit_tests PRIVATE ${SODIUM_WARNING_FLAGS})
