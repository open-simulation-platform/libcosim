
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

/**
 * Class for loading an execution based on a SystemStructure.ssd file.
 */
class ssp_loader
{

public:

    /**
     * Constructs a `ssp_loader` with default values
     */
    ssp_loader();

    /**
     * Constructs a `ssp_loader` using a custom `model_uri_resolver`
     *
     * \param [in] modelResolver
     *      The `model_uri_resolver` to use.
     */
    explicit ssp_loader(const std::shared_ptr<cse::model_uri_resolver>& modelResolver);

    /**
     *  Explicitly specify the simulation start time
     *  Will override any value found in the ssp.
     *
     *  \param [in] timePoint
     *      The (logical) time point at which the simulation should start
     */
    void set_start_time(cse::time_point timePoint);

    /**
     *  Explicitly specify the co-simulation algorithm to use.
     *  Will override any value found in the ssp.
     *
     *  \param [in] algorithm
     *      The co-simulation algorithm to be used in the execution.
     */
    void set_algorithm(std::shared_ptr<cse::algorithm> algorithm);

    /**
     *  Creates an execution based on a SystemStructure.ssd file.
     *
     *  \param [in] configPath
     *      Path to an .ssd file, or a directory holding SystemStructure.ssd
     */
    std::pair<execution, simulator_map> load(const boost::filesystem::path& configPath);

private:
    std::optional<cse::time_point> overrideStartTime_;
    std::shared_ptr<cse::algorithm> overrideAlgorithm_;
    const std::shared_ptr<cse::model_uri_resolver> modelResolver_;

};

} // namespace cse

#endif //CSECORE_SSP_LOADER_HPP
