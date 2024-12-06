#include "cosim/serialization.hpp"

#include <ios>
#include <iterator>
#include <utility>
#include <tinycbor/cbor.h>
#include <cosim/log/logger.hpp>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <typeindex>
#include <iomanip>

namespace
{
    inline void throw_parsing_error(const std::string& msg, std::optional<CborError> ce = std::nullopt)
    {
        std::stringstream ss;
        ss << "Error while parsing CBOR: " << msg;
        if (ce) {
            ss << ", cbor error: " << cbor_error_string(*ce);
        }
        throw std::runtime_error(ss.str());
    }

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
    
    constexpr size_t DEFAULT_CBOR_BLOCK_SIZE = 1024;

    class cbor_encoding_data
    {
    public:
        std::shared_ptr<CborEncoder> encoder;
        std::vector<uint8_t>& buf;

        explicit cbor_encoding_data(std::vector<uint8_t>& buf_ref)
            : encoder(std::make_shared<CborEncoder>())
            , buf(buf_ref)
            , parent_(std::make_shared<CborEncoder>())
        {
            cbor_encoder_init(parent_.get(), buf.data(), buf.size(), 0);
            wrap_cbor_encoding(cbor_encoder_create_map, parent_.get(), encoder.get(), CborIndefiniteLength);
            root_ = parent_;
        }

        cbor_encoding_data(const cbor_encoding_data& data)
            : encoder(data.encoder)
            , buf(data.buf)
            , root_(data.root_)
            , parent_(data.parent_)
        {
        }

        ~cbor_encoding_data()
        {
            resize_buffer(128, true); // Always give some extra space to avoid CborErrorOutOfMemory while closing the current container
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

        cbor_encoding_data new_child_map()
        {
            auto map_encoder = std::make_shared<CborEncoder>();
            wrap_cbor_encoding(cbor_encoder_create_map, encoder.get(), map_encoder.get(), CborIndefiniteLength);
            auto new_data = cbor_encoding_data{*this};
            new_data.encoder = map_encoder;
            new_data.parent_ = encoder;
            return new_data;
        }

        cbor_encoding_data new_child_array()
        {
            auto array_encoder = std::make_shared<CborEncoder>();
            wrap_cbor_encoding(cbor_encoder_create_array, encoder.get(), array_encoder.get(), CborIndefiniteLength);
            auto new_data = cbor_encoding_data{*this};
            new_data.encoder = array_encoder;
            new_data.parent_ = encoder;
            return new_data;
        }

        template<typename F, typename... Args>
        CborError wrap_cbor_encoding(F&& f, Args&&... args)
        {
            total_bytes_ = cbor_encoder_get_buffer_size(encoder.get(), buf.data());
            auto err = std::forward<F>(f)(std::forward<Args>(args)...);

            if (err == CborErrorOutOfMemory) {
                BOOST_LOG_SEV(cosim::log::logger(), cosim::log::debug) << "Out of memory while encoding CBOR, resizing a buffer.";
                resize_buffer(DEFAULT_CBOR_BLOCK_SIZE);
                err = std::forward<F>(f)(std::forward<Args>(args)...);
            }

            if (err != CborNoError) {
                throw_parsing_error("encoding error", err);
            }
            return err;
        }

    private:
        void resize_buffer(size_t inc, bool skip_extra = false)
        {
            auto extra_bytes = skip_extra ? 0 : cbor_encoder_get_extra_bytes_needed(encoder.get());
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

    typedef enum IntTags {
        Uint8  = 0x100,
        Uint16 = 0x101,
        Uint32 = 0x102,
        Uint64 = 0x103,
        Int8   = 0x104,
        Int16  = 0x105,
        Int32  = 0x106,
        Int64  = 0x107
    } IntTags;

    constexpr int INT_MASK = 0xfff8;
    constexpr int INT_FLAG = 0x100;

    struct cbor_write_visitor
    {
        explicit cbor_write_visitor(cbor_encoding_data& data)
            : data_(data)
        { }

        cbor_write_visitor() = delete;

        void operator()(std::nullptr_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_null, data_.encoder.get());
        }

        void operator()(std::byte v)
        {
            data_.wrap_cbor_encoding(cbor_encode_simple_value, data_.encoder.get(), static_cast<uint8_t>(v));
        }

        void operator()(const std::vector<std::byte>& blob)
        {
            auto child = data_.new_child_array();
            for (const auto& b : blob) {
                child.wrap_cbor_encoding(cbor_encode_simple_value, child.encoder.get(), static_cast<unsigned char>(b)); 
            }
        }

        void operator()(bool v)
        {
            data_.wrap_cbor_encoding(cbor_encode_boolean, data_.encoder.get(), v);
        }

        void operator()(uint8_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Uint8);
            data_.wrap_cbor_encoding(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int8_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Int8);
            data_.wrap_cbor_encoding(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(uint16_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Uint16);
            data_.wrap_cbor_encoding(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int16_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Int16);
            data_.wrap_cbor_encoding(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(uint32_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Uint32);
            data_.wrap_cbor_encoding(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int32_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Int32);
            data_.wrap_cbor_encoding(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(uint64_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Uint64);
            data_.wrap_cbor_encoding(cbor_encode_uint, data_.encoder.get(), v);
        }

        void operator()(int64_t v)
        {
            data_.wrap_cbor_encoding(cbor_encode_tag, data_.encoder.get(), IntTags::Int64);
            data_.wrap_cbor_encoding(cbor_encode_int, data_.encoder.get(), v);
        }

        void operator()(float v)
        {
            data_.wrap_cbor_encoding(cbor_encode_float, data_.encoder.get(), v);
        }

        void operator()(double v)
        {
            data_.wrap_cbor_encoding(cbor_encode_double, data_.encoder.get(), v);
        }

        void operator()(char v)
        {
            data_.wrap_cbor_encoding(cbor_encode_text_string, data_.encoder.get(), &v, 1);
        }

        void operator()(std::string v)
        {
            data_.wrap_cbor_encoding(cbor_encode_text_string, data_.encoder.get(), v.data(), v.size());
        }

    private:
        cbor_encoding_data& data_;
    };

    void serialize_cbor(cbor_encoding_data& data, const cosim::serialization::node& tree)
    {
        for (auto child = tree.begin(); child != tree.end(); ++child) {
            data.wrap_cbor_encoding(cbor_encode_text_stringz, data.encoder.get(), child->first.c_str());
            if (child->second.begin() != child->second.end()) {
                auto new_data = data.new_child_map();
                serialize_cbor(new_data, child->second);
            } else {
                std::visit(cbor_write_visitor(data), child->second.data());
            }
        }
    }

    template<typename F, typename... Args>
    CborError wrap_cbor_call(F&& f, Args&&... args)
    {
        auto result = std::forward<F>(f)(std::forward<Args>(args)...);
        if (result != CborNoError) {
            throw_parsing_error("decoding error", result);
        }
        return result;
    }
    
    template<typename T, typename F, typename... Args>
    void wrap_cbor_put_simple(cosim::serialization::node& node, char* key, F&& f, Args&&... args)
    {
       T result;
       wrap_cbor_call(std::forward<F>(f), std::forward<Args>(args)..., &result);
       node.put(key, result);
    }

    template<IntTags Tag, typename Enable = void>
    struct CborValueGetter;

    template<IntTags Tag>
    struct CborValueGetter<Tag, typename std::enable_if<(Tag == Uint8 || Tag == Uint16 || Tag == Uint32 || Tag == Uint64)>::type> {
        using CastTo = typename std::conditional<
            Tag == Uint8, uint8_t,
            typename std::conditional<
                Tag == Uint16, uint16_t,
                typename std::conditional<
                    Tag == Uint32, uint32_t,
                    uint64_t
                >::type
            >::type
        >::type;
    };

    template<IntTags Tag>
    struct CborValueGetter<Tag, typename std::enable_if<(Tag == Int8 || Tag == Int16 || Tag == Int32 || Tag == Int64)>::type> {
        using CastTo = typename std::conditional<
            Tag == Int8, int8_t,
            typename std::conditional<
                Tag == Int16, int16_t,
                typename std::conditional<
                    Tag == Int32, int32_t,
                    int64_t
                >::type
            >::type
        >::type;
    };

    template<IntTags Tag, typename std::enable_if<(Tag == Uint8 || Tag == Uint16 || Tag == Uint32 || Tag == Uint64)>::type* = nullptr>
    void wrap_cbor_put_int(cosim::serialization::node& node, char* key, CborValue& value)
    {
        uint64_t int_val;
        wrap_cbor_call(cbor_value_get_uint64, &value, &int_val);
        node.put(key, static_cast<typename CborValueGetter<Tag>::CastTo>(int_val));
    }

    template<IntTags Tag, typename std::enable_if<(Tag == Int8 || Tag == Int16 || Tag == Int32 || Tag == Int64)>::type* = nullptr>
    void wrap_cbor_put_int(cosim::serialization::node& node, char* key, CborValue& value)
    {
        int64_t int_val;
        wrap_cbor_call(cbor_value_get_int64, &value, &int_val);
        node.put(key, static_cast<typename CborValueGetter<Tag>::CastTo>(int_val));
    }

    void read_entry(CborValue& value, cosim::serialization::node& node)
    {
        if(!cbor_value_is_valid(&value)) {
            throw_parsing_error("Invalid CBOR value while parsing map entries!");
        }

        if(!cbor_value_is_text_string(&value)) {
            throw_parsing_error("A map's key is expected to be a string type");
        }

        size_t key_len;
        wrap_cbor_call(cbor_value_get_string_length, &value, &key_len);
        std::vector<char> keyvec(key_len + 1);
        wrap_cbor_call(cbor_value_copy_text_string, &value, keyvec.data(), &key_len, &value);
        keyvec.push_back('\0');
        char* key = keyvec.data();
        std::cout << "Paring " << key << std::endl;

        if (cbor_value_is_tag(&value)) {
            CborTag tag;
            cbor_value_get_tag(&value, &tag);
            cbor_value_advance(&value);
            std::cout << "has tag" << std::endl;

            switch (tag) {
                case Uint8:
                    std::cout << "uint8" << std::endl;
                    wrap_cbor_put_int<Uint8>(node, key, value);
                    break;
                case Uint16:
                    std::cout << "uint16" << std::endl;
                    wrap_cbor_put_int<Uint16>(node, key, value);
                    break;
                case Uint32:
                    std::cout << "uint32"  << std::endl;
                    wrap_cbor_put_int<Uint32>(node, key, value);
                    break;
                case Uint64:
                    std::cout << "uint64"  << std::endl;
                    wrap_cbor_put_int<Uint64>(node, key, value);
                    break;
                case Int8:
                    std::cout << "int8"  << std::endl;
                    wrap_cbor_put_int<Int8>(node, key, value);
                    break;
                case Int16:
                    std::cout << "int16"  << std::endl;
                    wrap_cbor_put_int<Int16>(node, key, value);
                    break;
                case Int32:
                    std::cout << "int32"  << std::endl;
                    wrap_cbor_put_int<Int32>(node, key, value);
                    break;
                case Int64:
                    std::cout << "int64"  << std::endl;
                    wrap_cbor_put_int<Int64>(node, key, value);
                    break;
                default:
                    std::stringstream ss;
                    ss << "Unknown integer type: " << tag;
                    throw_parsing_error(ss.str());
            }
        } else if (cbor_value_is_null(&value) || cbor_value_is_undefined(&value)) {
            node.put(key, std::nullptr_t());
        } else if (cbor_value_is_boolean(&value)) {
            wrap_cbor_put_simple<bool>(node, key, cbor_value_get_boolean, &value);
        } else if (cbor_value_is_double(&value)) {
            wrap_cbor_put_simple<double>(node, key, cbor_value_get_double, &value);
        } else if (cbor_value_is_float(&value)) {
            wrap_cbor_put_simple<float>(node, key, cbor_value_get_float, &value);
        } else if (cbor_value_is_text_string(&value)) {
            size_t len;
            wrap_cbor_call(cbor_value_get_string_length, &value, &len);
            std::vector<char> buf(len);
            wrap_cbor_call(cbor_value_copy_text_string, &value, buf.data(), &len, &value);
            if (len == 0) {
                node.put(key, std::string());
            } else {
                node.put(key, std::string(buf.data()));
            }
            return;
        } else if (cbor_value_is_byte_string(&value)) {
            size_t len;
            wrap_cbor_call(cbor_value_get_string_length, &value, &len);
            std::vector<uint8_t> buf(len);
            wrap_cbor_call(cbor_value_copy_byte_string, &value, buf.data(), &len, &value);
            if (len == 0) {
                node.put(key, std::string());
            } else {
                node.put(key, std::string(reinterpret_cast<char*>(buf.data()), buf.size()));
            }
            return;
        } else if (cbor_value_is_map(&value)) {
            CborValue recur;
            std::cout << "enter cont 2" << std::endl;
            wrap_cbor_call(cbor_value_enter_container, &value, &recur);
            cosim::serialization::node child;
            while (!cbor_value_at_end(&recur)) {
                std::cout << "read next entry" << std::endl;
                read_entry(recur, child);
            }
            node.put_child(key, child);
            wrap_cbor_call(cbor_value_leave_container, &value, &recur);
            std::cout << "leave cont 2" << std::endl;
            return;
        } else if (cbor_value_is_array(&value)) {
            /// For our node_type value, array can only be std::vector<std::byte>
            CborValue recur;
            wrap_cbor_call(cbor_value_enter_container, &value, &recur);
            std::vector<std::byte> buf;
            while (!cbor_value_at_end(&recur)) {
                if (!cbor_value_is_simple_type(&recur)) {
                    throw_parsing_error("node_type array can only be byte type");
                }
                uint8_t byte_;
                wrap_cbor_call(cbor_value_get_simple_type, &value, &byte_);
                buf.push_back(static_cast<std::byte>(byte_));
                wrap_cbor_call(cbor_value_advance, &recur);
            }
            node.put(key, buf);
            wrap_cbor_call(cbor_value_leave_container, &value, &recur);
            return;
        } else {
            std::stringstream ss;
            ss << "Unexpected data type: " << std::hex << std::setw(2) << std::setfill('0') << cbor_value_get_type(&value);
            throw_parsing_error(ss.str());
        }
        std::cout << "IS END " << cbor_value_at_end(&value) << std::endl;
        wrap_cbor_call(cbor_value_advance, &value);
    }

    void parse_cbor(CborValue& value, cosim::serialization::node& tree)
    {
        if (!cbor_value_is_map(&value)) {
            throw_parsing_error("expected a map type");
        }
        
        std::cout << "enter container" << std::endl;

        CborValue map;
        wrap_cbor_call(cbor_value_enter_container, &value, &map);

        while (!cbor_value_at_end(&map)) {
            read_entry(map, tree);
        }
        std::cout << "leave cont" << std::endl;
        wrap_cbor_call(cbor_value_leave_container, &value, &map);
    }
}

std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data)
{
    std::vector<uint8_t> vec(DEFAULT_CBOR_BLOCK_SIZE);
    {
        auto cbor_array = cbor_encoding_data(vec);
        serialize_cbor(cbor_array, data);
    } // To finalize vec by calling cbor_encoding_data's destructor

    out.write(reinterpret_cast<const char*>(vec.data()), vec.size());
    return out;
}


std::istream& operator>>(std::istream& in, cosim::serialization::node& data)
{
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.clear();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    in.read(reinterpret_cast<char*>(buf.data()), size);

    CborParser parser;
    CborValue value;
    cbor_parser_init(buf.data(), size, 0, &parser, &value);
    
    parse_cbor(value, data);
    return in;
}

void print_ptree(std::ostream& out, const cosim::serialization::node& data)
{
    constexpr int indentStep = 2;
    print_tree(out, data, 0, indentStep);
}