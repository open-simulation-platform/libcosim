#ifndef CSE_ORCHESTRATION_HPP
#define CSE_ORCHESTRATION_HPP

#include <cse/async_slave.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/model.hpp>

#include <memory>
#include <string_view>
#include <vector>


namespace cse
{
namespace detail
{
// Generic implementation of both model_uri_resolver and slave_uri_resolver.
// Nevermind this for now, look further down first.
template<
    typename Resource,
    typename SubResolver,
    std::shared_ptr<Resource> (SubResolver::*resolveFun)(std::string_view)>
class generic_uri_resolver
{
public:
    void add_sub_resolver(std::shared_ptr<SubResolver> sr)
    {
        subResolvers_.push_back(sr);
    }

    std::shared_ptr<Resource> resolve(std::string_view baseUri, std::string_view uri)
    {
        std::string _uri;
        
        if ((uri.find(':') == std::string_view::npos) || uri[0] == '.')
        {
            _uri = std::string(baseUri) + "/" + std::string(uri); 
        } else {
            _uri = std::string(uri); 
        }

        for (auto sr : subResolvers_) {
            auto r = ((*sr).*resolveFun)(_uri);
            if (r) return r;
        }
        throw std::runtime_error(
            std::string("No resolvers available to handle URI: ") + std::string(uri));
    }

private:
    std::vector<std::shared_ptr<SubResolver>> subResolvers_;
};
} // namespace detail


/**
 *  A model, i.e., a blueprint from which slaves can be instantiated.
 *
 *  \todo
 *      Find a better name for this. This basically represents what
 *      we would call an FMU, but I think we should try to find a
 *      non-FMI-specific term for it.
 */
class model
{
public:
    virtual ~model() noexcept = default;

    /// Returns a description of this model.
    virtual std::shared_ptr<const model_description> description() const noexcept = 0;

    /// Instantiates a slave.
    virtual std::shared_ptr<async_slave> instantiate(std::string_view name) = 0;
};


/**
 *  An interface for classes that resolve model URIs of one or more specific
 *  URI schemes.
 *
 *  Examples could be a file URI resolver which handles URIs like
 *  `file:///models/my_model.fmu` or an FMU-proxy URI resolver which handles
 *  URIs like `fmu-proxy://sim.example.com:6345`.
 *
 *  Client code will normally not use this directly to resolve URIs, but rather
 *  as one of many sub-resolvers in a `model_uri_resolver`.
 */
class model_uri_sub_resolver
{
public:
    virtual ~model_uri_sub_resolver() noexcept = default;

    /**
     *  Tries to resolve the given URI.
     *
     *  Returns a `model` object for the model referred to by `uri`, or
     *  null if this resolver is not designed to handle the given URI.
     *  May also throw an exception if the URI would normally be handled,
     *  but the address resolution failed (e.g. due to I/O error).
     */
    virtual std::shared_ptr<model> lookup_model(std::string_view uri) = 0;
};


/**
 *  Groups resolvers for multiple model URI schemes into one.
 */
class model_uri_resolver
{
public:
    /// Adds a sub-resolver.
    void add_sub_resolver(std::shared_ptr<model_uri_sub_resolver> sr)
    {
        resolver_.add_sub_resolver(sr);
    }

    /**
     *  Tries to resolve the given URI.
     *
     *  If `uri` is relative, it will be interpreted relative to
     *  `baseUri`.  Otherwise, `baseUri` is ignored.
     *
     *  The URI will be passed to each of the sub-resolvers in turn,
     *  in the order they were added, until one of them succeeds
     *  (or throws).
     *
     *  Returns a `model` object for the model referred to by `uri`,
     *  or throws an exception if URI resolution failed.
     */
    std::shared_ptr<model> lookup_model(
        std::string_view baseUri,
        std::string_view uri)
    {
        return resolver_.resolve(baseUri, uri);
    }

private:
    detail::generic_uri_resolver<model, model_uri_sub_resolver, &model_uri_sub_resolver::lookup_model>
        resolver_;
};


/**
 *  An interface for model directory/database/discovery services.
 *
 *  Examples include an object that keeps track of normal FMU files in some
 *  directory on the disk, or an object which communicates with an FMU-proxy
 *  discovery service.
 */
class model_directory
{
public:
    /// Contains the name and URI of a single model.
    struct model_info
    {
        std::string name;
        std::string uri;
    };

    /// Returns a list of all models in this directory.
    virtual gsl::span<model_info> models() const noexcept = 0;
};


/**
 *  An interface for classes that resolve slave URIs of one or more specific
 *  URI schemes.
 *
 *  Examples could be a resolver which handles URIs for in-process slaves,
 *  e.g. `cse-inproc://thruster-3`, or a DCP URI resolver which handles
 *  URIs like `dcp-slave://10.0.0.52:9054`.
 *
 *  Client code will normally not use this directly to resolve URIs, but rather
 *  as one of many sub-resolvers in a `slave_uri_resolver`.
 */
class slave_uri_sub_resolver
{
public:
    virtual ~slave_uri_sub_resolver() noexcept = default;

    /**
     *  Tries to resolve the given URI.
     *
     *  Returns an `async_slave` object for the slave referred to by `uri`, or
     *  null if this resolver is not designed to handle the given URI.
     *  May also throw an exception if the URI would normally be handled,
     *  but the address resolution failed (e.g. due to I/O error).
     */
    virtual std::shared_ptr<async_slave> connect_to_slave(std::string_view uri) = 0;
};


/**
 *  Groups resolvers for multiple slave URI schemes into one.
 */
class slave_uri_resolver
{
public:
    /// Adds a sub-resolver.
    void add_sub_resolver(std::shared_ptr<slave_uri_sub_resolver> sr)
    {
        resolver_.add_sub_resolver(sr);
    }

    /**
     *  Tries to resolve the given URI.
     *
     *  The URI will be passed to each of the sub-resolvers in turn,
     *  in the order they were added, until one of them succeeds
     *  (or throws).
     *
     *  Returns an `async_slave` object for the slave referred to by `uri`,
     *  or throws an exception if URI resolution failed.
     */
    std::shared_ptr<async_slave> connect_to_slave(std::string_view baseUri, std::string_view uri)
    {
        return resolver_.resolve(baseUri, uri);
    }

private:
    detail::generic_uri_resolver<async_slave, slave_uri_sub_resolver, &slave_uri_sub_resolver::connect_to_slave>
        resolver_;
};


class file_uri_sub_resolver : public model_uri_sub_resolver
{
public:
    file_uri_sub_resolver();

//    ~file_uri_sub_resolver() noexcept ov;

    std::shared_ptr<model> lookup_model(std::string_view uri) override;

private:
    std::shared_ptr<fmi::importer> importer_;
};


std::shared_ptr<model_uri_resolver> default_model_uri_resolver();

} // namespace cse
#endif // header guard
