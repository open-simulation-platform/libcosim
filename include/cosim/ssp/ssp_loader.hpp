#ifndef LIBCOSIM_SSP_LOADER_HPP
#define LIBCOSIM_SSP_LOADER_HPP

#include <cosim/algorithm/algorithm.hpp>
#include <cosim/model.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/system_structure.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>


namespace cosim
{

/// A configuration loaded from an SSP file.
struct ssp_configuration
{
    /// The system structure.
    cosim::system_structure system_structure;

    /// The start time.
    time_point start_time;

    /// The co-simulation algorithm.
    std::shared_ptr<cosim::algorithm> algorithm;

    /**
     *  Named parameter sets.
     *
     *  This always contains at least one parameter set whose key is the
     *  empty string, which represents the default parameter values.
     *  (If the SSP configuration does not specify any parameter values,
     *  this set will be empty but nevertheless present.)
     */
    std::unordered_map<std::string, variable_value_map> parameter_sets;
};


/// Class for loading an execution from a SSP configuration.
class ssp_loader
{

public:
    ssp_loader();

    /**
     * Assign a custom `model_uri_resolver`.
     *
     * \param [in] modelResolver
     *      The `model_uri_resolver` to use by the loader.
     */
    void set_model_uri_resolver(std::shared_ptr<cosim::model_uri_resolver> resolver);

    /**
     * Specify a non-default SystemStructureDefinition (.ssd) file to load.
     *
     * \param [in] name
     *      The name of the (custom) SystemStructureDefinition file to load.
     */
    void set_ssd_file_name(const std::string& name);

    /**
     *  Load an SSP configuration.
     *
     *  \param [in] configPath
     *      Path to the .ssp archive, or a directory holding one or more .ssd files.
     */
    ssp_configuration load(const boost::filesystem::path& configPath);

private:
    std::optional<std::string> ssdFileName_;
    std::shared_ptr<cosim::model_uri_resolver> modelResolver_;
};


} // namespace cosim
#endif
