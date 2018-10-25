# CMake generated Testfile for 
# Source directory: C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp
# Build directory: C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/test/cpp
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(cpp_async_slave_mockup_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_async_slave_mockup_test.exe")
add_test(cpp_hello_world_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_hello_world_test.exe")
add_test(cpp_fmi_v1_fmu_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_fmi_v1_fmu_unittest.exe")
set_tests_properties(cpp_fmi_v1_fmu_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
add_test(cpp_fmi_v2_fmu_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_fmi_v2_fmu_unittest.exe")
set_tests_properties(cpp_fmi_v2_fmu_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
add_test(cpp_libevent_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_libevent_unittest.exe")
set_tests_properties(cpp_libevent_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
add_test(cpp_utility_filesystem_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_utility_filesystem_unittest.exe")
set_tests_properties(cpp_utility_filesystem_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
add_test(cpp_utility_uuid_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_utility_uuid_unittest.exe")
set_tests_properties(cpp_utility_uuid_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
add_test(cpp_utility_zip_unittest "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/cpp_utility_zip_unittest.exe")
set_tests_properties(cpp_utility_zip_unittest PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/cpp/../data")
