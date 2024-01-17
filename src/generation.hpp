#pragma once

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>

#include "parser.hpp"

class Generator {

public:
    inline explicit Generator(NodeExit root)
    : m_root(std::move(root))
    {

    }

    [[nodiscard]] std::string generate() const
    {
        std::stringstream output;
        output << "gloabl _start\n_start:\n";
        output << "    mov rax, 60\n";
        output << "    mov rdi, " << m_root.expr.int_lit.value.value() << "\n";
        output << "    syscall";
        return output.str();
    }

private:
    const NodeExit m_root;
};