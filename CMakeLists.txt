cmake_minimum_required(VERSION 3.20)

project(sodium)

set(CMAKE_CXX_STANDARD 20)

set(BACKEND_SOURCES
    src/backend/x86_64/backend.cpp
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
