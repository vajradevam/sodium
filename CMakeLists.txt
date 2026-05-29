cmake_minimum_required(VERSION 3.20)

project(sodium)

set(CMAKE_CXX_STANDARD 20)

add_executable(sodium
    src/main.cpp
    src/arena.cpp
    src/tokenization.cpp
    src/parser.cpp
    src/generation.cpp
    src/ast_printer.cpp
)
