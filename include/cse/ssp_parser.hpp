
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <boost/filesystem/path.hpp>

#include <cse/execution.hpp>
#include <cse/model.hpp>


namespace cse
{

execution load_ssp(const boost::filesystem::path& sspDir, cse::time_point startTime);

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
