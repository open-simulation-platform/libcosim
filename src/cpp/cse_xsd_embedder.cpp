#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    std::cout << "Hello world!" << std::endl;
    if (argc != 3) {
        std::cerr << "Wrong number of arguments!" << std::endl;
        std::cout << "csexsdembedder requires 2 arguments:" << std::endl;
        std::cout << " 1: Name of input file" << std::endl;
        std::cout << " 2: Name of output file" << std::endl;
        return -1;
    }

    std::experimental::filesystem::path input = argv[1] ;
    std::experimental::filesystem::path output = argv[2];

    std::cout << " --- ---" << std::endl;
    std::cout << " --- --- Embedding: " << input << " -> " << output << std::endl;
    std::cout << " --- --- Current dir: " << std::experimental::filesystem::current_path() << std::endl;
    std::cout << " --- ---" << std::endl;

    std::ifstream xsd(argv[1]);
    std::string all, line;

    while(std::getline(xsd, line))
        all+=line+"\n";

    std::stringstream ss;
    ss.write((const char*)&all, sizeof(all));

    std::ofstream out_file(argv[2]);
    if (out_file) {
        out_file
        << "#include <iostream>" << std::endl
        << "#include <string>" << std::endl
        << "#define cse_system_structure_xsd \"" << ss.rdbuf() << "\";" << std::endl
        << "std::string get_xsd() {" << std::endl
        << "std::stringstream ss;" << std::endl
        << "ss.read((char*) &cse_system_structure_xsd, sizeof(cse_system_structure_xsd)));" << std::endl
        << "return ret; }" << std::endl
        << std::endl;
    }
    else
    {
        std::cerr << "Failure opening " << argv[2] << std::endl;
        return -1;
    }

    return 0;
}
