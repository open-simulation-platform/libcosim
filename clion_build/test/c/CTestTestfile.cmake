# CMake generated Testfile for 
# Source directory: C:/Dev/osp/cse-core-branches/master/cse-core/test/c
# Build directory: C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/test/c
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(c_hello_world_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_hello_world_test.exe")
set_tests_properties(c_hello_world_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_single_fmu_execution_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_single_fmu_execution_test.exe")
set_tests_properties(c_single_fmu_execution_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_multiple_fmus_execution_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_multiple_fmus_execution_test.exe")
set_tests_properties(c_multiple_fmus_execution_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_observer_initial_samples_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_observer_initial_samples_test.exe")
set_tests_properties(c_observer_initial_samples_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_observer_can_buffer_samples "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_observer_can_buffer_samples.exe")
set_tests_properties(c_observer_can_buffer_samples PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_observer_multiple_slaves_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_observer_multiple_slaves_test.exe")
set_tests_properties(c_observer_multiple_slaves_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
add_test(c_observer_slave_logging_test "C:/Dev/osp/cse-core-branches/master/cse-core/clion_build/output/debug/bin/c_observer_slave_logging_test.exe")
set_tests_properties(c_observer_slave_logging_test PROPERTIES  ENVIRONMENT "TEST_DATA_DIR=C:/Dev/osp/cse-core-branches/master/cse-core/test/c/../data")
