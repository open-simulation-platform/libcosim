#include "cosim/serialization.hpp"

#include <ios>
#include <iterator>
#include <utility>
#include <cosim/log/logger.hpp>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <typeindex>
#include <iomanip>
#include <cbor.h>
#include <memory>

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

    constexpr uint64_t TYPE_INT  = 0x8000;
    constexpr uint64_t TYPE_UINT = 0x8001;

    struct cbor_write_visitor
    {
        cbor_write_visitor(cbor_item_t* data, const char* key)
            : data_(data)
            , key_(key)
        {
        }
        void operator()(std::nullptr_t v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_ctrl(CBOR_CTRL_NULL))});
        }

        void operator()(std::byte v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint8(static_cast<uint8_t>(v)))});
        }

        void operator()(const std::vector<std::byte>& blob)
        {
            auto* array = cbor_new_definite_array(blob.size());
            for (const auto& b : blob) {
                cbor_array_push(array, cbor_build_uint8(static_cast<uint8_t>(b)));
            }
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(array)});
        }

        void operator()(bool v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_bool(v))});
        }

        void operator()(uint8_t v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint8(v))});
        }

        void operator()(int8_t v)
        {
            auto value = v < 0 ? cbor_build_negint8(v) : cbor_build_uint8(v);
            value = cbor_build_tag(TYPE_INT, value);
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint16_t v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint16(v))});
        }

        void operator()(int16_t v)
        {
            auto value = v < 0 ? cbor_build_negint16(v) : cbor_build_uint16(v);
            value = cbor_build_tag(TYPE_INT, value);
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint32_t v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint32(v))});
        }

        void operator()(int32_t v)
        {
            auto value = v < 0 ? cbor_build_negint32(v) : cbor_build_uint32(v);
            value = cbor_build_tag(TYPE_INT, value);
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint64_t v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint64(v))});
        }

        void operator()(int64_t v)
        {
            auto value = v < 0 ? cbor_build_negint64(v) : cbor_build_uint64(v);
            value = cbor_build_tag(TYPE_INT, value);
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(float v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_float4(v))});
        }

        void operator()(double v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_float8(v))});
        }

        void operator()(char v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_ctrl(v))});
        }

        void operator()(const std::string& v)
        {
            cbor_map_add(data_, {cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_string(v.c_str()))});
        }
    private:
        cbor_item_t* data_;
        const char* key_;
    };

    void serialize_cbor(cbor_item_t *item, const cosim::serialization::node& tree)
    {
        for (auto child = tree.begin(); child != tree.end(); ++child) {
            if (child->second.begin() != child->second.end()) {
                auto new_map = cbor_new_indefinite_map();
                serialize_cbor(new_map, child->second);
                cbor_map_add(item, {cbor_move(cbor_build_string(child->first.c_str())), cbor_move(new_map)});
            } else {
                std::visit(cbor_write_visitor(item, child->first.c_str()), child->second.data());
            }
        }
    }

    struct cbor_reader_ctx
    {
        enum STATE {
            READING_INDEF_ARRAY = 0x01,
            READING_ARRAY       = 0x02,
            READING_MAP         = 0x03
        };

        cosim::serialization::node& root;
        std::vector<cosim::serialization::node> children{};
        std::vector<std::string> keys{};
        std::optional<uint64_t> tag{};

        /// For FMU states
        std::vector<std::byte> byte_array{};
        size_t byte_array_length{};

        STATE state = READING_MAP;
        int level = -1;
    };

    class cbor_reader
    {
    public:
        using STATE = cbor_reader_ctx::STATE;

        static cosim::serialization::node& current_node(cbor_reader_ctx* ctx)
        {
            if (ctx->children.size() == 0) {
                return ctx->root;
            }
            return ctx->children.back();
        }

        inline static bool current_string_is_key(cbor_reader_ctx* ctx)
        {
            return ctx->children.size() == ctx->keys.size();
        }

        static void cbor_read_tag(void* _ctx, uint64_t tag)
        {
            auto node = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            node->tag = tag;
        }

        static void cbor_read_null_undefined(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            current_node(ctx).put(ctx->keys.back(), nullptr);
            ctx->keys.pop_back();
        }

        static void cbor_indef_map_start(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->level++;
            if (ctx->level > 0) {
                ctx->children.emplace_back();
            }
        }

        static void cbor_indef_break(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            if (ctx->state == STATE::READING_MAP) {
                if (!ctx->keys.empty()) {
                    auto key = ctx->keys.back();
                    auto map = current_node(ctx);
                    if(!ctx->children.empty()) {
                        ctx->children.pop_back();
                    }
                    current_node(ctx).put_child(key, map);
                    ctx->keys.pop_back();
                }
                ctx->level--;
            } else if (ctx->state == STATE::READING_INDEF_ARRAY) {
                current_node(ctx).put(ctx->keys.back(), ctx->byte_array);
                ctx->state = STATE::READING_MAP;
            } else {
                BOOST_LOG_SEV(cosim::log::logger(), cosim::log::warning) << "Unexpected parsing state " << ctx->state;
            }
        }

        static void cbor_read_string(void* _ctx, cbor_data buffer, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            auto value = std::string(reinterpret_cast<const char*>(buffer), len);
            if (current_string_is_key(ctx)) {
                ctx->keys.push_back(value);
            } else {
                current_node(ctx).put(ctx->keys.back(), value);
                ctx->keys.pop_back();
            }
        }

        static void cbor_read_boolean(void* _ctx, bool v)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            current_node(ctx).put(ctx->keys.back(), v);
            ctx->keys.pop_back();
        }

        static void cbor_read_half_float(void* _ctx, float v)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            current_node(ctx).put(ctx->keys.back(), v);
            ctx->keys.pop_back();
        }

        static void cbor_read_double_float(void* _ctx, double v)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            current_node(ctx).put(ctx->keys.back(), v);
            ctx->keys.pop_back();
        }

        static void cbor_read_byte_string(void * _ctx, cbor_data data, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            auto value = std::string(reinterpret_cast<const char*>(data), len);
            current_node(ctx).put(ctx->keys.back(), value);
            ctx->keys.pop_back();
        }

        static void cbor_array_start(void * _ctx, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->state = STATE::READING_ARRAY;
            ctx->byte_array.clear();
            ctx->byte_array_length = len;
        }

        static void cbor_indef_array_start(void * _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->state = STATE::READING_INDEF_ARRAY;
            ctx->byte_array.clear();
        }

        template<typename T>
        struct int_type;

        template<>
        struct int_type<uint8_t> {
           using type = int8_t;
        };

        template<>
        struct int_type<uint16_t> {
            using type = int16_t;
        };

        template<>
        struct int_type<uint32_t> {
            using type = int32_t;
        };

        template<>
        struct int_type<uint64_t> {
            using type = int64_t;
        };

        template<typename T>
        static void cbor_read_int(void* _ctx, T v)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);

            // If tag is defined, read the value as signed integer.
            if (ctx->tag) {
                current_node(ctx).put(ctx->keys.back(), static_cast<typename int_type<T>::type>(v));
                ctx->tag = std::nullopt;
            } else {
                current_node(ctx).put(ctx->keys.back(), v);
            }
            ctx->keys.pop_back();
        }

        static void cbor_read_int(void* _ctx, uint8_t v)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);

            if (ctx->tag) {
                current_node(ctx).put(ctx->keys.back(), static_cast<int8_t>(v));
                ctx->tag = std::nullopt;
            } else if (ctx->state == STATE::READING_ARRAY || ctx->state == STATE::READING_INDEF_ARRAY) {
                // While reading array, cast the value to std::byte.
                ctx->byte_array.push_back(static_cast<std::byte>(v));

                if (ctx->state == STATE::READING_ARRAY && ctx->byte_array.size() == ctx->byte_array_length) {
                    current_node(ctx).put(ctx->keys.back(), ctx->byte_array);
                    ctx->state = STATE::READING_MAP;
                } else {
                    return;
                }
            } else {
                current_node(ctx).put(ctx->keys.back(), v);
            }

            ctx->keys.pop_back();
        }
    };

}

std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data)
{
    auto root = cbor_new_indefinite_map();
    serialize_cbor(root, data);

    unsigned char* buffer;
    size_t buffer_size;

    cbor_serialize_alloc(root, &buffer, &buffer_size);
    out.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(buffer_size));

    free(buffer);
    cbor_decref(&root);
    return out;
}


std::istream& operator>>(std::istream& in, cosim::serialization::node& root)
{
    in.seekg(0, std::ios::end);
    std::streamsize length = in.tellg();
    in.clear();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(length);
    in.read(reinterpret_cast<char*>(buf.data()), length);

    cbor_reader_ctx ctx{root};
    struct cbor_callbacks cbs = cbor_empty_callbacks;
    struct cbor_decoder_result decode_result{};
    auto len = static_cast<size_t>(length);
    size_t bytes_read = 0;

    // Callback functions for reading the input node recursively
    cbs.string = cbor_reader::cbor_read_string;
    cbs.uint8 = cbor_reader::cbor_read_int;
    cbs.uint16 = cbor_reader::cbor_read_int;
    cbs.uint32 = cbor_reader::cbor_read_int;
    cbs.uint64 = cbor_reader::cbor_read_int;
    cbs.negint8 = cbor_reader::cbor_read_int;
    cbs.negint16 = cbor_reader::cbor_read_int;
    cbs.negint32 = cbor_reader::cbor_read_int;
    cbs.negint64 = cbor_reader::cbor_read_int;
    cbs.boolean = cbor_reader::cbor_read_boolean;
    cbs.float4 = cbor_reader::cbor_read_half_float;
    cbs.float8 = cbor_reader::cbor_read_double_float;
    cbs.indef_map_start = cbor_reader::cbor_indef_map_start;
    cbs.indef_break = cbor_reader::cbor_indef_break;
    cbs.undefined = cbor_reader::cbor_read_null_undefined;
    cbs.null = cbor_reader::cbor_read_null_undefined;
    cbs.tag = cbor_reader::cbor_read_tag;
    cbs.byte_string = cbor_reader::cbor_read_byte_string;
    cbs.indef_array_start = cbor_reader::cbor_indef_array_start;
    cbs.array_start = cbor_reader::cbor_array_start;

    while(bytes_read < len) {
        decode_result = cbor_stream_decode(buf.data() + bytes_read, len - bytes_read, &cbs, &ctx);
        if (decode_result.status != cbor_decoder_status::CBOR_DECODER_FINISHED) {
            BOOST_LOG_SEV(cosim::log::logger(), cosim::log::error) << "Decoding error " << decode_result.status;
            return in;
        }
        bytes_read += decode_result.read;
    }
    return in;
}

void print_ptree(std::ostream& out, const cosim::serialization::node& data)
{
    constexpr int indentStep = 2;
    print_tree(out, data, 0, indentStep);
}