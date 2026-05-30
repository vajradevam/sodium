cmake_minimum_required(VERSION 3.20)

project(sodium)

set(CMAKE_CXX_STANDARD 20)

# ── Runtime libraries (x86-64) ─────────────────────────────────────
find_program(NASM nasm)
if(NASM)
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/sodium-rt/x86_64/sodium-rt.a
        COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/sodium-rt
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
        add_custom_command(
            OUTPUT ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64/sodium-rt.a
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/sodium-rt/riscv64
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

add_executable(sodium
    src/main.cpp
    src/arena.cpp
    src/tokenization.cpp
    src/parser.cpp
    src/generation.cpp
    src/ast_printer.cpp
    src/preprocessor.cpp
    ${BACKEND_SOURCES}
    ${IR_SOURCES}
)

add_executable(cyan-lsp
    src/lsp_main.cpp
    src/lsp.cpp
    src/arena.cpp
    src/tokenization.cpp
    src/parser.cpp
    src/generation.cpp
    src/ast_printer.cpp
    src/preprocessor.cpp
    ${BACKEND_SOURCES}
    ${IR_SOURCES}
)
