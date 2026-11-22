#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <climits>

#include "arena.hpp"
#include "./generation.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "preprocessor.hpp"
#include "backend/interface.hpp"
#include "backend/x86_64/backend.hpp"
#include "backend/riscv64/backend.hpp"
#include "backend/peephole.hpp"

// Temp directory to clean up on exit (set after mkdtemp succeeds)
static std::string g_tmp_dir;

// Run a child process with an argv array — no shell, so paths containing
// spaces, semicolons or other shell metacharacters are passed literally.
// Returns the child's exit code, or -1 if it could not be started.
static int run_child(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv.data());
        std::cerr << "error: failed to execute " << argv[0] << std::endl;
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

static void cleanup_temp_dir() {
    if (g_tmp_dir.empty()) return;
    // The temp directory contains only these three known files.
    for (const char* name : {"out.asm", "out.o", "out"}) {
        std::error_code ec;
        std::filesystem::remove(g_tmp_dir + "/" + name, ec);
    }
    std::error_code ec;
    std::filesystem::remove(g_tmp_dir, ec);
    g_tmp_dir.clear();
}

int main(int argc, char* argv[]) {

    bool print_ast_flag = false;
    bool emit_ir_flag = false;
    bool verify_ir_flag = false;
    bool dump_regalloc_flag = false;
    bool no_alloc_flag = false;
    int opt_level = 1;           // -O1 (default, D-032) or -O0
    bool emit_asm_only = false;     // -S: stop after assembly
    const char* filename = nullptr;
    std::string output_path;         // -o <file>, or derived from input
    std::vector<std::string> include_dirs;
    std::string target = "x86_64";  // default
    std::string cyanlib_dir;         // --cyanlib-dir <path> overrides default
    bool warn_disable = false;       // -w
    bool warn_as_error = false;      // -Werror
    bool color_requested = false;    // -fcolor-diagnostics
    bool color_forced_off = false;   // -fno-color-diagnostics or NO_COLOR
    bool bounds_check = false;       // --bounds
    bool null_check = false;         // --check-null
    bool stack_probe = false;        // --stack-probe

    auto usage = []() {
        std::cerr << "Usage: sodium [--target <arch>] [--print-ast] [--emit-ir] [--verify-ir] [--dump-regalloc] [-O0|-O1] (default -O1) [--no-alloc] [-o <file>] [-S] [-I <dir>] [--cyanlib-dir <dir>] [-w] [-Werror] [-fcolor-diagnostics] [--bounds] [--check-null] [--stack-probe] <filename.cyan>" << std::endl;
    };

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--print-ast") == 0) {
            print_ast_flag = true;
        } else if (std::strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir_flag = true;
        } else if (std::strcmp(argv[i], "--verify-ir") == 0) {
            verify_ir_flag = true;
        } else if (std::strcmp(argv[i], "--dump-regalloc") == 0) {
            dump_regalloc_flag = true;
        } else if (std::strcmp(argv[i], "-O0") == 0) {
            opt_level = 0;
        } else if (std::strcmp(argv[i], "-O1") == 0) {
            opt_level = 1;
        } else if (std::strcmp(argv[i], "--no-alloc") == 0) {
            no_alloc_flag = true;
        } else if (std::strcmp(argv[i], "--bounds") == 0) {
            bounds_check = true;
        } else if (std::strcmp(argv[i], "--check-null") == 0) {
            null_check = true;
        } else if (std::strcmp(argv[i], "--stack-probe") == 0) {
            stack_probe = true;
        } else if (std::strcmp(argv[i], "--target") == 0) {
            if (i + 1 < argc) {
                target = argv[++i];
            } else {
                std::cerr << "--target requires an argument (x86_64 or riscv64)" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "-o requires an argument" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(argv[i], "-S") == 0 || std::strcmp(argv[i], "--emit-asm") == 0) {
            emit_asm_only = true;
        } else if (std::strcmp(argv[i], "-I") == 0 || std::strcmp(argv[i], "--include-dir") == 0) {
            if (i + 1 < argc) {
                include_dirs.push_back(argv[++i]);
            } else {
                usage();
                exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(argv[i], "-w") == 0) {
            warn_disable = true;
        } else if (std::strcmp(argv[i], "-Werror") == 0) {
            warn_as_error = true;
        } else if (std::strcmp(argv[i], "-fcolor-diagnostics") == 0) {
            color_requested = true;
        } else if (std::strcmp(argv[i], "-fno-color-diagnostics") == 0) {
            color_forced_off = true;
        } else if (std::strcmp(argv[i], "--cyanlib-dir") == 0) {
            if (i + 1 < argc) {
                cyanlib_dir = argv[++i];
            } else {
                usage();
                exit(EXIT_FAILURE);
            }
        } else if (filename == nullptr) {
            filename = argv[i];
        } else {
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if (filename == nullptr) {
        usage();
        exit(EXIT_FAILURE);
    }

    // Derive default output path from input filename
    if (output_path.empty()) {
        std::string fn = filename;
        // Strip directory prefix
        auto slash = fn.find_last_of('/');
        std::string base = (slash == std::string::npos) ? fn : fn.substr(slash + 1);
        // Strip .cyan extension
        if (base.size() > 5 && base.substr(base.size() - 5) == ".cyan") {
            base = base.substr(0, base.size() - 5);
        }
        // When emitting assembly only, default to .asm extension
        if (emit_asm_only) {
            output_path = base + ".asm";
        } else {
            output_path = base;
        }
    }

    // Helper: get directory part of a path (like dirname(1), but doesn't modify input).
    auto get_dir = [](const std::string& path) -> std::string {
        auto pos = path.find_last_of('/');
        if (pos == std::string::npos) return ".";
        if (pos == 0) return "/";
        return path.substr(0, pos);
    };

    // Resolve the runtime directory relative to the compiler binary location.
    auto resolve_rt_dir = [&]() -> std::string {
        std::string exe_dir;
        // First, try /proc/self/exe to find the compiler's absolute path
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            exe_dir = get_dir(exe_path);
        } else if (argv[0][0] == '/') {
            // Absolute path from argv[0]
            exe_dir = get_dir(argv[0]);
        } else {
            // Last resort: CWD-relative (original behaviour)
            return (target == "x86_64") ? "sodium-rt" : "sodium-rt/riscv64";
        }

        std::string arch_sub = (target == "x86_64") ? "" : "/riscv64";

        // Check <exe_dir>/sodium-rt/<arch>/sodium-rt.a
        std::string trial = exe_dir + "/sodium-rt" + arch_sub + "/sodium-rt.a";
        std::ifstream f(trial);
        if (f.good()) {
            return exe_dir + "/sodium-rt" + arch_sub;
        }
        // Check <exe_dir>/../sodium-rt (for build-directory layout)
        trial = exe_dir + "/../sodium-rt" + arch_sub + "/sodium-rt.a";
        std::ifstream f2(trial);
        if (f2.good()) {
            return exe_dir + "/../sodium-rt" + arch_sub;
        }

        // Check the original CWD-relative path as a last fallback
        std::string cwd_path = (target == "x86_64") ? "sodium-rt/sodium-rt.a" : "sodium-rt/riscv64/sodium-rt.a";
        std::ifstream f3(cwd_path);
        if (f3.good()) {
            return (target == "x86_64") ? "sodium-rt" : "sodium-rt/riscv64";
        }

        std::cerr << "error: cannot find sodium-rt runtime library (looked in "
                  << exe_dir << "/sodium-rt" << arch_sub << " and ../sodium-rt" << arch_sub << ")" << std::endl;
        exit(EXIT_FAILURE);
    };

    // Select backend and target info
    Backend* backend = nullptr;
    TargetRegisterInfo* tri = nullptr;
    std::vector<std::string> asm_argv, link_argv;
    std::string rt_dir;
    std::string asm_file, obj_file, exe_file;

    if (emit_asm_only) {
        // Write assembly directly to the output path, no intermediates needed
        asm_file = output_path;
    } else {
        // Create a temporary directory for intermediate files (.asm, .o)
        g_tmp_dir = "/tmp/sodium-XXXXXX";
        if (!mkdtemp(g_tmp_dir.data())) {
            std::cerr << "error: failed to create temporary directory" << std::endl;
            exit(EXIT_FAILURE);
        }
        std::atexit(cleanup_temp_dir);

        asm_file = g_tmp_dir + "/out.asm";
        obj_file = g_tmp_dir + "/out.o";
        exe_file = g_tmp_dir + "/out";
    }

    if (target == "x86_64") {
        backend = new X8664Backend();
        tri = new TargetRegisterInfo(TargetRegisterInfo::x86_64_systemv());
        rt_dir = resolve_rt_dir();
        if (!emit_asm_only) {
            asm_argv = {"nasm", "-felf64", asm_file, "-o", obj_file};
            link_argv = {"ld", "-o", exe_file, obj_file, rt_dir + "/sodium-rt.a"};
        }
    } else if (target == "riscv64") {
        backend = new RISCV64Backend();
        tri = new TargetRegisterInfo(TargetRegisterInfo::riscv64_lp64());
        rt_dir = resolve_rt_dir();
        if (!emit_asm_only) {
            asm_argv = {"riscv64-elf-as", "-march=rv64gc", "-mno-relax", "-o", obj_file, asm_file};
            // --no-relax is required: relaxation rewrites auipc/addi pairs
            // that the compiler uses for label addresses, breaking pointer
            // arithmetic. Calls use auipc+jalr now, so the old JAL range limit
            // is gone, but relaxation of data labels is still unsafe.
            link_argv = {"riscv64-elf-gcc", "-nostdlib", "-static", "-Wl,--no-relax",
                         "-o", exe_file, obj_file, rt_dir + "/sodium-rt.a"};
        }
    } else {
        std::cerr << "Unsupported target: " << target << " (use x86_64 or riscv64)" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Add default cyanlib include path (shipped with the compiler).
    // Priority: --cyanlib-dir flag > SODIUM_CYANLIB_DIR env > auto-detect
    if (cyanlib_dir.empty()) {
        const char* env_dir = std::getenv("SODIUM_CYANLIB_DIR");
        if (env_dir) {
            cyanlib_dir = env_dir;
        }
    }
    if (!cyanlib_dir.empty()) {
        include_dirs.push_back(cyanlib_dir);
    } else {
        // Auto-detect: look relative to compiler binary
        std::string exe_dir;
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            exe_dir = get_dir(exe_path);
        } else if (argv[0][0] == '/') {
            exe_dir = get_dir(argv[0]);
        } else {
            exe_dir = ".";
        }
        // Check <exe_dir>/cyanlib (installed layout)
        std::string trial = exe_dir + "/cyanlib/string.cyan";
        std::ifstream f(trial);
        if (f.good()) {
            include_dirs.push_back(exe_dir + "/cyanlib");
        } else {
            // Check <exe_dir>/../cyanlib (build-directory layout)
            trial = exe_dir + "/../cyanlib/string.cyan";
            std::ifstream f2(trial);
            if (f2.good()) {
                include_dirs.push_back(exe_dir + "/../cyanlib");
            }
        }
        // Last resort: check development directory ($HOME/myc/sodium/cyanlib)
        if (include_dirs.empty()) {
            const char* home = std::getenv("HOME");
            if (home) {
                trial = std::string(home) + "/myc/sodium/cyanlib/string.cyan";
                std::ifstream f3(trial);
                if (f3.good()) {
                    include_dirs.push_back(std::string(home) + "/myc/sodium/cyanlib");
                }
            }
        }
    }

    std::string contents;    
    {
        std::stringstream contents_stream;
        std::fstream input(filename, std::ios::in);
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    register_source_file(filename, contents);

    // Run preprocessor
    PreprocessedResult pp_result = preprocess(contents, filename, include_dirs);
    contents = std::move(pp_result.expanded_source);

    g_source_text = contents;
    g_show_code = true;

    Tokenizer tokenizer(std::move(contents), filename);
    std::vector<Token> tokens = tokenizer.tokenize();

    WarnConfig warn_cfg;
    warn_cfg.enabled = !warn_disable;
    warn_cfg.werror = warn_as_error;

    // NO_COLOR (non-empty) wins over -fcolor-diagnostics, per the convention.
    const char* no_color = std::getenv("NO_COLOR");
    bool color_enabled = color_requested && !color_forced_off
                         && !(no_color && *no_color);

    Parser parser(std::move(tokens));
    parser.set_warn_config(warn_cfg);
    parser.set_color(color_enabled);
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
        generator.set_warn_config(warn_cfg);
        generator.set_color(color_enabled);
        generator.set_no_alloc(no_alloc_flag);
        generator.set_emit_ir(emit_ir_flag);
        generator.set_verify_ir(verify_ir_flag);
        generator.set_dump_regalloc(dump_regalloc_flag);
        generator.set_opt_level(opt_level);
        generator.set_bounds_check(bounds_check);
        generator.set_null_check(null_check);
        generator.set_stack_probe(stack_probe);
        (void)generator.gen_prog();  // writes to backend stream
        asm_output = asm_stream.str();
        if (opt_level >= 1) {
            PeepholeStats peephole_stats;
            asm_output = peephole_optimize(asm_output, backend->target_name(), &peephole_stats);
            if (dump_regalloc_flag) {
                std::cerr << "peephole self_moves=" << peephole_stats.self_moves_removed
                          << " dead_moves=" << peephole_stats.dead_moves_removed << "\n";
            }
        }
    }

    // Write assembly file
    {
        // For -S mode, ensure parent directory exists
        if (emit_asm_only) {
            auto out_dir = get_dir(asm_file);
            if (out_dir != ".") {
                std::error_code ec;
                std::filesystem::create_directories(out_dir, ec);
                if (ec) {
                    std::cerr << "error: cannot create output directory " << out_dir << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
        }
        std::fstream file(asm_file, std::ios::out);
        file << asm_output;
    }

    if (emit_asm_only) {
        // -S: stop after writing assembly — no assemble, link, or copy
        delete backend;
        delete tri;
        return EXIT_SUCCESS;
    }

    // Assemble
    {
        int ret = run_child(asm_argv);
        if (ret != 0) {
            std::cerr << "assembly failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Link
    {
        int ret = run_child(link_argv);
        if (ret != 0) {
            std::cerr << "linking failed (exit code " << ret << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Copy the final binary to the output path
    {
        auto out_dir = get_dir(output_path);
        if (out_dir != ".") {
            std::error_code ec;
            std::filesystem::create_directories(out_dir, ec);
            if (ec) {
                std::cerr << "error: cannot create output directory " << out_dir << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        std::error_code ec;
        std::filesystem::copy_file(exe_file, output_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "error: failed to write output binary to " << output_path << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    // Cleanup
    delete backend;
    delete tri;

    return EXIT_SUCCESS;
}
