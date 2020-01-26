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

class ssp_loader
{

public:
    ssp_loader();

    /**
     *  Explicitly specify the simulation start time
     *  Will override any value found in the configuration file.
     *
     *  \param [in] timePoint
     *      The (logical) time point at which the simulation should start
     */
    virtual void override_start_time(cse::time_point timePoint);

    /**
     *  Explicitly specify the co-simulation algorithm to use.
     *  Will override any value found in the configuration file.
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
    void set_model_uri_resolver(std::shared_ptr<cse::model_uri_resolver> resolver);

    void set_ssd_file_name(const std::string& name);

    void set_parameter_set_name(const std::string& name);

    /**
     *  Creates an execution based on a configuration file.
     *
     *  \param [in] configPath
     *      Path to the configuration file, or the directory holding it.
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
