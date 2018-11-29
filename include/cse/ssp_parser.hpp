
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <exception>
#include <string>

#include <boost/filesystem/path.hpp>

#include <cse/algorithm.hpp>
#include <cse/fmi/fmu.hpp>


namespace cse
{

class ssp_parser
{

public:
    ssp_parser(boost::filesystem::path xmlPath);
    ~ssp_parser() noexcept;

private:
    boost::filesystem::path xmlPath_;
    boost::property_tree::ptree pt_;
    std::string name_;
    std::string description_;

    cse::algorithm *algo_;
    std::vector<cse::fmi::fmu> fmus_;

};

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
