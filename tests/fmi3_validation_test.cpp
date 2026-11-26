/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 *  FMI 3 data-type validation against FMPy (docs/fmi3-validation-plan.md).
 *
 *  Replays one case from tests/data/fmi3/validation/cases/<case>.json
 *  through a libcosim slave instance, writes the samples in the same CSV
 *  format as tests/tools/gen_validation.py (which reads the same case file
 *  and runs it through FMPy), and compares them with
 *  tests/data/fmi3/validation/results/<case>.csv.
 *
 *  The slave is driven directly (not through `execution`) because that is
 *  the level at which FMI 3 exact types are reachable, and
 *  it mirrors FMPy's own stepping loop.
 */
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v3/fmu.hpp>
#include <cosim/model_description.hpp>
#include <cosim/time.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <gsl/span>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace cosim;
namespace pt = boost::property_tree;

namespace
{

// ---------------------------------------------------------------------------
// Case definition (mirrors gen_validation.py write_case_json)
// ---------------------------------------------------------------------------

struct change
{
    double time;
    std::vector<std::string> literals; // one per element; scalar => size 1
};

struct stimulus
{
    std::string variable;
    std::string type;
    std::vector<change> changes;
};

struct output
{
    std::string name;
    std::string type;
};

struct case_definition
{
    std::string name;
    std::string fmu;
    double start = 0.0;
    double stop = 0.0;
    double step = 0.0;
    bool eventMode = false;
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> startValues; // name -> (type, literals)
    std::map<std::string, std::uint64_t> structural;
    std::vector<stimulus> stimuli;
    std::vector<output> outputs;
};

std::vector<std::string> literals_of(const pt::ptree& node)
{
    std::vector<std::string> out;
    if (node.empty()) {
        out.push_back(node.data());
    } else {
        for (const auto& child : node) out.push_back(child.second.data());
    }
    return out;
}

case_definition read_case(const std::string& path)
{
    pt::ptree tree;
    pt::read_json(path, tree);
    static const pt::ptree empty;
    case_definition c;
    c.name = filesystem::path(path).stem().string();
    c.fmu = tree.get<std::string>("fmu");
    c.start = tree.get<double>("start", 0.0);
    c.stop = tree.get<double>("stop");
    c.step = tree.get<double>("step");
    c.eventMode = tree.get<bool>("event_mode", false);
    for (const auto& [name, node] : tree.get_child("start_values", empty)) {
        c.startValues[name] = {node.get<std::string>("type"), literals_of(node.get_child("value"))};
    }
    for (const auto& [name, node] : tree.get_child("structural", empty)) {
        c.structural[name] = node.get_value<std::uint64_t>();
    }
    for (const auto& [_, node] : tree.get_child("stimuli", empty)) {
        stimulus s;
        s.variable = node.get<std::string>("variable");
        s.type = node.get<std::string>("type");
        for (const auto& [__, ch] : node.get_child("changes")) {
            s.changes.push_back({ch.get<double>("time"), literals_of(ch.get_child("value"))});
        }
        c.stimuli.push_back(std::move(s));
    }
    for (const auto& [_, node] : tree.get_child("outputs")) {
        c.outputs.push_back({node.get<std::string>("name"), node.get<std::string>("type")});
    }
    return c;
}

// ---------------------------------------------------------------------------
// Type and literal handling
// ---------------------------------------------------------------------------

variable_type type_from_fmi_name(const std::string& n)
{
    static const std::map<std::string, variable_type> names = {
        {"Float32", variable_type::float32}, {"Float64", variable_type::real},
        {"Int8", variable_type::int8}, {"UInt8", variable_type::uint8},
        {"Int16", variable_type::int16}, {"UInt16", variable_type::uint16},
        {"Int32", variable_type::integer}, {"UInt32", variable_type::uint32},
        {"Int64", variable_type::int64}, {"UInt64", variable_type::uint64},
        {"Boolean", variable_type::boolean}, {"String", variable_type::string},
        {"Binary", variable_type::binary}, {"Enumeration", variable_type::enumeration}};
    const auto it = names.find(n);
    if (it == names.end()) throw std::runtime_error("Unknown FMI type name: " + n);
    return it->second;
}

std::vector<std::uint8_t> hex_decode(const std::string& hex)
{
    if (hex.size() % 2 != 0) throw std::runtime_error("Odd-length hex literal");
    std::vector<std::uint8_t> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
    }
    return out;
}

std::string hex_encode(gsl::span<const std::uint8_t> bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

bool parse_bool(const std::string& s)
{
    if (s == "True" || s == "true" || s == "1") return true;
    if (s == "False" || s == "false" || s == "0") return false;
    throw std::runtime_error("Bad boolean literal: " + s);
}

template<typename T>
T parse_int(const std::string& s)
{
    if constexpr (std::is_signed_v<T>) {
        const auto v = std::stoll(s);
        if (v < std::numeric_limits<T>::min() || v > std::numeric_limits<T>::max()) throw std::out_of_range(s);
        return static_cast<T>(v);
    } else {
        if (!s.empty() && s[0] == '-') throw std::out_of_range(s);
        const auto v = std::stoull(s);
        if (v > std::numeric_limits<T>::max()) throw std::out_of_range(s);
        return static_cast<T>(v);
    }
}

/// Builds an owning `variable_value` of `type` from text literals with the given shape.
variable_value make_value(variable_type type, const std::vector<std::string>& lits, std::vector<std::uint64_t> dims)
{
    const auto n = lits.size();
    auto fill = [&](auto tag, auto conv) {
        using V = decltype(tag);
        V v;
        v.values.reserve(n);
        for (const auto& l : lits) v.values.push_back(conv(l));
        return variable_value(variable_value_storage(std::move(v)), std::move(dims));
    };
    switch (type) {
        case variable_type::real: return fill(real_values{}, [](const std::string& s) { return std::stod(s); });
        case variable_type::float32: return fill(float32_values{}, [](const std::string& s) { return std::stof(s); });
        case variable_type::integer: return fill(integer_values{}, [](const std::string& s) { return parse_int<int>(s); });
        case variable_type::int8: return fill(int8_values{}, [](const std::string& s) { return parse_int<std::int8_t>(s); });
        case variable_type::uint8: return fill(uint8_values{}, [](const std::string& s) { return parse_int<std::uint8_t>(s); });
        case variable_type::int16: return fill(int16_values{}, [](const std::string& s) { return parse_int<std::int16_t>(s); });
        case variable_type::uint16: return fill(uint16_values{}, [](const std::string& s) { return parse_int<std::uint16_t>(s); });
        case variable_type::uint32: return fill(uint32_values{}, [](const std::string& s) { return parse_int<std::uint32_t>(s); });
        case variable_type::int64: return fill(int64_values{}, [](const std::string& s) { return parse_int<std::int64_t>(s); });
        case variable_type::uint64: return fill(uint64_values{}, [](const std::string& s) { return parse_int<std::uint64_t>(s); });
        case variable_type::boolean: return fill(boolean_values{}, [](const std::string& s) { return parse_bool(s); });
        case variable_type::string: return fill(string_values{}, [](const std::string& s) { return s; });
        case variable_type::enumeration:
            return fill(enumeration_values{}, [](const std::string& s) { return enumeration_value{parse_int<std::int64_t>(s)}; });
        case variable_type::binary:
            return fill(binary_values{}, [](const std::string& s) { return binary_value{hex_decode(s)}; });
        case variable_type::clock: break;
    }
    throw std::runtime_error("Unsupported value type");
}

// ---------------------------------------------------------------------------
// CSV literal formatting (same contract as gen_validation.py fmt())
// ---------------------------------------------------------------------------

template<typename F>
std::string float_literal(F v)
{
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    std::string s(buf, r.ptr);
    // Python prints integral floats as "1.0"; to_chars prints "1".
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string literal_of(const variable_value& value)
{
    std::vector<std::string> parts;
    std::visit([&](const auto& typed) {
        using V = std::decay_t<decltype(typed)>;
        for (const auto& e : typed.values) {
            if constexpr (std::is_same_v<V, real_values>) {
                parts.push_back(value.is_scalar() ? float_literal(e) : [&] { char b[32]; auto r = std::to_chars(b, b + 32, e, std::chars_format::general, 16); return std::string(b, r.ptr); }());
            } else if constexpr (std::is_same_v<V, float32_values>) {
                parts.push_back(float_literal(e));
            } else if constexpr (std::is_same_v<V, boolean_values>) {
                parts.push_back(e ? "True" : "False");
            } else if constexpr (std::is_same_v<V, string_values>) {
                parts.push_back(e);
            } else if constexpr (std::is_same_v<V, binary_values>) {
                parts.push_back(hex_encode(e.data));
            } else if constexpr (std::is_same_v<V, enumeration_values>) {
                parts.push_back(std::to_string(e.value));
            } else {
                parts.push_back(std::to_string(+e)); // promotes int8/uint8 to int
            }
        }
    },
        value.values());
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ' ';
        out += parts[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::uint32_t float_bits(float f)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

/// Compares two CSV literals of the given type as decoded values, not as text.
bool literals_equal(variable_type type, const std::string& expected, const std::string& actual, std::string& why)
{
    if (type == variable_type::string) return expected == actual;
    if (type == variable_type::binary) return expected == actual; // hex is canonical
    const auto ev = split(expected, ' ');
    const auto av = split(actual, ' ');
    if (ev.size() != av.size()) {
        why = "element count " + std::to_string(av.size()) + " != " + std::to_string(ev.size());
        return false;
    }
    for (std::size_t i = 0; i < ev.size(); ++i) {
        bool ok = true;
        switch (type) {
            case variable_type::real: ok = std::stod(ev[i]) == std::stod(av[i]); break;
            case variable_type::float32:
                ok = float_bits(std::stof(ev[i])) == float_bits(std::stof(av[i]));
                break;
            case variable_type::boolean: ok = parse_bool(ev[i]) == parse_bool(av[i]); break;
            case variable_type::uint64: ok = std::stoull(ev[i]) == std::stoull(av[i]); break;
            default: ok = std::stoll(ev[i]) == std::stoll(av[i]); break;
        }
        if (!ok) {
            why = "element " + std::to_string(i) + ": " + av[i] + " != " + ev[i];
            return false;
        }
    }
    return true;
}

struct table
{
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

table read_csv(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path);
    table t;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto cells = split(line, ',');
        if (first) {
            for (auto& h : cells) {
                if (h.size() >= 2 && h.front() == '"' && h.back() == '"') h = h.substr(1, h.size() - 2);
            }
            t.header = std::move(cells);
            first = false;
        } else {
            t.rows.push_back(std::move(cells));
        }
    }
    return t;
}

int compare(const table& expected, const table& actual, const case_definition& c)
{
    int failures = 0;
    if (expected.header != actual.header) {
        std::cerr << "FAIL: column sets differ\n";
        return 1;
    }
    if (expected.rows.size() != actual.rows.size()) {
        std::cerr << "FAIL: row count " << actual.rows.size() << " != " << expected.rows.size() << '\n';
        ++failures;
    }
    const auto n = std::min(expected.rows.size(), actual.rows.size());
    for (std::size_t r = 0; r < n; ++r) {
        const auto& e = expected.rows[r];
        const auto& a = actual.rows[r];
        const double te = std::stod(e[0]);
        const double ta = std::stod(a[0]);
        if (std::abs(te - ta) > 1e-9) {
            std::cerr << "FAIL row " << r << ": time " << a[0] << " != " << e[0] << '\n';
            ++failures;
        }
        for (std::size_t col = 1; col < e.size(); ++col) {
            std::string why;
            if (!literals_equal(type_from_fmi_name(c.outputs[col - 1].type), e[col], a[col], why)) {
                std::cerr << "FAIL row " << r << " (t=" << e[0] << ") " << expected.header[col] << ": "
                          << (why.empty() ? a[col] + " != " + e[col] : why) << '\n';
                if (++failures > 20) {
                    std::cerr << "... (further differences suppressed)\n";
                    return failures;
                }
            }
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

struct resolved_variable
{
    typed_value_reference ref;
    std::vector<std::uint64_t> dims; // empty => scalar
};

/// Resolves name -> typed reference and the current flat shape of the variable.
resolved_variable resolve(const model_description& md, const std::string& name, variable_type type,
    const std::map<std::string, std::uint64_t>& structural)
{
    const auto desc = find_variable(md, name);
    if (!desc) throw std::runtime_error("No variable named " + name);
    if (desc->type != type) {
        throw std::runtime_error("Variable " + name + " has type " + to_text(desc->type) + ", case says " + to_text(type));
    }
    resolved_variable rv{{type, desc->reference}, {}};
    for (const auto& d : desc->dimensions) {
        if (const auto* fixed = std::get_if<fixed_dimension>(&d)) {
            rv.dims.push_back(fixed->size);
        } else {
            const auto vr = std::get<variable_dimension>(d).structural_parameter;
            const auto sp = std::find_if(md.variables.begin(), md.variables.end(),
                [vr](const auto& v) { return v.reference == vr && v.type == variable_type::uint64; });
            if (sp == md.variables.end()) throw std::runtime_error("Structural parameter not found for " + name);
            const auto it = structural.find(sp->name);
            if (it != structural.end()) {
                rv.dims.push_back(it->second);
            } else if (sp->start) {
                rv.dims.push_back(std::get<std::uint64_t>(*sp->start));
            } else {
                throw std::runtime_error("No value for structural parameter " + sp->name);
            }
        }
    }
    return rv;
}

int run(const std::string& casePath)
{
    const auto c = read_case(casePath);
    const auto* fmuDir = std::getenv("REFERENCE_FMU_V3_DIR");
    const auto* validationDir = std::getenv("FMI3_VALIDATION_DIR");
    if (!fmuDir || !validationDir) {
        std::cerr << "FMI3_VALIDATION_DIR and REFERENCE_FMU_V3_DIR must be set\n";
        return 2;
    }

    const auto importer = fmi::importer::create();
    const auto fmu = std::dynamic_pointer_cast<fmi::v3::fmu>(importer->import(filesystem::path(fmuDir) / c.fmu));
    if (!fmu) throw std::runtime_error(c.fmu + " is not an FMI 3 FMU");
    const auto md = fmu->model_description();

    fmi::v3::co_simulation_callbacks options;
    options.event_mode_used = c.eventMode;
    options.early_return_allowed = false;
    const auto slave = fmu->instantiate_v3_slave(c.name, options);

    // Batch helper: set a list of (name, type, literals) on the slave.
    auto set_batch = [&](const std::vector<std::tuple<std::string, variable_type, std::vector<std::string>>>& items) {
        if (items.empty()) return;
        std::vector<typed_value_reference> refs;
        std::vector<variable_value> values;
        for (const auto& [name, type, lits] : items) {
            auto rv = resolve(*md, name, type, c.structural);
            if (rv.dims.empty() && lits.size() != 1) {
                throw std::runtime_error("Scalar " + name + " given " + std::to_string(lits.size()) + " values");
            }
            refs.push_back(rv.ref);
            values.push_back(make_value(type, lits, rv.dims));
        }
        slave->set_variable_values(refs, values);
    };

    if (!c.structural.empty()) {
        slave->enter_configuration_mode();
        std::vector<std::tuple<std::string, variable_type, std::vector<std::string>>> items;
        for (const auto& [name, value] : c.structural) {
            items.emplace_back(name, variable_type::uint64, std::vector<std::string>{std::to_string(value)});
        }
        set_batch(items);
        slave->exit_configuration_mode();
    }

    const auto start = to_time_point(c.start);
    const auto stop = to_time_point(c.stop);
    const auto h = to_duration(c.step, c.start);
    slave->setup(start, stop, std::nullopt);

    {
        std::vector<std::tuple<std::string, variable_type, std::vector<std::string>>> items;
        for (const auto& [name, tv] : c.startValues) items.emplace_back(name, type_from_fmi_name(tv.first), tv.second);
        for (const auto& s : c.stimuli) {
            for (const auto& ch : s.changes) {
                if (to_time_point(ch.time) == start) items.emplace_back(s.variable, type_from_fmi_name(s.type), ch.literals);
            }
        }
        set_batch(items);
    }
    slave->start_simulation();

    std::vector<typed_value_reference> outRefs;
    for (const auto& o : c.outputs) outRefs.push_back(resolve(*md, o.name, type_from_fmi_name(o.type), c.structural).ref);

    table result;
    result.header.push_back("time");
    for (const auto& o : c.outputs) result.header.push_back(o.name);
    auto sample = [&](time_point t) {
        std::vector<std::string> row;
        row.push_back(float_literal(to_double_time_point(t)));
        for (const auto& v : slave->get_variable_values(outRefs)) row.push_back(literal_of(v));
        result.rows.push_back(std::move(row));
    };

    bool terminated = false;
    sample(start);
    const auto nSteps = static_cast<std::int64_t>(std::llround((c.stop - c.start) / c.step));
    for (std::int64_t k = 0; k < nSteps; ++k) {
        const auto t = start + k * h;
        std::vector<std::tuple<std::string, variable_type, std::vector<std::string>>> items;
        for (const auto& s : c.stimuli) {
            for (const auto& ch : s.changes) {
                const auto tc = to_time_point(ch.time);
                if (tc == t && tc != start) items.emplace_back(s.variable, type_from_fmi_name(s.type), ch.literals);
            }
        }
        set_batch(items);
        const auto info = slave->do_step_with_info(t, h);
        if (info.result != step_result::complete) throw std::runtime_error("Step failed at t=" + std::to_string(to_double_time_point(t)));
        if (info.early_return) throw std::runtime_error("Unexpected early return at t=" + std::to_string(to_double_time_point(t)));
        if (info.event_handling_needed && c.eventMode) slave->handle_events();
        // On an FMU-requested termination libcosim requires Terminate before
        // any further Get, so the final sample is read in the Terminated
        // state. FMPy reads it in Step Mode before terminating; the FMU's
        // values are the same either way.
        if (info.termination_requested) {
            slave->end_simulation();
            sample(start + (k + 1) * h);
            terminated = true;
            break;
        }
        sample(start + (k + 1) * h);
    }
    if (!terminated) slave->end_simulation();

    const auto outPath = filesystem::path(validationDir) / "results" / (c.name + ".libcosim.csv");
    {
        std::ofstream out(outPath.string(), std::ios::binary);
        for (std::size_t i = 0; i < result.header.size(); ++i) out << (i ? "," : "") << '"' << result.header[i] << '"';
        out << '\n';
        for (const auto& row : result.rows) {
            for (std::size_t i = 0; i < row.size(); ++i) out << (i ? "," : "") << row[i];
            out << '\n';
        }
    }

    const auto expected = read_csv((filesystem::path(validationDir) / "results" / (c.name + ".csv")).string());
    const auto failures = compare(expected, result, c);
    if (failures == 0) {
        std::cout << "OK " << c.name << ": " << result.rows.size() << " rows match FMPy\n";
        filesystem::remove(outPath);
        return 0;
    }
    std::cerr << "libcosim output kept at " << outPath.string() << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <case.json>\n";
        return 2;
    }
    try {
        return run(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
