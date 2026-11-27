#include <cosim/log/logger.hpp>
#include <cosim/serialization.hpp>

#include <boost/log/sources/severity_feature.hpp>

#include <algorithm>
#include <cbor.h> // IWYU pragma: keep
#include <cbor/arrays.h>
#include <cbor/callbacks.h>
#include <cbor/common.h>
#include <cbor/data.h>
#include <cbor/floats_ctrls.h>
#include <cbor/ints.h>
#include <cbor/maps.h>
#include <cbor/serialization.h>
#include <cbor/streaming.h>
#include <cbor/strings.h>
#include <cbor/tags.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
// Visitor class which prints the value(s) contained in a leaf node to an
// output stream and returns whether anything was written.
struct leaf_output_visitor
{
    explicit leaf_output_visitor(std::ostream& out)
        : out_(out)
    { }

    template<typename T>
    bool operator()(T value)
    {
        out_ << value;
        return true;
    }

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
        out_ << "0x" << std::hex << static_cast<int>(value);
        out_.flags(flags);
        return true;
    }

    bool operator()(const std::vector<std::byte>& blob)
    {
        out_ << "<binary data, " << blob.size() << " bytes>";
        return true;
    }

private:
    std::ostream& out_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): The visitor borrows the output stream.
};

void print_n_spaces(std::ostream& out, int count)
{
    std::fill_n(std::ostream_iterator<char>(out), count, ' ');
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
        if (wroteData) {
            out << ' ';
        }
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

constexpr uint64_t TYPE_INT = 0x8000;

struct cbor_item_deleter
{
    void operator()(cbor_item_t* item) const noexcept
    {
        cbor_decref(&item);
    }
};

using cbor_item_ptr = std::unique_ptr<cbor_item_t, cbor_item_deleter>;

cbor_item_ptr own_cbor_item(cbor_item_t* item)
{
    if (item == nullptr) {
        throw std::bad_alloc();
    }
    return cbor_item_ptr(item);
}

void add_cbor_field(cbor_item_t* map, const char* name, cbor_item_ptr value)
{
    const auto key = own_cbor_item(cbor_build_string(name));
    if (!cbor_map_add(map, {key.get(), value.get()})) {
        throw std::runtime_error("Could not add a field to a CBOR map");
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
        add(cbor_build_ctrl(CBOR_CTRL_NULL));
    }

    void operator()(std::byte value)
    {
        add(cbor_build_uint8(static_cast<uint8_t>(value)));
    }

    void operator()(const std::vector<std::byte>& blob)
    {
        auto array = own_cbor_item(cbor_new_definite_array(blob.size()));
        for (const auto& element : blob) {
            const auto byte = own_cbor_item(cbor_build_uint8(static_cast<uint8_t>(element)));
            if (!cbor_array_push(array.get(), byte.get())) {
                throw std::runtime_error("Could not append a byte to a CBOR array");
            }
        }
        add_cbor_field(data_, key_, std::move(array));
    }

    void operator()(bool value)
    {
        add(cbor_build_bool(value));
    }

    void operator()(uint8_t value)
    {
        add(cbor_build_uint8(value));
    }

    void operator()(int8_t value)
    {
        add_signed(value < 0 ? cbor_build_negint8(value) : cbor_build_uint8(value));
    }

    void operator()(uint16_t value)
    {
        add(cbor_build_uint16(value));
    }

    void operator()(int16_t value)
    {
        add_signed(value < 0 ? cbor_build_negint16(value) : cbor_build_uint16(value));
    }

    void operator()(uint32_t value)
    {
        add(cbor_build_uint32(value));
    }

    void operator()(int32_t value)
    {
        add_signed(value < 0 ? cbor_build_negint32(value) : cbor_build_uint32(value));
    }

    void operator()(uint64_t value)
    {
        add(cbor_build_uint64(value));
    }

    void operator()(int64_t value)
    {
        add_signed(value < 0 ? cbor_build_negint64(value) : cbor_build_uint64(value));
    }

    void operator()(float value)
    {
        add(cbor_build_float4(value));
    }

    void operator()(double value)
    {
        add(cbor_build_float8(value));
    }

    void operator()(char value)
    {
        add(cbor_build_ctrl(value));
    }

    void operator()(const std::string& value)
    {
        add(cbor_build_string(value.c_str()));
    }

private:
    void add(cbor_item_t* value)
    {
        add_cbor_field(data_, key_, own_cbor_item(value));
    }

    void add_signed(cbor_item_t* value)
    {
        const auto payload = own_cbor_item(value);
        add(cbor_build_tag(TYPE_INT, payload.get()));
    }

    cbor_item_t* data_;
    const char* key_;
};

void serialize_cbor(cbor_item_t* item, const cosim::serialization::node& tree)
{
    for (auto child = tree.begin(); child != tree.end(); ++child) {
        if (child->second.begin() != child->second.end()) {
            auto map = own_cbor_item(cbor_new_definite_map(child->second.size()));
            serialize_cbor(map.get(), child->second);
            add_cbor_field(item, child->first.c_str(), std::move(map));
        } else {
            std::visit(cbor_write_visitor(item, child->first.c_str()), child->second.data());
        }
    }
}

struct cbor_reader_ctx
{
    enum class STATE // NOLINT(performance-enum-size): Preserve the existing enum size.
    {
        READING_INDEF_ARRAY = 0x01,
        READING_ARRAY = 0x02,
        READING_MAP = 0x03,
        READING_INDEF_MAP = 0x04,
    };

    cosim::serialization::node& root; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): The CBOR reader writes into the caller's node.

    // Cbor parser state
    std::vector<STATE> states{}; // NOLINT(readability-redundant-member-init)

    std::vector<cosim::serialization::node> children{}; // NOLINT(readability-redundant-member-init)
    std::vector<std::string> keys{}; // NOLINT(readability-redundant-member-init)
    std::optional<uint64_t> tag{}; // NOLINT(readability-redundant-member-init)

    // For storing FMU states
    std::vector<std::byte> byte_array{}; // NOLINT(readability-redundant-member-init)
    size_t byte_array_length{};

    // For maps with fixed sizes
    std::vector<uint64_t> map_current_indexes{}; // NOLINT(readability-redundant-member-init)
    std::vector<uint64_t> map_sizes{}; // NOLINT(readability-redundant-member-init)

    // Indicates current level when parsing maps recursively
    int level = -1;

    std::exception_ptr callbackError = nullptr;
};

// Exceptions must not propagate through libcbor's C decoder.
template<typename F>
void guarded(void* context, F&& callback) noexcept
{
    auto* ctx = static_cast<cbor_reader_ctx*>(context);
    if (ctx->callbackError) {
        return;
    }
    try {
        std::forward<F>(callback)();
    } catch (...) {
        ctx->callbackError = std::current_exception();
    }
}

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

} // namespace serialization

class cbor_reader
{
public:
    using STATE = cbor_reader_ctx::STATE;

    static STATE top_state(const cbor_reader_ctx* ctx)
    {
        if (ctx->states.empty()) {
            throw std::runtime_error("[Cbor reader] Invalid input: unexpected item outside a map or array");
        }
        return ctx->states.back();
    }

    static cosim::serialization::node& current_node(cbor_reader_ctx* ctx)
    {
        if (ctx->children.empty()) {
            return ctx->root;
        }
        return ctx->children.back();
    }

    // PropertyTree's path-based access splits keys on '.' and rejects empty paths.
    static cosim::serialization::node& child_at(
        cosim::serialization::node& parent, const std::string& key)
    {
        const auto existing = parent.find(key);
        if (existing != parent.not_found()) {
            return existing->second;
        }
        return parent.push_back({key, cosim::serialization::node()})->second;
    }

    template<typename T>
    static void add_value(cbor_reader_ctx* ctx, const T& value)
    {
        if (ctx->keys.size() == ctx->children.size() + 1) {
            child_at(current_node(ctx), ctx->keys.back()).put_value(value);
            ctx->keys.pop_back();
        } else {
            std::stringstream message;
            message << "[Cbor error] Unable to retrieve a key for the value";
            throw std::runtime_error(message.str());
        }
    }

    static bool string_is_key(cbor_reader_ctx* ctx)
    {
        return ctx->children.size() == ctx->keys.size();
    }

    static void append_map_child(cbor_reader_ctx* ctx)
    {
        if (ctx->level > 0 && !ctx->keys.empty()) {
            const auto key = ctx->keys.back();
            const auto map = current_node(ctx);
            if (!ctx->children.empty()) {
                ctx->children.pop_back();
            }
            child_at(current_node(ctx), key) = map;
            ctx->keys.pop_back();
            ctx->level--;
        }
    }

    static void check_map_end(cbor_reader_ctx* ctx)
    {
        if (top_state(ctx) == STATE::READING_MAP) {
            if (ctx->map_current_indexes.size() != ctx->map_sizes.size() ||
                ctx->map_current_indexes.empty() || ctx->map_sizes.empty()) {
                throw std::runtime_error("[Cbor reader] Invalid state while parsing a map - invalid map sizes");
            }
            ctx->map_current_indexes.back()++;

            if (ctx->map_current_indexes.back() == ctx->map_sizes.back()) {
                ctx->map_current_indexes.pop_back();
                ctx->map_sizes.pop_back();
                append_map_child(ctx);
                ctx->states.pop_back();

                if (ctx->states.empty()) {
                    throw std::runtime_error("[Cbor reader] Invalid state while parsing a map - empty state vector");
                }

                if (top_state(ctx) == STATE::READING_MAP) {
                    check_map_end(ctx);
                }
            }
        }
    }

    static void cbor_read_tag(void* _ctx, uint64_t tag)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        top_state(ctx);
        ctx->tag = tag;
    }

    static void cbor_indef_map_start(void* _ctx)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        ctx->level++;

        // Only add a child node if this is not a root node
        if (ctx->level > 0) {
            ctx->children.emplace_back();
        }
        ctx->states.emplace_back(STATE::READING_INDEF_MAP);
    }

    static void cbor_map_start(void* _ctx, uint64_t size)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        ctx->level++;
        if (ctx->level > 0) {
            ctx->children.emplace_back();
        }
        ctx->states.emplace_back(STATE::READING_MAP);
        ctx->map_sizes.emplace_back(size);
        ctx->map_current_indexes.emplace_back(0);
    }

    static void cbor_indef_break(void* _ctx)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        const auto state = top_state(ctx);
        if (state == STATE::READING_INDEF_MAP) {
            append_map_child(ctx);
        } else if (state == STATE::READING_INDEF_ARRAY) {
            add_value(ctx, ctx->byte_array);
        } else {
            std::stringstream message;
            message << "[Cbor reader] Unexpected parsing state at indefinite item break point: "
                    << static_cast<int>(state);
            throw std::runtime_error(message.str());
        }
        ctx->states.pop_back();
    }

    static void cbor_array_start(void* _ctx, uint64_t len)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        top_state(ctx);
        ctx->byte_array.clear();
        if (len == 0) {
            add_value(ctx, ctx->byte_array);
            check_map_end(ctx);
            return;
        }
        ctx->states.emplace_back(STATE::READING_ARRAY);
        ctx->byte_array_length = len;
    }

    static void cbor_indef_array_start(void* _ctx)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        top_state(ctx);
        ctx->states.emplace_back(STATE::READING_INDEF_ARRAY);
        ctx->byte_array.clear();
    }

    static void cbor_read_null_undefined(void* _ctx)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        add_value(ctx, nullptr);
        check_map_end(ctx);
    }

    static void cbor_read_string(void* _ctx, cbor_data buffer, uint64_t len)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        top_state(ctx);
        const auto value = std::string(reinterpret_cast<const char*>(buffer), len); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): CBOR supplies byte buffers.

        if (string_is_key(ctx)) {
            ctx->keys.push_back(value);
        } else {
            add_value(ctx, value);

            check_map_end(ctx);
        }
    }

    static void cbor_read_boolean(void* _ctx, bool value)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        add_value(ctx, value);
        check_map_end(ctx);
    }

    static void cbor_read_half_float(void* _ctx, float value)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        add_value(ctx, value);
        check_map_end(ctx);
    }

    static void cbor_read_double_float(void* _ctx, double value)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        add_value(ctx, value);
        check_map_end(ctx);
    }

    static void cbor_read_byte_string(void* _ctx, cbor_data data, uint64_t len)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);
        const auto value = std::string(reinterpret_cast<const char*>(data), len); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): CBOR supplies byte buffers.
        add_value(ctx, value);
        check_map_end(ctx);
    }

    template<typename T>
    static void cbor_read_int(void* _ctx, T value)
    {
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);

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
        auto* ctx = static_cast<cbor_reader_ctx*>(_ctx);

        if (ctx->tag) {
            add_value(ctx, static_cast<int8_t>(value));
            ctx->tag = std::nullopt;
        } else if (top_state(ctx) == STATE::READING_INDEF_ARRAY) {
            ctx->byte_array.push_back(static_cast<std::byte>(value));
            return;
        } else if (top_state(ctx) == STATE::READING_ARRAY) {
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

std::ostream& serialize_cbor(std::ostream& out, const cosim::serialization::node& data)
{
    const auto root = own_cbor_item(cbor_new_indefinite_map());
    serialize_cbor(root.get(), data);

    unsigned char* buffer = nullptr;
    const auto size = cbor_serialize_alloc(root.get(), &buffer, nullptr);
    const std::unique_ptr<unsigned char, decltype(&std::free)> bytes(buffer, &std::free);
    if (size == 0) {
        throw std::runtime_error("Could not serialize the CBOR state");
    }
    return out.write(reinterpret_cast<const char*>(bytes.get()), static_cast<std::streamsize>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Streams write the CBOR byte buffer as char data.
}

std::istream& deserialize_cbor(std::istream& input, cosim::serialization::node& root)
{
    input.seekg(0, std::ios::end);
    const std::streamsize length = input.tellg();
    if (length <= 0) {
        throw std::runtime_error("[Cbor reader] No data");
    }
    input.clear();
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(length);
    if (!input.read(reinterpret_cast<char*>(buf.data()), length)) { // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Streams read into the CBOR byte buffer as char data.
        throw std::runtime_error("[Cbor reader] Could not read data");
    }

    // Initializing the reader context object
    cbor_reader_ctx ctx{root};

    cbor_callbacks cbs = cbor_empty_callbacks;
    cbor_decoder_result decode_result{};
    const auto len = static_cast<size_t>(length);
    size_t bytes_read = 0;

    // Callback functions for reading the input node recursively
    cbs.string = [](void* ctx, cbor_data data, size_t size) {
        guarded(ctx, [&] { cbor_reader::cbor_read_string(ctx, data, size); });
    };
    cbs.uint8 = [](void* ctx, uint8_t value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_int(ctx, value); });
    };
    cbs.uint16 = [](void* ctx, uint16_t value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_int(ctx, value); });
    };
    cbs.uint32 = [](void* ctx, uint32_t value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_int(ctx, value); });
    };
    cbs.uint64 = [](void* ctx, uint64_t value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_int(ctx, value); });
    };
    cbs.negint8 = cbs.uint8;
    cbs.negint16 = cbs.uint16;
    cbs.negint32 = cbs.uint32;
    cbs.negint64 = cbs.uint64;
    cbs.boolean = [](void* ctx, bool value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_boolean(ctx, value); });
    };
    cbs.float4 = [](void* ctx, float value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_half_float(ctx, value); });
    };
    cbs.float8 = [](void* ctx, double value) {
        guarded(ctx, [&] { cbor_reader::cbor_read_double_float(ctx, value); });
    };
    cbs.indef_map_start = [](void* ctx) {
        guarded(ctx, [&] { cbor_reader::cbor_indef_map_start(ctx); });
    };
    cbs.map_start = [](void* ctx, size_t size) {
        guarded(ctx, [&] { cbor_reader::cbor_map_start(ctx, size); });
    };
    cbs.indef_break = [](void* ctx) {
        guarded(ctx, [&] { cbor_reader::cbor_indef_break(ctx); });
    };
    cbs.undefined = [](void* ctx) {
        guarded(ctx, [&] { cbor_reader::cbor_read_null_undefined(ctx); });
    };
    cbs.null = cbs.undefined;
    cbs.tag = [](void* ctx, uint64_t tag) {
        guarded(ctx, [&] { cbor_reader::cbor_read_tag(ctx, tag); });
    };
    cbs.byte_string = [](void* ctx, cbor_data data, size_t size) {
        guarded(ctx, [&] { cbor_reader::cbor_read_byte_string(ctx, data, size); });
    };
    cbs.indef_array_start = [](void* ctx) {
        guarded(ctx, [&] { cbor_reader::cbor_indef_array_start(ctx); });
    };
    cbs.array_start = [](void* ctx, size_t size) {
        guarded(ctx, [&] { cbor_reader::cbor_array_start(ctx, size); });
    };

    while (bytes_read < len && !ctx.callbackError) {
        decode_result = cbor_stream_decode(&buf.at(bytes_read), len - bytes_read, &cbs, &ctx);
        if (decode_result.status != cbor_decoder_status::CBOR_DECODER_FINISHED) {
            const auto err_msg = "[Cbor reader] Decoding error. CBOR decoder status:  " + std::to_string(decode_result.status);
            BOOST_LOG_SEV(cosim::log::logger(), cosim::log::error) << err_msg;
            throw std::runtime_error(err_msg);
        }
        bytes_read += decode_result.read;
    }
    if (ctx.callbackError) {
        std::rethrow_exception(ctx.callbackError);
    }
    if (!ctx.states.empty() || !ctx.keys.empty() || ctx.tag) {
        throw std::runtime_error("[Cbor reader] Invalid input: incomplete data");
    }

    return input;
}
} // namespace


namespace cosim::serialization::format
{

std::ios_base& cbor(std::ios_base& stream)
{
    stream.iword(format_xalloc) = FORMAT_CBOR;
    return stream;
}

std::ios_base& pretty_print(std::ios_base& stream)
{
    stream.iword(format_xalloc) = FORMAT_PRETTY_PRINT;
    return stream;
}

} // namespace cosim::serialization::format


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


std::istream& operator>>(std::istream& input, cosim::serialization::node& root)
{
    const auto format = input.iword(cosim::serialization::format::format_xalloc);

    if (format == cosim::serialization::format::FORMAT_CBOR) {
        return deserialize_cbor(input, root);
    }

    // Default fallback
    BOOST_LOG_SEV(cosim::log::logger(), cosim::log::warning) << "Unknown deserialization format, falling back to CBOR";
    return deserialize_cbor(input, root);
}
