
#ifndef CSECORE_SSP_LOADER_HPP
#define CSECORE_SSP_LOADER_HPP


#include <cse/config_loader.hpp>


namespace cse
{

/**
 * Class for loading an execution based on a SystemStructure.ssd file.
 */
class ssp_loader: public config_loader
{

public:

    std::pair<execution, simulator_map> load(const boost::filesystem::path& configPath) override;

};

} // namespace cse

#endif //CSECORE_SSP_LOADER_HPP
