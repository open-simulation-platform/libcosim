/**
 *  \file
 *  Interfaces for orchestration of remote as well as local simulations.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_ORCHESTRATION_HPP
#define COSIM_ORCHESTRATION_HPP

#include <cosim/file_cache.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/model_description.hpp>
#include <cosim/slave.hpp>
#include <cosim/uri.hpp>

#include <memory>
#include <string_view>
#include <vector>


namespace cosim
{


/// A model, i.e., a blueprint from which slaves can be instantiated.
class model
{
public:
    virtual ~model() noexcept = default;

    /// Returns a description of this model.
    virtual std::shared_ptr<const model_description> description() const noexcept = 0;

    /// Instantiates a slave.
    virtual std::shared_ptr<slave> instantiate(std::string_view name) = 0;
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
     *  Tries to resolve a model URI relative to some base URI.
     *
     *  Returns a `model` object for the model referred to by the resulting
     *  URI, or null if this resolver is not designed to handle such URIs.
     *  May also throw an exception if the URI would normally be handled,
     *  but the address resolution failed (e.g. due to I/O error).
     *
     *  \note
     *      The default implementation of this function resolves
     *      `modelUriReference` relative to `baseUri` in an RFC 3986
     *      compliant manner using `cosim::resolve_uri_reference()` and
     *      forwards to `lookup_model(const uri&)`.  Specific sub-resolvers
     *      may override it to use non-standard resolution mechanisms.
     *
     *  \param [in] baseUri
     *      An (absolute) base URI.
     *  \param [in] modelUriReference
     *      A model URI reference that will be resolved relative to `baseUri`.
     */
    virtual std::shared_ptr<model> lookup_model(
        const uri& baseUri,
        const uri& modelUriReference);

    /**
     *  Tries to resolve a model URI.
     *
     *  Returns a `model` object for the model referred to by `modelUri`,
     *  or null if this resolver is not designed to handle such URIs.
     *  May also throw an exception if the URI would normally be handled,
     *  but the address resolution failed (e.g. due to I/O error).
     *
     *  \param [in] modelUri
     *      An (absolute) model URI.
     */
    virtual std::shared_ptr<model> lookup_model(const uri& modelUri) = 0;
};


/**
 *  A generic model URI resolver.
 *
 *  This class groups resolvers for multiple model URI schemes into one.
 *  Use `default_model_uri_resolver()` to create one which handles all
 *  schemes that have built-in support in libcosim.
 *
 *  A custom URI resolver can be created by starting with a default-constructed
 *  object and adding scheme-specific resolvers using `add_sub_resolver()`.
 */
class model_uri_resolver
{
public:
    /// Constructs an empty URI resolver.
    model_uri_resolver() noexcept;

    model_uri_resolver(model_uri_resolver&&) noexcept;
    model_uri_resolver& operator=(model_uri_resolver&&) noexcept;

    model_uri_resolver(const model_uri_resolver&) = delete;
    model_uri_resolver& operator=(const model_uri_resolver&) = delete;

    ~model_uri_resolver() noexcept;

    /// Adds a sub-resolver.
    void add_sub_resolver(std::shared_ptr<model_uri_sub_resolver> sr);

    /**
     *  Tries to resolve a model URI reference relative to some base URI.
     *
     *  The URIs will be passed to each of the sub-resolvers in turn,
     *  in the order they were added, until one of them succeeds (or throws).
     *
     *  \param [in] baseUri
     *      An (absolute) base URI.
     *  \param [in] modelUriReference
     *      A model URI reference that will be resolved relative to `baseUri`.
     *
     *  \returns
     *      The model referred to by `modelUriReference`.
     *
     *  \throws std::invalid_argument
     *      if neither of `baseUri` or `modelUriReference` are absolute.
     *  \throws std::runtime_error
     *      if URI resolution failed.
     */
    std::shared_ptr<model> lookup_model(
        const uri& baseUri,
        const uri& modelUriReference);

    /**
     *  Tries to resolve the given model URI.
     *
     *  The URI will be passed to each of the sub-resolvers in turn,
     *  in the order they were added, until one of them succeeds (or throws).
     *
     *  \param [in] modelUri
     *      An (absolute) model URI.
     *
     *  \returns
     *      The model referred to by `modelUri`.
     *
     *  \throws std::invalid_argument
     *      if `modelUri` is not absolute.
     *  \throws std::runtime_error
     *      if URI resolution failed.
     */
    std::shared_ptr<model> lookup_model(const uri& modelUri);

private:
    std::vector<std::shared_ptr<model_uri_sub_resolver>> subResolvers_;
};


/// A resolver for `file://` model URIs with .fmu file extension.
class fmu_file_uri_sub_resolver : public model_uri_sub_resolver
{
public:
    fmu_file_uri_sub_resolver();

    explicit fmu_file_uri_sub_resolver(std::shared_ptr<file_cache> cache);

    std::shared_ptr<model> lookup_model(const uri& modelUri) override;

private:
    std::shared_ptr<fmi::importer> importer_;
};


/**
 *  Returns a resolver for all URI schemes supported natively by libcosim.
 *
 *  If `cache` is not null, it will be used for caching by the URI
 *  resolvers that support it (e.g. for unpacking of FMUs by the `file`
 *  URI resolver).
 */
std::shared_ptr<model_uri_resolver> default_model_uri_resolver(
    std::shared_ptr<file_cache> cache = nullptr);


} // namespace cosim
#endif // header guard
