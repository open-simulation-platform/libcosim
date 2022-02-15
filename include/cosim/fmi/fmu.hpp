/**
 *  \file
 *  Defines a version-independent FMU interface.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_FMU_HPP
#define COSIM_FMI_FMU_HPP

#include <cosim/model_description.hpp>
#include <cosim/slave.hpp>

#include <memory>
#include <string_view>


namespace cosim
{
namespace fmi
{


/// Constants that refer to FMI version numbers.
enum class fmi_version
{
    /// Unknown (or possibly unsupported)
    unknown = 0,

    /// FMI 1.0
    v1_0 = 10000,

    /// FMI 2.0
    v2_0 = 20000,
};


class importer;
class slave_instance;


/**
 *  An interface for classes that represent imported FMUs.
 *
 *  This is an abstract class which only defines the functions that are common
 *  between different FMI versions.  Use `fmi::importer::import()` to  import
 *  an FMU and create an `fmu` object.
 */
class fmu
{
public:
    /// Which FMI standard version is used in this FMU.
    virtual fmi::fmi_version fmi_version() const = 0;

    /// A description of this FMU.
    virtual std::shared_ptr<const cosim::model_description>
    model_description() const = 0;

    /// Creates a co-simulation slave instance of this FMU.
    virtual std::shared_ptr<slave_instance> instantiate_slave(
        std::string_view instanceName) = 0;

    /// The `fmi::importer` which was used to import this FMU.
    virtual std::shared_ptr<fmi::importer> importer() const = 0;

    virtual ~fmu() { }

    void disable_logging(bool disabled) { logging_disabled = disabled; }

    bool is_logging_disabled() const { return logging_disabled; }

protected:
    bool logging_disabled;
};


/// An FMI co-simulation slave instance.
class slave_instance : public cosim::slave
{
public:
    /// Returns a reference to the FMU of which this is an instance.
    virtual std::shared_ptr<fmi::fmu> fmu() const = 0;

    cosim::model_description model_description() const final override
    {
        return *(fmu()->model_description());
    }

    virtual ~slave_instance() { }
};


} // namespace fmi
} // namespace cosim
#endif // header guard
