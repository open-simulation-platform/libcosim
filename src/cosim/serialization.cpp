#include "cosim/serialization.hpp"

#include <algorithm>
#include <ios>
#include <iterator>
#include <utility>


namespace
{
    // Visitor class which prints the value(s) contained in a leaf node to an
    // output stream and returns whether anything was written.
    struct leaf_output_visitor
    {
        leaf_output_visitor(std::ostream& out) : out_(out) { }

        template<typename T>
        bool operator()(T value) { out_ << value; return true; }

        bool operator()(std::nullptr_t) { return false; }

        bool operator()(bool value)
        {
            const auto flags = out_.flags();
            out_ << std::boolalpha << value;
            out_.flags(flags);
            return true;
        }

        bool operator()(char value)
        {
            out_ << '\'' << value << '\'';
            return true;
        }

        bool operator()(const std::string& str)
        {
            out_ << '"' << str << '"';
            return true;
        }

        bool operator()(std::byte value)
        {
            const auto flags = out_.flags();
            out_ << "0x" << std::hex << int(value);
            out_.flags(flags);
            return true;
        }

        bool operator()(const std::vector<std::byte>& blob)
        {
            out_ << "<binary data, " << blob.size() << " bytes>";
            return true;
        }

    private:
        std::ostream& out_;
    };

    void print_n_spaces(std::ostream& out, int n)
    {
        std::fill_n(std::ostream_iterator<char>(out), n, ' ');
    }

    void print_tree(
        std::ostream& out,
        const cosim::serialization::node& tree,
        int indent,
        int indentStep)
    {
        const auto wroteData = std::visit(leaf_output_visitor(out), tree.data());
        auto child = tree.begin();
        if (child != tree.end()) {
            if (wroteData) out << ' ';
            out << "{\n";
            print_n_spaces(out, indent + indentStep);
            out << child->first << " = ";
            print_tree(out, child->second, indent + indentStep, indentStep);
            ++child;
            for (; child != tree.end(); ++child) {
                out << ",\n";
                print_n_spaces(out, indent + indentStep);
                out << child->first << " = ";
                print_tree(out, child->second, indent + indentStep, indentStep);
            }
            out << '\n';
            print_n_spaces(out, indent);
            out << '}';
        }
    }
}


std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data)
{
    constexpr int indentStep = 2;
    print_tree(out, data, 0, indentStep);
    return out;
}
