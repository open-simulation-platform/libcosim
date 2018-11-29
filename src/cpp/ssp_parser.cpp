#include "ssp_parser.hpp"


namespace cse
{

ssp_parser::ssp_parser(boost::filesystem::path xmlPath)
    : xmlPath_(xmlPath)
{
    boost::property_tree::read_xml(xmlPath.string(), pt_);
}

ssp_parser::~ssp_parser() noexcept = default;

} // namespace cse