
#ifndef CSE_FMUPROXY_HELPER_HPP
#define CSE_FMUPROXY_HELPER_HPP

#include <vector>

#include "cse/error.hpp"
#include "cse/model.hpp"
#include "cse/fmuproxy/service_types.hpp"


namespace {

    inline cse::variable_causality convert(const fmuproxy::thrift::Causality::type &c) {
        switch (c) {
            case fmuproxy::thrift::Causality::type::INPUT_CAUSALITY:
                return cse::variable_causality::input;
            case fmuproxy::thrift::Causality::type::OUTPUT_CAUSALITY:
                return cse::variable_causality::output;
            case fmuproxy::thrift::Causality::type::PARAMETER_CAUSALITY:
                return cse::variable_causality::parameter;
            case fmuproxy::thrift::Causality::type::CALCULATED_PARAMETER_CAUSALITY:
                return cse::variable_causality::calculated_parameter;
            case fmuproxy::thrift::Causality::type::LOCAL_CAUSALITY:
            case fmuproxy::thrift::Causality::type::INDEPENDENT_CAUSALITY:
            case fmuproxy::thrift::Causality::type::UNKNOWN_CAUSALITY:
                return cse::variable_causality::local;
            default:
                CSE_PANIC();
        }
    }

    inline cse::variable_variability convert(const fmuproxy::thrift::Variability::type &v) {
        switch (v) {
            case fmuproxy::thrift::Variability::type::CONSTANT_VARIABILITY:
                return cse::variable_variability::constant;
            case fmuproxy::thrift::Variability::type::DISCRETE_VARIABILITY:
                return cse::variable_variability::discrete;
            case fmuproxy::thrift::Variability::type::FIXED_VARIABILITY:
                return cse::variable_variability::fixed;
            case fmuproxy::thrift::Variability::type::TUNABLE_VARIABILITY:
                return cse::variable_variability::tunable;
            case fmuproxy::thrift::Variability::type::CONTINUOUS_VARIABILITY:
            case fmuproxy::thrift::Variability::type::UNKNOWN_VARIABILITY:
                return cse::variable_variability::continuous;
            default:
                CSE_PANIC();
        }
    }

    inline cse::variable_type getType(const fmuproxy::thrift::ScalarVariable &v) {
        if (v.attribute.__isset.integerAttribute) {
            return cse::variable_type::integer;
        } else if (v.attribute.__isset.realAttribute) {
            return cse::variable_type::real;
        } else if (v.attribute.__isset.stringAttribute) {
            return cse::variable_type::string;
        } else if (v.attribute.__isset.booleanAttribute) {
            return cse::variable_type::boolean;
        } else {
            CSE_PANIC();
        }
    }

    inline cse::variable_description convert(const fmuproxy::thrift::ScalarVariable &v) {
        cse::variable_description var;
        var.name = v.name;
        var.index = (cse::variable_index) v.valueReference;
        var.causality = convert(v.causality);
        var.variability = convert(v.variability);
        var.type = getType(v);
        return var;
    }

    inline std::vector<cse::variable_description> convert(fmuproxy::thrift::ModelVariables &vars) {
        std::vector<cse::variable_description> modelVariables(vars.size());
        for (const auto &v : vars) {
            modelVariables.push_back(convert(v));
        }
        return modelVariables;
    }

    inline std::shared_ptr<cse::model_description> convert(fmuproxy::thrift::ModelDescription &md) {

        auto modelDescription = std::make_shared<cse::model_description>();
        modelDescription->name = md.modelName;
        modelDescription->author = md.author;
        modelDescription->uuid = md.guid;
        modelDescription->version = md.version;
        modelDescription->description = md.description;
        modelDescription->variables = convert(md.modelVariables);
        return modelDescription;

    }

}

#endif
