
#ifndef CSECORE_CSE_CONFIG_LOADER_HPP
#define CSECORE_CSE_CONFIG_LOADER_HPP

#include <cse/config_loader.hpp>


namespace cse
{

/**
 * Class for loading an execution based on a OspSystemStructure.xml file.
 */
class cse_config_loader: public config_loader
{

public:

    std::pair<execution, simulator_map> load(const boost::filesystem::path& configPath) override;

};

} // namespace cse

#endif
