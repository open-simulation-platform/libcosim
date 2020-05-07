/**
 *  \file
 *  Windows-specific things.
 */
#ifndef COSIM_FMI_WINDOWS_HPP
#define COSIM_FMI_WINDOWS_HPP
#ifdef _WIN32

#    include <boost/filesystem.hpp>

#    include <string>


namespace cosim
{
namespace fmi
{
namespace detail
{


/**
 *  Temporarily adds a path to the `PATH` environment variable for the current
 *  process.
 *
 *  The path is added to `PATH` when the class is instantiated, and removed
 *  again when the instance is destroyed.
 *
 *  The purpose of this class is to add an FMU's `binaries/<platform>` directory
 *  to Windows' DLL search path.  This solves a problem where Windows was
 *  unable to locate some DLLs that are indirectly loaded.  Specifically,
 *  the problem has been observed when the main FMU model DLL runs Java code
 *  (through JNI), and that Java code loaded a second DLL, which again was
 *  linked to further DLLs.  The latter were located in the
 *  `binaries/<platform>` directory, but were not found by the dynamic loader
 *  because that directory was not in the search path.
 *
 *  Since environment variables are shared by the entire process, the functions
 *  use a mutex to protect against concurrent access to the `PATH` variable
 *  while it's being read, modified and written.  (This does not protect
 *  against access by client code, of course, which is a potential source of
 *  bugs.)
 */
class additional_path
{
public:
    /// Constructor. Adds `p` to `PATH`.
    additional_path(const boost::filesystem::path& p);

    /// Destructor.  Removes the path from `PATH` again.
    ~additional_path();

private:
    std::wstring addedPath_;
};


} // namespace detail


/// Given `path/to/fmu`, returns `path/to/fmu/binaries/<platform>`
boost::filesystem::path fmu_binaries_dir(const boost::filesystem::path& baseDir);


} // namespace fmi
} // namespace cosim

#endif // _WIN32
#endif // header guard
