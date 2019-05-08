
#ifndef CSE_FMUPROXY_SUB_RESOLVER_HPP
#define CSE_FMUPROXY_SUB_RESOLVER_HPP

#include <cse/orchestration.hpp>

namespace cse
{

namespace fmuproxy
{

class fmuproxy_uri_sub_resolver : public model_uri_sub_resolver
{

public:
    std::shared_ptr<model> lookup_model(std::string_view uri) override;
};

} // namespace fmuproxy

} // namespace cse

#endif //CSE_FMUPROXY_SUB_RESOLVER_HPP
