#define BOOST_TEST_MODULE FMI 2 platform fallback unittest
#include <cosim/exception.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fs_portability.hpp>

#include <boost/test/unit_test.hpp>


// On platforms without an FMI 2 x86 platform name, FMI 2.0.4 uses the FMI 3
// platform tuple (FMI2_TEST_PLATFORM, e.g. "aarch64-darwin"). If an FMU has no
// such folder, the importer falls back to the x86 platform folder (e.g.
// "darwin64"), but only if that contains code for this platform.

using namespace cosim;

namespace
{
std::shared_ptr<fmi::v2::fmu> import_v2(const cosim::filesystem::path& fmuPath)
{
    // Use a separate importer (and cache) for each FMU, as the FMUs we use
    // here may have the same GUID.
    auto importer = fmi::importer::create();
    return std::static_pointer_cast<fmi::v2::fmu>(importer->import(fmuPath));
}
} // namespace


BOOST_AUTO_TEST_CASE(x86_platform_folder_with_native_code_is_used)
{
    const auto fmu = import_v2(FMU_WITH_NATIVE_CODE);
    BOOST_TEST(cosim::filesystem::is_symlink(fmu->directory() / "binaries" / FMI2_TEST_PLATFORM));
    BOOST_CHECK_NO_THROW(fmu->instantiate_v2_slave("instance"));
}


BOOST_AUTO_TEST_CASE(x86_platform_folder_without_native_code_is_not_used)
{
    const auto fmu = import_v2(FMU_WITHOUT_NATIVE_CODE);
    BOOST_TEST(!cosim::filesystem::exists(
        cosim::filesystem::symlink_status(fmu->directory() / "binaries" / FMI2_TEST_PLATFORM)));
    BOOST_CHECK_THROW(fmu->instantiate_v2_slave("instance"), cosim::error);
}
