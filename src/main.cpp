#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>
#include <cstring>

#include "arena.hpp"
#include "./generation.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "preprocessor.hpp"

int main(int argc, char* argv[]) {

    bool print_ast_flag = false;
    const char* filename = nullptr;
    std::vector<std::string> include_dirs;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--print-ast") == 0) {
            print_ast_flag = true;
        } else if (std::strcmp(argv[i], "-I") == 0 || std::strcmp(argv[i], "--include-dir") == 0) {
            if (i + 1 < argc) {
                include_dirs.push_back(argv[++i]);
            } else {
                std::cerr << "Usage: sodium [--print-ast] [-I <dir>] <filename.cyan>" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else if (filename == nullptr) {
            filename = argv[i];
        } else {
            std::cerr << "Usage: sodium [--print-ast] [-I <dir>] <filename.cyan>" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    if (filename == nullptr) {
        std::cerr << "Usage: sodium [--print-ast] [-I <dir>] <filename.cyan>" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string contents;    
    {
        std::stringstream contents_stream;
        std::fstream input(filename, std::ios::in);
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    // Run preprocessor
    PreprocessedResult pp_result = preprocess(contents, filename, include_dirs);
    contents = std::move(pp_result.expanded_source);

    g_source_text = contents;
    g_show_code = true;

    Tokenizer tokenizer(std::move(contents), filename);
    std::vector<Token> tokens = tokenizer.tokenize();

    Parser parser(std::move(tokens));
    std::optional<NodeProg> prog = parser.parse_prog();

    if (!prog.has_value()) {
        std::cerr << "Invalid Program" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (print_ast_flag) {
        dump_ast(prog.value());
    }

    Generator generator(prog.value());
    {
        std::fstream file("./out.asm", std::ios::out);
        file << generator.gen_prog();
    }

    {
        int ret = system("nasm -felf64 ./out.asm");
        if (ret != 0) {
            std::cerr << "nasm assembly failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    {
        int ret = system("ld -o out ./out.o sodium-rt/sodium-rt.a");
        if (ret != 0) {
            std::cerr << "linking failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    return EXIT_SUCCESS;
}
