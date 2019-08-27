
#ifndef CSE_FMUPROXY_FMUPROXY_HELPER_HPP
#define CSE_FMUPROXY_FMUPROXY_HELPER_HPP

#include "cse/error.hpp"
#include "cse/fmuproxy/service_types.hpp"
#include "cse/model.hpp"

#include <vector>


namespace
{

inline cse::variable_causality parse_causality(const std::string& c)
{
    if (c.compare("input") == 0) {
        return cse::variable_causality::input;
    } else if (c.compare("output") == 0) {
        return cse::variable_causality::output;
    } else if (c.compare("parameter") == 0) {
        return cse::variable_causality::parameter;
    } else if (c.compare("calculated_parameter") == 0) {
        return cse::variable_causality::calculated_parameter;
    } else if (c.compare("local") == 0 || c.compare("independent") == 0 || c.compare("unknown") == 0 || c.compare("") == 0) {
        return cse::variable_causality::local;
    } else {
        const auto err = "Failed to parse causality: '" + c + "'";
        CSE_PANIC_M(err.c_str());
    }
}

inline cse::variable_variability parse_variability(const std::string& v)
{
    if (v.compare("constant") == 0) {
        return cse::variable_variability::constant;
    } else if (v.compare("discrete") == 0) {
        return cse::variable_variability::discrete;
    } else if (v.compare("fixed") == 0) {
        return cse::variable_variability::fixed;
    } else if (v.compare("tunable") == 0) {
        return cse::variable_variability::tunable;
    } else if (v.compare("continuous") == 0 || v.compare("unknown") == 0 || v.compare("") == 0) {
        return cse::variable_variability::continuous;
    } else {
        const auto err = "Failed to parse variability: '" + v + "'";
        CSE_PANIC_M(err.c_str());
    }
}

inline cse::variable_type get_type(const fmuproxy::thrift::ScalarVariable& v)
{
    if (v.attribute.__isset.integer_attribute) {
        return cse::variable_type::integer;
    } else if (v.attribute.__isset.real_attribute) {
        return cse::variable_type::real;
    } else if (v.attribute.__isset.string_attribute) {
        return cse::variable_type::string;
    } else if (v.attribute.__isset.boolean_attribute) {
        return cse::variable_type::boolean;
    } else if (v.attribute.__isset.enumeration_attribute) {
        return cse::variable_type::enumeration;
    } else {
        const auto err = "Failed to get type of variable: '" + v.name + "'";
        CSE_PANIC_M(err.c_str());
    }
}

inline cse::variable_description convert(const fmuproxy::thrift::ScalarVariable& v)
{
    cse::variable_description var;
    var.name = v.name;
    var.index = (cse::value_reference)v.value_reference;
    var.causality = parse_causality(v.causality);
    var.variability = parse_variability(v.variability);
    var.type = get_type(v);
    if (v.attribute.__isset.integer_attribute) {
        var.start = v.attribute.integer_attribute.start;
    } else if (v.attribute.__isset.real_attribute) {
        var.start = v.attribute.real_attribute.start;
    } else if (v.attribute.__isset.string_attribute) {
        var.start = v.attribute.string_attribute.start;
    } else if (v.attribute.__isset.boolean_attribute) {
        var.start = v.attribute.boolean_attribute.start;
    } else if (v.attribute.__isset.enumeration_attribute) {
        var.start = v.attribute.enumeration_attribute.start;
    }
    return var;
}

inline std::vector<cse::variable_description> convert(fmuproxy::thrift::ModelVariables& vars)
{
    std::vector<cse::variable_description> modelVariables;
    for (const auto& v : vars) {
        modelVariables.push_back(convert(v));
    }
    return modelVariables;
}

inline std::shared_ptr<cse::model_description> convert(fmuproxy::thrift::ModelDescription& md)
{
    auto modelDescription = std::make_shared<cse::model_description>();
    modelDescription->name = md.modelName;
    modelDescription->author = md.author;
    modelDescription->uuid = md.guid;
    modelDescription->version = md.version;
    modelDescription->description = md.description;
    modelDescription->variables = convert(md.model_variables);
    return modelDescription;
}

} // namespace

#endif
