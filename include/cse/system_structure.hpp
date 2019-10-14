#ifndef CSE_SYSTEM_STRUCTURE_HPP
#define CSE_SYSTEM_STRUCTURE_HPP

#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/map.hpp>
#include <gsl/span>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>


namespace cse
{

struct variable_qname
{
    std::string simulator_name;
    std::string variable_name;
};


bool operator==(const variable_qname& a, const variable_qname& b)
{
    return a.simulator_name == b.simulator_name &&
        a.variable_name == b.variable_name;
}

} // namespace cse

namespace std
{
template<>
struct hash<cse::variable_qname>
{
    std::size_t operator()(const cse::variable_qname& vqn) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, vqn.simulator_name);
        boost::hash_combine(seed, vqn.variable_name);
        return seed;
    }
};
} // namespace std

namespace cse
{

class system_structure
{
public:
    struct simulator
    {
        std::string name;
        std::shared_ptr<cse::model> model;
    };

    struct scalar_connection
    {
        variable_qname source;
        variable_qname target;
    };

private:
    using simulator_map = std::unordered_map<std::string, simulator>;
    using scalar_connection_map =
        std::unordered_map<variable_qname, variable_qname>;

public:
    using simulator_range = boost::select_second_const_range<simulator_map>;
    using scalar_connection_range =
        boost::select_second_const_range<scalar_connection_map>;

    void add_simulator(const simulator& s);

    void add_simulator(std::string_view name, std::shared_ptr<cse::model> model)
    {
        add_simulator({std::string(name), model});
    }

    simulator_range simulators() const;

    void add_scalar_connection(const scalar_connection& c);

    void add_scalar_connection(
        const variable_qname& source, const variable_qname& target)
    {
        add_scalar_connection({source, target});
    }

    scalar_connection_range scalar_connections() const;

    /// Looks up the description of a variable by its qualified name.
    const variable_description& get_variable_description(
        const variable_qname& v) const;

private:
    // Simulators, indexed by name.
    simulator_map simulators_;

    // Scalar connections. Target is key, source is value.
    scalar_connection_map scalarConnections_;

    // Cache for fast lookup of model info, indexed by model UUID.
    struct model_info
    {
        std::unordered_map<std::string, variable_description> variables;
    };
    std::unordered_map<std::string, model_info> modelCache_;
};


bool is_valid_simulator_name(std::string_view name) noexcept;

//bool is_valid_variable_value(
//    const variable_description& variable,
//    const scalar_value& value,
//    std::string* reason);

bool is_valid_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason);


} // namespace cse
#endif // header guard
