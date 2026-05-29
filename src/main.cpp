#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>

#include "arena.hpp"
#include "./generation.hpp"
#include "parser.hpp"

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Usage: sodium <filename.cyan>" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string contents;    
    {
        std::stringstream contents_stream;
        std::fstream input(argv[1], std::ios::in);
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    Tokenizer tokenizer(std::move(contents));
    std::vector<Token> tokens = tokenizer.tokenize();

    Parser parser(std::move(tokens));
    std::optional<NodeProg> prog = parser.parse_prog();

    if (!prog.has_value()) {
        std::cerr << "Invalid Program" << std::endl;
        exit(EXIT_FAILURE);
    }

    Generator generator(prog.value());
    {
        std::fstream file("./out.asm", std::ios::out);
        file << generator.gen_prog();
    }

    system("nasm -felf64 ./out.asm");
    system("ld -o out ./out.o");

    return EXIT_SUCCESS;
}
