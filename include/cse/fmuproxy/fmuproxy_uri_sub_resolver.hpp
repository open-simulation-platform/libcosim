
#ifndef CSE_FMUPROXY_URI_SUB_RESOLVER_HPP
#define CSE_FMUPROXY_URI_SUB_RESOLVER_HPP

#include <cse/orchestration.hpp>

namespace cse
{

namespace fmuproxy
{

/**
 *  Class for resolving fmu-proxy URI schemes.
 *
 *  Supports file, url and guid (pre-loaded on server) FMUs
 *
 *  From file: `fmu-proxy://127.0.0.1:9090?file=models/my_model.fmu`
 *  From url: `fmu-proxy://127.0.0.1:9090?url=http://example.com/my_model.fmu`
 *  From guid: `fmu-proxy://127.0.0.1:9090?guid={ads4ffsa4523fgg8}`
 */
class fmuproxy_uri_sub_resolver : public model_uri_sub_resolver
{

public:
    std::shared_ptr<model> lookup_model(const uri& baseUri, const uri& modelUriReference) override;

    std::shared_ptr<model> lookup_model(const cse::uri& uri) override;
};

} // namespace fmuproxy

} // namespace cse

#endif
