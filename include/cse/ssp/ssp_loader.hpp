#ifndef CSECORE_SSP_LOADER_HPP
#define CSECORE_SSP_LOADER_HPP

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


// Class for loading an execution from a SSP configuration.
class ssp_loader
{

public:
    ssp_loader();

    /**
     *  Explicitly specifies the simulation start time.
     *  Overrides the start time specified in the configuration file.
     *
     *  \param [in] timePoint
     *      The (logical) time point at which the simulation should start
     */
    virtual void override_start_time(cse::time_point timePoint);

    /**
     *  Explicitly specify the co-simulation algorithm to use.
     *  Required if no algorithm is present in the configuration to load.
     *
     *  Overrides the algorithm specified in the configuration file.
     *
     *  \param [in] algorithm
     *      The co-simulation algorithm to be used in the execution.
     */
    virtual void override_algorithm(std::shared_ptr<cse::algorithm> algorithm);

    /**
     * Assign a custom `model_uri_resolver`.
     *
     * \param [in] modelResolver
     *      The `model_uri_resolver` to use by the loader.
     */
    void set_model_uri_resolver(std::shared_ptr<cse::model_uri_resolver> resolver);

    /**
     * Specify a non-default SystemStructureDefinition (.ssd) file to load.
     *
     * \param [in] name
     *      The name of the (custom) SystemStructureDefinition file to load.
     */
    void set_ssd_file_name(const std::string& name);

    /**
     * Explicitly specify the name of the parameter set(s) to load.
     *
     * /param [in] name
     *      The name of the parameter set(s) to load
     */
    void set_parameter_set_name(const std::string& name);

    /**
     *  Create an execution based on a SSP configuration.
     *
     *  \param [in] configPath
     *      Path to the .ssp archive, or a directory holding one or more .ssd files.
     */
    std::pair<execution, simulator_map> load(const boost::filesystem::path& configPath);

private:
    std::optional<std::string> ssdFileName_;
    std::optional<std::string> parameterSetName_;
    std::optional<cse::time_point> overrideStartTime_;
    std::shared_ptr<cse::algorithm> overrideAlgorithm_;
    std::shared_ptr<cse::model_uri_resolver> modelResolver_;
};

} // namespace cse

#endif
