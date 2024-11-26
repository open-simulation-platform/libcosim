#include "cosim/serialization.hpp"

#include <ios>
#include <iterator>
#include <utility>
#include <tinycbor/cbor.h>
#include <cosim/log/logger.hpp>

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
    
    static constexpr size_t DEFAULT_CBOR_BLOCK_SIZE = 128;

    class cbor_data
    {
    public:
        std::shared_ptr<CborEncoder> encoder;
        std::vector<uint8_t>& buf;

        cbor_data(std::vector<uint8_t>& buf_ref)
            : encoder(std::make_shared<CborEncoder>())
            , buf(buf_ref)
            , parent_(std::make_shared<CborEncoder>())
        {
            cbor_encoder_init(parent_.get(), buf.data(), buf.size(), 0);
            wrap_cbor(cbor_encoder_create_map, parent_.get(), encoder.get(), CborIndefiniteLength);
            root_ = parent_;
        }

        cbor_data(const cbor_data& data)
            : encoder(data.encoder)
            , buf(data.buf)
            , root_(data.root_)
            , parent_(data.parent_)
        {
        }

        ~cbor_data()
        {
            auto err = cbor_encoder_close_container(parent_.get(), encoder.get());
            if (err != CborNoError) {
                std::stringstream ss;
                ss << "Encountered an issue while closing CBOR container: " << cbor_error_string(err);
                BOOST_LOG_SEV(cosim::log::logger(), cosim::log::warning) << ss.str();
            }

            if (root_ == parent_) {
                auto buf_size = cbor_encoder_get_buffer_size(parent_.get(), buf.data());
                buf.resize(buf_size);
            }
        }

        cbor_data new_child_map()
        {
            auto map_encoder = std::make_shared<CborEncoder>();
            wrap_cbor(cbor_encoder_create_map, encoder.get(), map_encoder.get(), CborIndefiniteLength);
            auto new_data = cbor_data{*this};
            new_data.encoder = map_encoder;
            new_data.parent_ = encoder;
            return new_data;
        }

        cbor_data new_child_array()
        {
            auto array_encoder = std::make_shared<CborEncoder>();
            wrap_cbor(cbor_encoder_create_array, encoder.get(), array_encoder.get(), CborIndefiniteLength);
            auto new_data = cbor_data{*this};
            new_data.encoder = array_encoder;
            new_data.parent_ = encoder;
            return new_data;
        }

        template<typename F, typename... Args>
        CborError wrap_cbor(F&& f, Args&&... args)
        {
            total_bytes_ = cbor_encoder_get_buffer_size(encoder.get(), buf.data());
            auto err = std::forward<F>(f)(std::forward<Args>(args)...);

            if (err == CborErrorOutOfMemory) {
                BOOST_LOG_SEV(cosim::log::logger(), cosim::log::debug) << "Out of memory while encoding CBOR, resizing a buffer.";
                resize_buffer(DEFAULT_CBOR_BLOCK_SIZE);
                err = std::forward<F>(f)(std::forward<Args>(args)...);
            }

            if (err != CborNoError) {
                std::stringstream ss;
                ss << "Error while encoding CBOR: " << cbor_error_string(err);
                throw std::runtime_error(ss.str());
            }
            return err;
        }

    private:
        void resize_buffer(size_t inc)
        {
            auto extra_bytes = cbor_encoder_get_extra_bytes_needed(encoder.get());
            auto new_size = buf.size() + extra_bytes + inc;
            buf.resize(new_size);

            // Update the encoder's buffer pointer
            encoder->data.ptr = buf.data() + total_bytes_;
            encoder->end = buf.data() + new_size;
            encoder->remaining = CborIndefiniteLength + 1;
        }

        std::shared_ptr<CborEncoder> root_, parent_;
        size_t total_bytes_{};
    };

    struct cbor_write_visitor
    {
        cbor_write_visitor(cbor_data& data)
            : data_(data)
        { }

        void operator()(std::nullptr_t v)
        {
            data_.wrap_cbor(cbor_encode_null, data_.encoder.get());
        }

        void operator()(std::byte v)
        {
            data_.wrap_cbor(cbor_encode_simple_value, data_.encoder.get(), static_cast<uint8_t>(v));
        }

        void operator()(const std::vector<std::byte>& blob)
        {
            auto child = data_.new_child_array();
            child.wrap_cbor(cbor_encode_byte_string, child.encoder.get(), reinterpret_cast<const uint8_t*>(blob.data()), blob.size());
        }

        void operator()(bool v)
        {
            data_.wrap_cbor(cbor_encode_boolean, data_.encoder.get(), v);
        }

        void operator()(uint8_t v)
        {
            data_.wrap_cbor(cbor_encode_simple_value, data_.encoder.get(), v);
        }

        void operator()(int8_t v)
        {
            data_.wrap_cbor(cbor_encode_simple_value, data_.encoder.get(), v);
        }

        void operator()(uint16_t v)
        {
            data_.wrap_cbor(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int16_t v)
        {
            data_.wrap_cbor(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(uint32_t v)
        {
            data_.wrap_cbor(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int32_t v)
        {
            data_.wrap_cbor(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(uint64_t v)
        {
            data_.wrap_cbor(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int64_t v)
        {
            data_.wrap_cbor(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(float v)
        {
            data_.wrap_cbor(cbor_encode_float, data_.encoder.get(), v);
        }

        void operator()(double v)
        {
            data_.wrap_cbor(cbor_encode_double, data_.encoder.get(), v);
        }

        void operator()(char v)
        {
            data_.wrap_cbor(cbor_encode_text_string, data_.encoder.get(), &v, 1);
        }

        void operator()(std::string v)
        {
            data_.wrap_cbor(cbor_encode_text_stringz, data_.encoder.get(), v.data());
        }

    private:
        cbor_write_visitor() = delete;

        cbor_data& data_;
    };
    void serialize_cbor(cbor_data& data, const cosim::serialization::node& tree)
    {
        for (auto child = tree.begin(); child != tree.end(); ++child) {
            data.wrap_cbor(cbor_encode_text_stringz, data.encoder.get(), child->first.c_str());
            if (child->second.begin() != child->second.end()) {
                auto new_data = data.new_child_map();
                serialize_cbor(new_data, child->second);
            } else {
                std::visit(cbor_write_visitor(data), child->second.data());
            }
        }
    }
}

std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data)
{
    std::vector<uint8_t> vec(DEFAULT_CBOR_BLOCK_SIZE);
    {
        auto cbor_array = cbor_data(vec);
        serialize_cbor(cbor_array, data);
    } // To finalize vec by calling cbor_data's destructor

    out.write(reinterpret_cast<const char*>(vec.data()), vec.size());
    return out;
}

void print_ptree(std::ostream& out, const cosim::serialization::node& data)
{
    constexpr int indentStep = 2;
    print_tree(out, data, 0, indentStep);
}
