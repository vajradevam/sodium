#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>

#include "./generation.hpp"

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Holy shit. Yeh kya mazak hai?" << std::endl;
        std::cerr << "Correct usage is -> sodium <filename.sm>" << std::endl;
        std::cerr << "Ram Ram!" << std::endl;
    }

    std::string contents;    
    {
        std::stringstream contents_stream;
        std::fstream input(argv[1], std::ios::in);
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    //std::cout << contents << std::endl;
    Tokenizer tokenizer(std::move(contents));
    std::vector<Token> tokens = tokenizer.tokenize();

    Parser parser(std::move(tokens));
    std::optional<NodeExit> tree = parser.parse();

    if (!tree.has_value()) {
        std:std::cerr << "No return statement found" << std::endl;
        exit(EXIT_FAILURE);
    }

    Generator generator(tree.value());
    {
        std::fstream file("../out.asm", std::ios::out);
        file << generator.generate();
    }

    return EXIT_SUCCESS;
}