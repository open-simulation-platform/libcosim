#include <cosim/log/logger.hpp>
#include <cosim/serialization.hpp>

#include <cbor.h>

#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <unordered_map>
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

    void print_ptree(
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
            print_ptree(out, child->second, indent + indentStep, indentStep);
            ++child;
            for (; child != tree.end(); ++child) {
                out << ",\n";
                print_n_spaces(out, indent + indentStep);
                out << child->first << " = ";
                print_ptree(out, child->second, indent + indentStep, indentStep);
            }
            out << '\n';
            print_n_spaces(out, indent);
            out << '}';
        }
    }

    constexpr uint64_t TYPE_INT  = 0x8000;
    constexpr uint64_t TYPE_UINT = 0x8001;

    template<typename F, typename... Args>
    void wrap_cbor_call(F&& f, const std::string& err_msg, Args&&... args)
    {
        auto result = std::forward<F>(f)(std::forward<Args>(args)...);
        if (!result) {
            throw std::runtime_error("Error occurred while encoding cbor: " + err_msg);
        }
    }

    struct cbor_write_visitor
    {
        cbor_write_visitor(cbor_item_t* data, const char* key)
            : data_(data)
            , key_(key)
        {
        }
        void operator()(std::nullptr_t)
        {
            wrap_cbor_call(cbor_map_add, "adding null to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_ctrl(CBOR_CTRL_NULL))});
        }

        void operator()(std::byte v)
        {
            wrap_cbor_call(cbor_map_add, "adding a byte to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint8(static_cast<uint8_t>(v)))});
        }

        void operator()(const std::vector<std::byte>& blob)
        {
            auto* array = cbor_new_definite_array(blob.size());
            for (const auto& b : blob) {
                wrap_cbor_call(cbor_array_push, "appending a byte to an array", array, cbor_build_uint8(static_cast<uint8_t>(b)));
            }
            wrap_cbor_call(cbor_map_add, "adding a byte array to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(array)});
        }

        void operator()(bool v)
        {
            wrap_cbor_call(cbor_map_add, "adding a boolean to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_bool(v))});
        }

        void operator()(uint8_t v)
        {
            wrap_cbor_call(cbor_map_add, "adding an uint8 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint8(v))});
        }

        void operator()(int8_t v)
        {
            auto value = v < 0 ? cbor_build_negint8(v) : cbor_build_uint8(v);
            value = cbor_build_tag(TYPE_INT, value);
            wrap_cbor_call(cbor_map_add, "adding an int8 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint16_t v)
        {
            wrap_cbor_call(cbor_map_add, "adding an uint16 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint16(v))});
        }

        void operator()(int16_t v)
        {
            auto value = v < 0 ? cbor_build_negint16(v) : cbor_build_uint16(v);
            value = cbor_build_tag(TYPE_INT, value);
            wrap_cbor_call(cbor_map_add, "adding an int16 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint32_t v)
        {
            wrap_cbor_call(cbor_map_add, "adding an uint32 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint32(v))});
        }

        void operator()(int32_t v)
        {
            auto value = v < 0 ? cbor_build_negint32(v) : cbor_build_uint32(v);
            value = cbor_build_tag(TYPE_INT, value);
            wrap_cbor_call(cbor_map_add, "adding an int32 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(uint64_t v)
        {
            wrap_cbor_call(cbor_map_add, "adding an uint64 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_uint64(v))});
        }

        void operator()(int64_t v)
        {
            auto value = v < 0 ? cbor_build_negint64(v) : cbor_build_uint64(v);
            value = cbor_build_tag(TYPE_INT, value);
            wrap_cbor_call(cbor_map_add, "adding an int64 to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(value)});
        }

        void operator()(float v)
        {
            wrap_cbor_call(cbor_map_add, "adding a float to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_float4(v))});
        }

        void operator()(double v)
        {
            wrap_cbor_call(cbor_map_add, "adding a double to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_float8(v))});
        }

        void operator()(char v)
        {
            wrap_cbor_call(cbor_map_add, "adding a char to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_ctrl(v))});
        }

        void operator()(const std::string& v)
        {
            wrap_cbor_call(cbor_map_add, "adding a string to a map", data_, cbor_pair{cbor_move(cbor_build_string(key_)), cbor_move(cbor_build_string(v.c_str()))});
        }
    private:
        cbor_item_t* data_;
        const char* key_;
    };

    void serialize_cbor(cbor_item_t *item, const cosim::serialization::node& tree)
    {
        for (auto child = tree.begin(); child != tree.end(); ++child) {
            if (child->second.begin() != child->second.end()) {
                auto new_map = cbor_new_definite_map(child->second.size());
                serialize_cbor(new_map, child->second);
                wrap_cbor_call(cbor_map_add, "adding a child map", item, cbor_pair{cbor_move(cbor_build_string(child->first.c_str())), cbor_move(new_map)});
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
            READING_MAP         = 0x03,
            READING_INDEF_MAP   = 0x04
        };

        cosim::serialization::node& root;

        // Cbor parser state
        std::vector<STATE> states{};

        std::vector<cosim::serialization::node> children{};
        std::vector<std::string> keys{};
        std::optional<uint64_t> tag{};

        // For storing FMU states
        std::vector<std::byte> byte_array{};
        size_t byte_array_length{};

        // For maps with fixed sizes
        std::vector<uint64_t> map_current_indexes{};
        std::vector<uint64_t> map_sizes{};

        // Indicates current level when parsing maps recursively
        int level = -1;
    };

    namespace serialization
    {

        template<typename T>
        struct int_type;

        template<>
        struct int_type<uint8_t>
        {
            using type = int8_t;
        };

        template<>
        struct int_type<uint16_t>
        {
            using type = int16_t;
        };

        template<>
        struct int_type<uint32_t>
        {
            using type = int32_t;
        };

        template<>
        struct int_type<uint64_t>
        {
            using type = int64_t;
        };

    }

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

        template<typename T>
        static void add_value(cbor_reader_ctx* ctx, T&& value)
        {
            if (ctx->children.size() == ctx->keys.size() - 1) {
                current_node(ctx).put(ctx->keys.back(), value);
                ctx->keys.pop_back();
            } else {
                std::stringstream ss;
                ss << "[Cbor error] Unable to retrieve a key for the value";
                throw std::runtime_error(ss.str());
            }
        }

        inline static bool string_is_key(cbor_reader_ctx* ctx)
        {
            return ctx->children.size() == ctx->keys.size();
        }

        static void append_map_child(cbor_reader_ctx* ctx)
        {
            if (ctx->level > 0 && !ctx->keys.empty()) {
                auto key = ctx->keys.back();
                auto map = current_node(ctx);
                if(!ctx->children.empty()) {
                    ctx->children.pop_back();
                }
                current_node(ctx).put_child(key, map);
                ctx->keys.pop_back();
                ctx->level--;
            }
        }

        inline static void check_map_end(cbor_reader_ctx* ctx)
        {
            if (ctx->states.back() == cbor_reader_ctx::READING_MAP) {
                if (ctx->map_current_indexes.size() != ctx->map_sizes.size() ||
                    !ctx->map_current_indexes.size() || !ctx->map_sizes.size()) {
                    throw std::runtime_error("[Cbor reader] Invalid state while parsing a map - invalid map sizes");
                }
                ctx->map_current_indexes.back()++;

                if (ctx->map_current_indexes.back() == ctx->map_sizes.back()) {
                    ctx->map_current_indexes.pop_back();
                    ctx->map_sizes.pop_back();
                    append_map_child(ctx);
                    ctx->states.pop_back();

                    if (!ctx->states.size()) {
                        throw std::runtime_error("[Cbor reader] Invalid state while parsing a map - empty state vector");
                    }

                    if (ctx->states.back() == STATE::READING_MAP) {
                        check_map_end(ctx);
                    }
                }
            }
        }

        static void cbor_read_tag(void* _ctx, uint64_t tag)
        {
            auto node = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            node->tag = tag;
        }

        static void cbor_indef_map_start(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->level++;

            // Only add a child node if this is not a root node
            if (ctx->level > 0) {
                ctx->children.emplace_back();
            }
            ctx->states.emplace_back(STATE::READING_INDEF_MAP);
        }

        static void cbor_map_start(void* _ctx, uint64_t size)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->level++;
            if (ctx->level > 0) {
                ctx->children.emplace_back();
            }
            ctx->states.emplace_back(cbor_reader_ctx::READING_MAP);
            ctx->map_sizes.emplace_back(size);
            ctx->map_current_indexes.emplace_back(0);
        }

        static void cbor_indef_break(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            if (ctx->states.back() == STATE::READING_INDEF_MAP) {
                append_map_child(ctx);
            } else if (ctx->states.back() == STATE::READING_INDEF_ARRAY) {
                add_value(ctx, ctx->byte_array);
            } else {
                std::stringstream ss;
                ss << "[Cbor reader] Unexpected parsing state at indefinite item break point: "
                   << ctx->states.back();
                throw std::runtime_error(ss.str());
            }
            ctx->states.pop_back();
        }

        static void cbor_array_start(void * _ctx, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->states.emplace_back(STATE::READING_ARRAY);
            ctx->byte_array.clear();
            ctx->byte_array_length = len;
        }

        static void cbor_indef_array_start(void * _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            ctx->states.emplace_back(STATE::READING_INDEF_ARRAY);
            ctx->byte_array.clear();
        }

        static void cbor_read_null_undefined(void* _ctx)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            add_value(ctx, nullptr);
            check_map_end(ctx);
        }

        static void cbor_read_string(void* _ctx, cbor_data buffer, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            auto value = std::string(reinterpret_cast<const char*>(buffer), len);

            if (string_is_key(ctx)) {
                ctx->keys.push_back(value);
            } else {
                add_value(ctx, value);

                check_map_end(ctx);
            }
        }

        static void cbor_read_boolean(void* _ctx, bool value)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            add_value(ctx, value);
            check_map_end(ctx);
        }

        static void cbor_read_half_float(void* _ctx, float value)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            add_value(ctx, value);
            check_map_end(ctx);
        }

        static void cbor_read_double_float(void* _ctx, double value)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            add_value(ctx, value);
            check_map_end(ctx);
        }

        static void cbor_read_byte_string(void * _ctx, cbor_data data, uint64_t len)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);
            auto value = std::string(reinterpret_cast<const char*>(data), len);
            add_value(ctx, value);
            check_map_end(ctx);
        }

        template<typename T>
        static void cbor_read_int(void* _ctx, T value)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);

            // If tag is defined, read the value as signed integer.
            if (ctx->tag) {
                add_value(ctx, static_cast<typename ::serialization::int_type<T>::type>(value));
                ctx->tag = std::nullopt;
            } else {
                add_value(ctx, value);
            }

            check_map_end(ctx);
        }

        static void cbor_read_int(void* _ctx, uint8_t value)
        {
            auto ctx = reinterpret_cast<cbor_reader_ctx*>(_ctx);

            if (ctx->tag) {
                add_value(ctx, static_cast<int8_t>(value));
                ctx->tag = std::nullopt;
            } else if (ctx->states.back() == STATE::READING_INDEF_ARRAY) {
                ctx->byte_array.push_back(static_cast<std::byte>(value));
                return;
            } else if (ctx->states.back() == STATE::READING_ARRAY) {
                ctx->byte_array.push_back(static_cast<std::byte>(value));
                if (ctx->byte_array.size() == ctx->byte_array_length) {
                    add_value(ctx, ctx->byte_array);
                    ctx->states.pop_back();
                } else {
                    return;
                }
            } else {
                add_value(ctx, value);
            }

            check_map_end(ctx);
        }
    };

    std::ostream& serialize_cbor(std::ostream &out, const cosim::serialization::node& data)
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

    std::istream& deserialize_cbor(std::istream &in, cosim::serialization::node& root)
    {
        in.seekg(0, std::ios::end);
        std::streamsize length = in.tellg();
        in.clear();
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(length);
        in.read(reinterpret_cast<char*>(buf.data()), length);

        // Initializing the reader context object
        cbor_reader_ctx ctx{root};

        cbor_callbacks cbs = cbor_empty_callbacks;
        cbor_decoder_result decode_result{};
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
        cbs.map_start = cbor_reader::cbor_map_start;
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
                const auto err_msg = "[Cbor reader] Decoding error. CBOR decoder status:  " + std::to_string(decode_result.status);
                BOOST_LOG_SEV(cosim::log::logger(), cosim::log::error) << err_msg;
                throw std::runtime_error(err_msg);
            }
            bytes_read += decode_result.read;
        }

        return in;
    }
}

namespace cosim
{
namespace serialization
{
namespace format
{

std::ios_base& cbor(std::ios_base& os)
{
    os.iword(format_xalloc) = FORMAT_CBOR;
    return os;
}

std::ios_base& pretty_print(std::ios_base& os)
{
    os.iword(format_xalloc) = FORMAT_PRETTY_PRINT;
    return os;
}

} // namespace format
} // namespace serialization
} // namespace cosim


std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data)
{
    const auto format = out.iword(cosim::serialization::format::format_xalloc);

    if (format == cosim::serialization::format::FORMAT_CBOR) {
        return serialize_cbor(out, data);
    } else if (format == cosim::serialization::format::FORMAT_PRETTY_PRINT) {
        constexpr int indentStep = 2;
        print_ptree(out, data, 0, indentStep);
        return out;
    }

    // Default fallback
    BOOST_LOG_SEV(cosim::log::logger(), cosim::log::warning) << "Unknown serialization format, falling back to CBOR";
    return serialize_cbor(out, data);
}


std::istream& operator>>(std::istream& in, cosim::serialization::node& root)
{
    const auto format = in.iword(cosim::serialization::format::format_xalloc);

    if (format == cosim::serialization::format::FORMAT_CBOR) {
        return deserialize_cbor(in, root);
    }

    // Default fallback
    BOOST_LOG_SEV(cosim::log::logger(), cosim::log::warning) << "Unknown deserialization format, falling back to CBOR";
    return deserialize_cbor(in, root);
}
