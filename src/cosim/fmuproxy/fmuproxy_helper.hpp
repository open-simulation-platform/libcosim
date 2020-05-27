/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_FMUPROXY_HELPER_HPP
#define COSIM_FMUPROXY_FMUPROXY_HELPER_HPP

#include "cosim/error.hpp"
#include "cosim/fmuproxy/service_types.hpp"
#include "cosim/model_description.hpp"

#include <vector>


namespace
{

inline cosim::variable_causality parse_causality(const std::string& c)
{
    if (c == "input") {
        return cosim::variable_causality::input;
    } else if (c == "output") {
        return cosim::variable_causality::output;
    } else if (c == "parameter") {
        return cosim::variable_causality::parameter;
    } else if (c == "calculated_parameter") {
        return cosim::variable_causality::calculated_parameter;
    } else if (c == "local" || c == "independent" || c == "unknown" || c == "") {
        return cosim::variable_causality::local;
    } else {
        const auto err = "Failed to parse causality: '" + c + "'";
        COSIM_PANIC_M(err.c_str());
    }
}

inline cosim::variable_variability parse_variability(const std::string& v)
{
    if (v == "constant") {
        return cosim::variable_variability::constant;
    } else if (v == "discrete") {
        return cosim::variable_variability::discrete;
    } else if (v == "fixed") {
        return cosim::variable_variability::fixed;
    } else if (v == "tunable") {
        return cosim::variable_variability::tunable;
    } else if (v == "continuous" || v == "unknown" || v == "") {
        return cosim::variable_variability::continuous;
    } else {
        const auto err = "Failed to parse variability: '" + v + "'";
        COSIM_PANIC_M(err.c_str());
    }
}

inline cosim::variable_type get_type(const fmuproxy::thrift::ScalarVariable& v)
{
    if (v.attribute.__isset.integer_attribute) {
        return cosim::variable_type::integer;
    } else if (v.attribute.__isset.real_attribute) {
        return cosim::variable_type::real;
    } else if (v.attribute.__isset.string_attribute) {
        return cosim::variable_type::string;
    } else if (v.attribute.__isset.boolean_attribute) {
        return cosim::variable_type::boolean;
    } else if (v.attribute.__isset.enumeration_attribute) {
        return cosim::variable_type::enumeration;
    } else {
        const auto err = "Failed to get type of variable: '" + v.name + "'";
        COSIM_PANIC_M(err.c_str());
    }
}

inline cosim::variable_description convert(const fmuproxy::thrift::ScalarVariable& v)
{
    cosim::variable_description var;
    var.name = v.name;
    var.reference = (cosim::value_reference)v.value_reference;
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

inline std::vector<cosim::variable_description> convert(fmuproxy::thrift::ModelVariables& vars)
{
    std::vector<cosim::variable_description> modelVariables;
    for (const auto& v : vars) {
        modelVariables.push_back(convert(v));
    }
    return modelVariables;
}

inline std::shared_ptr<cosim::model_description> convert(fmuproxy::thrift::ModelDescription& md)
{
    auto modelDescription = std::make_shared<cosim::model_description>();
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
