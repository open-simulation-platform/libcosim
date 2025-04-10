/**
 *  \file
 *  Classes for dealing with FMI 1.0 FMUs.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_V1_FMU_HPP
#define COSIM_FMI_V1_FMU_HPP

#include <cosim/file_cache.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/model_description.hpp>
#include <cosim/time.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>


struct fmi1_import_t;


namespace cosim
{
namespace fmi
{

#ifdef _WIN32
namespace detail
{
class additional_path;
}
#endif

namespace v1
{

class slave_instance;


/**
 *  A class which represents an imported FMI 1.0 FMU.
 *
 *  This class is an implementation of cosim::fmi::fmu specialised for FMUs that
 *  implement FMI v1.0.
 */
class fmu : public fmi::fmu, public std::enable_shared_from_this<fmu>
{
private:
    // Only fmi::importer is allowed to instantiate this class.
    friend class fmi::importer;
    fmu(
        std::shared_ptr<fmi::importer> importer,
        std::unique_ptr<file_cache::directory_ro> fmuDir);

public:
    // Disable copy and move
    fmu(const fmu&) = delete;
    fmu& operator=(const fmu&) = delete;
    fmu(fmu&&) = delete;
    fmu& operator=(fmu&&) = delete;

    ~fmu();

    // fmi::fmu methods
    fmi::fmi_version fmi_version() const override;

    std::shared_ptr<const cosim::model_description>
    model_description() const override;

    std::shared_ptr<fmi::slave_instance> instantiate_slave(
        std::string_view instanceName) override
    {
        return std::static_pointer_cast<fmi::slave_instance>(
            instantiate_v1_slave(instanceName));
    }

    std::shared_ptr<fmi::importer> importer() const override;

    /**
     *  Creates a new co-simulation slave instance.
     *
     *  This is equivalent to `instantiate_slave()`, except that the returned
     *  object is statically typed as an FMI 1.0 slave.
     */
    std::shared_ptr<v1::slave_instance> instantiate_v1_slave(
        std::string_view instanceName);

    /// The path to the directory in which this FMU was unpacked.
    cosim::filesystem::path directory() const;

    /// Returns the underlying C API handle (for FMI Library)
    fmi1_import_t* fmilib_handle() const;

private:
    std::shared_ptr<fmi::importer> importer_;
    std::unique_ptr<file_cache::directory_ro> dir_;

    fmi1_import_t* handle_;
    cosim::model_description modelDescription_;
    std::vector<std::weak_ptr<slave_instance>> instances_;

#ifdef _WIN32
    // Workaround for problems with locating DLLs on Windows
    std::unique_ptr<detail::additional_path> additionalDllSearchPath_;
#endif
};


/// An FMI 1.0 co-simulation slave instance.
class slave_instance : public fmi::slave_instance
{
private:
    // Only FMU1 is allowed to instantiate this class.
    friend std::shared_ptr<slave_instance> fmu::instantiate_v1_slave(std::string_view);
    slave_instance(std::shared_ptr<v1::fmu> fmu, std::string_view instanceName);

public:
    // Disable copy and move.
    slave_instance(const slave_instance&) = delete;
    slave_instance& operator=(const slave_instance&) = delete;
    slave_instance(slave_instance&&) = delete;
    slave_instance& operator=(slave_instance&&) = delete;

    ~slave_instance() noexcept;

    // cosim::slave methods
    void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;
    void start_simulation() override;
    void end_simulation() override;
    step_result do_step(time_point currentT, duration deltaT) override;

    void get_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<double> values) const override;
    void get_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<int> values) const override;
    void get_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<bool> values) const override;
    void get_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::string> values) const override;

    void set_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const double> values) override;
    void set_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const int> values) override;
    void set_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const bool> values) override;
    void set_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::string> values) override;

    state_index save_state() override;
    void save_state(state_index overwriteState) override;
    void restore_state(state_index state) override;
    void release_state(state_index state) override;
    serialization::node export_state(state_index stateIndex) const override;
    state_index import_state(const serialization::node& exportedState) override;

    // fmi::slave_instance methods
    std::shared_ptr<fmi::fmu> fmu() const override
    {
        return std::static_pointer_cast<fmi::fmu>(v1_fmu());
    }

    /// Returns the same object as `fmu()`, only statically typed as a `v1::fmu`.
    std::shared_ptr<v1::fmu> v1_fmu() const;

    /// Returns the underlying C API handle (for FMI Library)
    fmi1_import_t* fmilib_handle() const;

private:
    std::shared_ptr<v1::fmu> fmu_;
    fmi1_import_t* handle_;

    bool simStarted_ = false;

    std::string instanceName_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
};


} // namespace v1
} // namespace fmi
} // namespace cosim
#endif // header guard
