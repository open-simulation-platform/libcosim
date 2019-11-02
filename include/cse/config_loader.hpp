
#ifndef CSECORE_CONFIG_LOADER_HPP
#define CSECORE_CONFIG_LOADER_HPP

#include <cse/cse_config.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem/path.hpp>

#include <map>
#include <optional>
#include <string>

namespace cse
{

using simulator_map = std::map<std::string, simulator_map_entry>;

/**
 * Base class for loading an execution from a configuration file
 */
class config_loader
{

public:

    config_loader();

    /**
     *  Explicitly specify the simulation start time
     *  Will override any value found in the ssp.
     *
     *  \param [in] timePoint
     *      The (logical) time point at which the simulation should start
     */
    virtual void override_start_time(cse::time_point timePoint);

    /**
     *  Explicitly specify the co-simulation algorithm to use.
     *  Will override any value found in the ssp.
     *
     *  \param [in] algorithm
     *      The co-simulation algorithm to be used in the execution.
     */
    virtual void override_algorithm(std::shared_ptr<cse::algorithm> algorithm);

    /**
     * Assign a custom `model_uri_resolver`
     *
     * \param [in] modelResolver
     *      The `model_uri_resolver` to use by the loader
     *
     */
    void set_custom_model_uri_resolver(std::shared_ptr<cse::model_uri_resolver> modelResolver);

    /**
     *  Creates an execution based on a SystemStructure.ssd file.
     *
     *  \param [in] configPath
     *      Path to an .ssd file, or a directory holding SystemStructure.ssd
     */
    virtual std::pair<execution, simulator_map> load(const boost::filesystem::path& configPath) = 0;

protected:
    std::optional<cse::time_point> overrideStartTime_;
    std::shared_ptr<cse::algorithm> overrideAlgorithm_;
    std::shared_ptr<cse::model_uri_resolver> modelResolver_;

};

} // namespace cse

#endif //CSECORE_CONFIG_LOADER_HPP
