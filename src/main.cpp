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
#include "backend/interface.hpp"
#include "backend/x86_64/backend.hpp"
#include "backend/riscv64/backend.hpp"

int main(int argc, char* argv[]) {

    bool print_ast_flag = false;
    bool no_alloc_flag = false;
    const char* filename = nullptr;
    std::vector<std::string> include_dirs;
    std::string target = "x86_64";  // default

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--print-ast") == 0) {
            print_ast_flag = true;
        } else if (std::strcmp(argv[i], "--no-alloc") == 0) {
            no_alloc_flag = true;
        } else if (std::strcmp(argv[i], "--target") == 0) {
            if (i + 1 < argc) {
                target = argv[++i];
            } else {
                std::cerr << "--target requires an argument (x86_64 or riscv64)" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(argv[i], "-I") == 0 || std::strcmp(argv[i], "--include-dir") == 0) {
            if (i + 1 < argc) {
                include_dirs.push_back(argv[++i]);
            } else {
                std::cerr << "Usage: sodium [--target <arch>] [--print-ast] [--no-alloc] [-I <dir>] <filename.cyan>" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else if (filename == nullptr) {
            filename = argv[i];
        } else {
            std::cerr << "Usage: sodium [--target <arch>] [--print-ast] [--no-alloc] [-I <dir>] <filename.cyan>" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    if (filename == nullptr) {
        std::cerr << "Usage: sodium [--target <arch>] [--print-ast] [--no-alloc] [-I <dir>] <filename.cyan>" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Select backend and target info
    Backend* backend = nullptr;
    TargetRegisterInfo* tri = nullptr;
    std::string asm_file = "out.asm";
    std::string obj_file = "out.o";
    std::string asm_cmd, link_cmd, rt_dir;

    if (target == "x86_64") {
        backend = new X8664Backend();
        tri = new TargetRegisterInfo(TargetRegisterInfo::x86_64_systemv());
        asm_cmd = "nasm -felf64 " + asm_file + " -o " + obj_file;
        link_cmd = "ld -o out " + obj_file + " sodium-rt/sodium-rt.a";
        rt_dir = "sodium-rt";
    } else if (target == "riscv64") {
        backend = new RISCV64Backend();
        tri = new TargetRegisterInfo(TargetRegisterInfo::riscv64_lp64());
        asm_cmd = "riscv64-elf-as -march=rv64gc -o " + obj_file + " " + asm_file;
        link_cmd = "riscv64-elf-gcc -nostdlib -static -o out " + obj_file + " sodium-rt/riscv64/sodium-rt.a";
        rt_dir = "sodium-rt/riscv64";
    } else {
        std::cerr << "Unsupported target: " << target << " (use x86_64 or riscv64)" << std::endl;
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

    // Generate code
    std::string asm_output;
    {
        std::stringstream asm_stream;
        backend->set_output(asm_stream);

        Generator generator(prog.value(), *backend, *tri);
        generator.set_no_alloc(no_alloc_flag);
        generator.gen_prog();

        asm_output = asm_stream.str();
    }

    // Write assembly file
    {
        std::fstream file(asm_file, std::ios::out);
        file << asm_output;
    }

    // Assemble
    {
        int ret = system(asm_cmd.c_str());
        if (ret != 0) {
            std::cerr << "assembly failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Link
    {
        int ret = system(link_cmd.c_str());
        if (ret != 0) {
            std::cerr << "linking failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Cleanup
    delete backend;
    delete tri;

    return EXIT_SUCCESS;
}
