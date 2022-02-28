#include <fstream>
#include <iostream>
#include <sstream>

std::string encode(std::string& data)
{
    std::string buffer;
    buffer.reserve(data.size());
    for (size_t pos = 0; pos != data.size(); ++pos) {
        switch (data[pos]) {
            case '&': buffer.append("&amp;"); break;
            case '\"': buffer.append("&quot;"); break;
            case '\'': buffer.append("&apos;"); break;
            case '<': buffer.append("&lt;"); break;
            case '>': buffer.append("&gt;"); break;
            case '\r': break;
            default: buffer.append(&data[pos], 1); break;
        }
    }
    return buffer;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Wrong number of arguments!" << std::endl;
        std::cout << "ospxsdembedder requires 2 arguments:" << std::endl;
        std::cout << " 1: Name of input file" << std::endl;
        std::cout << " 2: Name of output file" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string output = argv[2];

    std::cout << " Embedding: " << input << " -> " << output << std::endl;

    std::ifstream xsd(input);
    std::string xsd_str, line;

    while (std::getline(xsd, line)) {
        xsd_str += line;
    }

    std::string encoded_xsd_str = encode(xsd_str);

    std::ofstream out_file(output);
    if (out_file) {
        out_file
            << "#include <string>" << std::endl
            << "#include <regex>" << std::endl
            << "#define osp_system_structure_xsd \"" << encoded_xsd_str << "\"" << std::endl
            << std::endl
            << "namespace cosim" << std::endl
            << "{" << std::endl
            << "std::string get_embedded_osp_config_xsd() {" << std::endl
            << "  std::string xsd_str(osp_system_structure_xsd);" << std::endl
            << "  xsd_str = std::regex_replace(xsd_str, std::regex(\"&amp;\"), \"&\");" << std::endl
            << "  xsd_str = std::regex_replace(xsd_str, std::regex(\"&quot;\"), \"\\\"\");" << std::endl
            << "  xsd_str = std::regex_replace(xsd_str, std::regex(\"&apos;\"), \"\'\");" << std::endl
            << "  xsd_str = std::regex_replace(xsd_str, std::regex(\"&lt;\"), \"<\");" << std::endl
            << "  xsd_str = std::regex_replace(xsd_str, std::regex(\"&gt;\"), \">\");" << std::endl
            << "  return xsd_str;" << std::endl
            << "}" << std::endl
            << "}" << std::endl;
    } else {
        std::cerr << "Failure opening " << output << std::endl;
        return 1;
    }

    return 0;
}
