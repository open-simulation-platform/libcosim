
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <cse/cse_config.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>
#include <optional>
#include <string>


namespace cse
{

using simulator_map = std::map<std::string, simulator_map_entry>;


class algorithm_sub_resolver
{

public:
    virtual std::shared_ptr<cse::algorithm> parse(
        const std::string& algorithmName,
        const boost::property_tree::ptree& tree) = 0;
};

class algorithm_resolver
{
public:
    /// Constructs an empty URI resolver.
    algorithm_resolver() noexcept = default;

    algorithm_resolver(algorithm_resolver&&) noexcept = default;
    algorithm_resolver& operator=(algorithm_resolver&&) noexcept = default;

    algorithm_resolver(const algorithm_resolver&) = delete;
    algorithm_resolver& operator=(const algorithm_resolver&) = delete;

    ~algorithm_resolver() noexcept = default;

    /// Adds a sub-resolver.
    void add_resolver(std::shared_ptr<algorithm_sub_resolver> resolver);

    std::shared_ptr<cse::algorithm> resolve(
        const std::string& algorithmName,
        const boost::property_tree::ptree& tree);

private:
    std::vector<std::shared_ptr<algorithm_sub_resolver>> subResolvers_;
};


class fixed_step_algorithm_resolver : public algorithm_sub_resolver
{

public:
    std::shared_ptr<cse::algorithm> parse(
        const std::string& algorithmName,
        const boost::property_tree::ptree& tree) override;
};

std::shared_ptr<algorithm_resolver> default_algorithm_resolver();

/**
 *  Creates an execution based on a SystemStructure.ssd file.
 *
 *  \param [in] configPath
 *      Path to an .ssd file, or a directory holding SystemStructure.ssd
 *  \param [in] overrideStartTime
 *      If defined, the (logical) time point at which the simulation should start.
 *      If not defined, this variable will be read from .ssd file.
 */
std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    cse::algorithm_resolver&,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime);

/**
 *  Creates an execution based on a SystemStructure.ssd file.
 *
 *  \param [in] configPath
 *      Path to an .ssd file, or a directory holding SystemStructure.ssd
 *  \param [in] overrideAlgorithm
 *      If defined, the co-simulation algorithm used in the execution.
 *      If not defined, the algorithm will be a `fixed_step_algorithm` with
 *      `stepSize` defined in the .ssd file.
 *  \param [in] overrideStartTime
 *      If defined, the (logical) time point at which the simulation should start.
 *      If not defined, this variable will be read from the .ssd file.
 */
std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    cse::algorithm_resolver&,
    const boost::filesystem::path& configPath,
    std::shared_ptr<cse::algorithm> overrideAlgorithm = nullptr,
    std::optional<cse::time_point> overrideStartTime = std::nullopt);

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
