/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_SSP_PARSER_HPP
#define LIBCOSIM_SSP_PARSER_HPP

#include "cosim/algorithm.hpp"
#include "cosim/fs_portability.hpp"
#include "cosim/model_description.hpp"
#include "cosim/time.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cosim
{

/**
 * Parses the supported SSP system-description subset into components,
 * connectors and connections. Returned references belong to this parser.
 */
class ssp_parser // NOLINT(cppcoreguidelines-special-member-functions): Preserve the implicit copy and move behavior of this parser.
{

public:
    explicit ssp_parser(const cosim::filesystem::path& ssdPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime = 0.0;
        std::optional<double> stopTime;
        std::shared_ptr<cosim::algorithm> algorithm;
    };

    /// Returns the parsed default time range and algorithm.
    [[nodiscard]] const DefaultExperiment& get_default_experiment() const;

    struct System
    {
        std::string name;
        std::optional<std::string> description;
    };

    struct SystemDescription
    {
        std::string name;
        std::string version;
        System system;
    };

    struct Connector // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        std::string name;
        std::string kind;
        std::optional<cosim::variable_type> type;
        std::vector<std::uint64_t> shape;
        std::unordered_map<std::string, std::string> clockAttributes;
    };

    struct EnumerationParameter
    {
        std::optional<std::string> attribute;
        std::vector<std::string> items;
        std::vector<std::uint64_t> shape;
    };

    struct Parameter
    {
        std::string name;
        std::variant<variable_value, EnumerationParameter> value;
    };

    struct ParameterSet
    {
        std::string name;
        std::vector<Parameter> parameters;
    };

    struct Component // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        std::string name;
        std::string source;
        std::optional<double> stepSizeHint;
        std::vector<ParameterSet> parameterSets;
        std::unordered_map<std::string, Connector> connectors;
    };

    /// Returns parsed components keyed by element name.
    [[nodiscard]] const std::unordered_map<std::string, Component>& get_elements() const;

    struct LinearTransformation
    {
        double offset;
        double factor;
    };

    struct Endpoint
    {
        std::string component;
        std::string connector;
    };

    struct Connection
    {
        Endpoint start;
        Endpoint end;
        std::optional<LinearTransformation> linearTransformation;
    };

    /// Returns parsed connections in their stored order.
    [[nodiscard]] const std::vector<Connection>& get_connections() const;
    [[nodiscard]] bool has_exact_types() const noexcept { return systemDescription_.version == "2.0"; }

private:
    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;

    std::vector<Connection> connections_;
    std::unordered_map<std::string, Component> elements_;
};

cosim::time_point get_default_start_time(const ssp_parser& parser);

} // namespace cosim

#endif
