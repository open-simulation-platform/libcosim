/**
 *  \file
 *  Classes for dealing with FMI 1.0 FMUs.
 */
#ifndef CSE_FMI_V1_FMU_HPP
#define CSE_FMI_V1_FMU_HPP

#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/model.hpp>

#include <boost/filesystem.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>


struct fmi1_import_t;


namespace cse
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
 *  This class is an implementation of cse::fmi::fmu specialised for FMUs that
 *  implement FMI v1.0.
*/
class fmu : public fmi::fmu, public std::enable_shared_from_this<fmu>
{
private:
    // Only fmi::importer is allowed to instantiate this class.
    friend class fmi::importer;
    fmu(
        std::shared_ptr<fmi::importer> importer,
        const boost::filesystem::path& fmuDir);

public:
    // Disable copy and move
    fmu(const fmu&) = delete;
    fmu& operator=(const fmu&) = delete;
    fmu(fmu&&) = delete;
    fmu& operator=(fmu&&) = delete;

    ~fmu();

    // fmi::fmu methods
    fmi::fmi_version fmi_version() const override;

    std::shared_ptr<const cse::model_description>
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
    boost::filesystem::path directory() const;

    /// Returns the underlying C API handle (for FMI Library)
    fmi1_import_t* fmilib_handle() const;

private:
    std::shared_ptr<fmi::importer> importer_;
    boost::filesystem::path dir_;

    fmi1_import_t* handle_;
    cse::model_description modelDescription_;
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

    // cse::slave methods
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
} // namespace cse
#endif // header guard
