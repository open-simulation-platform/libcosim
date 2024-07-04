#include "cosim/serialization.hpp"

#include <algorithm>
#include <iterator>


namespace cosim
{
namespace serialization
{
namespace
{
    // Visitor class which prints the value(s) contained in a serialization node
    // to an output stream.
    struct node_output_visitor
    {
        node_output_visitor(std::ostream& out, int firstIndent = 0, int indent = 0)
            : out_(out), firstIndent_(firstIndent), indent_(indent)
        { }

        template<typename T>
        void operator()(T value)
        {
            print_n_spaces(firstIndent_);
            out_ << value;
        }

        void operator()(bool value)
        {
            print_n_spaces(firstIndent_);
            out_ << std::boolalpha << value;
        }

        void operator()(const std::string& str)
        {
            print_n_spaces(firstIndent_);
            out_ << '"' << str << '"';
        }

        void operator()(const array& arr)
        {
            print_n_spaces(firstIndent_);
            out_ << "[\n";
            for (const auto& value : arr) {
                std::visit(node_output_visitor(out_, indent_+2, indent_+2), value);
                out_ << ",\n";
            }
            print_n_spaces(indent_);
            out_ << ']';
        }

        void operator()(const associative_array& aa)
        {
            print_n_spaces(firstIndent_);
            out_ << "{\n";
            for (const auto& [key, value] : aa) {
                print_n_spaces(indent_+2);
                out_ << key << " = ";
                std::visit(node_output_visitor(out_, 0, indent_+2), value);
                out_ << ",\n";
            }
            print_n_spaces(indent_);
            out_ << '}';
        }

        void operator()(const binary_blob& blob)
        {
            print_n_spaces(firstIndent_);
            out_ << "<binary data, " << blob.size() << " bytes>";
        }

    private:
        void print_n_spaces(int n)
        {
            std::fill_n(std::ostream_iterator<char>(out_), n, ' ');
        }

        std::ostream& out_;
        int firstIndent_ = 0;
        int indent_ = 0;
    };
}


std::ostream& operator<<(std::ostream& out, const node& data)
{
    std::visit(node_output_visitor(out), data);
    return out;
}


}} // namespace cosim::serialization
