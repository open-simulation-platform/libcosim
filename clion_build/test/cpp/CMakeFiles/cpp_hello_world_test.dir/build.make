# CMAKE generated file: DO NOT EDIT!
# Generated by "NMake Makefiles" Generator, CMake Version 3.12

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE
NULL=nul
!ENDIF
SHELL = cmd.exe

# The CMake executable.
CMAKE_COMMAND = "C:\Program Files\JetBrains\CLion 2018.2.4\bin\cmake\win\bin\cmake.exe"

# The command to remove a file.
RM = "C:\Program Files\JetBrains\CLion 2018.2.4\bin\cmake\win\bin\cmake.exe" -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = C:\Dev\osp\cse-core-branches\master\cse-core

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = C:\Dev\osp\cse-core-branches\master\cse-core\clion_build

# Include any dependencies generated for this target.
include test\cpp\CMakeFiles\cpp_hello_world_test.dir\depend.make

# Include the progress variables for this target.
include test\cpp\CMakeFiles\cpp_hello_world_test.dir\progress.make

# Include the compile flags for this target's objects.
include test\cpp\CMakeFiles\cpp_hello_world_test.dir\flags.make

test\cpp\CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.obj: test\cpp\CMakeFiles\cpp_hello_world_test.dir\flags.make
test\cpp\CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.obj: ..\test\cpp\hello_world_test.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object test/cpp/CMakeFiles/cpp_hello_world_test.dir/hello_world_test.cpp.obj"
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp
	C:\PROGRA~2\MICROS~3\2017\COMMUN~1\VC\Tools\MSVC\1415~1.267\bin\Hostx64\x64\cl.exe @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) /FoCMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.obj /FdCMakeFiles\cpp_hello_world_test.dir\ /FS -c C:\Dev\osp\cse-core-branches\master\cse-core\test\cpp\hello_world_test.cpp
<<
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build

test\cpp\CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/cpp_hello_world_test.dir/hello_world_test.cpp.i"
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp
	C:\PROGRA~2\MICROS~3\2017\COMMUN~1\VC\Tools\MSVC\1415~1.267\bin\Hostx64\x64\cl.exe > CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.i @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E C:\Dev\osp\cse-core-branches\master\cse-core\test\cpp\hello_world_test.cpp
<<
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build

test\cpp\CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/cpp_hello_world_test.dir/hello_world_test.cpp.s"
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp
	C:\PROGRA~2\MICROS~3\2017\COMMUN~1\VC\Tools\MSVC\1415~1.267\bin\Hostx64\x64\cl.exe @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) /FoNUL /FAs /FaCMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.s /c C:\Dev\osp\cse-core-branches\master\cse-core\test\cpp\hello_world_test.cpp
<<
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build

# Object files for target cpp_hello_world_test
cpp_hello_world_test_OBJECTS = \
"CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.obj"

# External object files for target cpp_hello_world_test
cpp_hello_world_test_EXTERNAL_OBJECTS =

output\debug\bin\cpp_hello_world_test.exe: test\cpp\CMakeFiles\cpp_hello_world_test.dir\hello_world_test.cpp.obj
output\debug\bin\cpp_hello_world_test.exe: test\cpp\CMakeFiles\cpp_hello_world_test.dir\build.make
output\debug\bin\cpp_hello_world_test.exe: output\debug\lib\csecorecpp.lib
output\debug\bin\cpp_hello_world_test.exe: C:\Users\STENBRO\.conan\data\FMILibrary\2.0.3\kyllingstad\testing\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\lib\fmilib.lib
output\debug\bin\cpp_hello_world_test.exe: C:\Users\STENBRO\.conan\data\libzip\1.5.1\bincrafters\stable\package\c97299ee8d7194d38a91af89230200a9897c2ac6\lib\zip.lib
output\debug\bin\cpp_hello_world_test.exe: C:\Users\STENBRO\.conan\data\libevent\2.0.22\bincrafters\stable\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\lib\libevent_core.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\dltnkstp\1\context\lib\libboost_context.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\65ffv82q\1\fiber\lib\libboost_fiber.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\696yzwhj\1\log\lib\libboost_log.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\696yzwhj\1\log\lib\libboost_log_setup.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\9snh7ghz\1\system\lib\libboost_system.lib
output\debug\bin\cpp_hello_world_test.exe: C:\.conan\1mjcvo_e\1\thread\lib\libboost_thread.lib
output\debug\bin\cpp_hello_world_test.exe: test\cpp\CMakeFiles\cpp_hello_world_test.dir\objects1.rsp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ..\..\output\debug\bin\cpp_hello_world_test.exe"
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp
	"C:\Program Files\JetBrains\CLion 2018.2.4\bin\cmake\win\bin\cmake.exe" -E vs_link_exe --intdir=CMakeFiles\cpp_hello_world_test.dir --manifests  -- C:\PROGRA~2\MICROS~3\2017\COMMUN~1\VC\Tools\MSVC\1415~1.267\bin\Hostx64\x64\link.exe /nologo @CMakeFiles\cpp_hello_world_test.dir\objects1.rsp @<<
 /out:..\..\output\debug\bin\cpp_hello_world_test.exe /implib:..\..\output\debug\lib\cpp_hello_world_test.lib /pdb:C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\output\debug\lib\cpp_hello_world_test.pdb /version:0.0  /machine:x64   /debug /INCREMENTAL  /subsystem:console  -LIBPATH:C:\.conan\65ffv82q\1\fiber\lib  -LIBPATH:C:\.conan\696yzwhj\1\log\lib  -LIBPATH:C:\.conan\8w78ha1p\1\test\lib  -LIBPATH:C:\.conan\lb164xev\1\uuid\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\FMILibrary\2.0.3\kyllingstad\testing\package\8cf01e2f50fcd6b63525e70584df0326550364e1\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\libevent\2.0.22\bincrafters\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\libzip\1.5.1\bincrafters\stable\package\bde6e51fe4777fd4036de780a4cb01d00558aab5\lib  -LIBPATH:C:\.conan\gdr7rwuk\1\property_tree\lib  -LIBPATH:C:\.conan\8m3ivvij\1\align\lib  -LIBPATH:C:\.conan\5lx3n0v0\1\asio\lib  -LIBPATH:C:\.conan\n7z6xi1v\1\interprocess\lib  -LIBPATH:C:\.conan\9c02i2_k\1\parameter\lib  -LIBPATH:C:\.conan\_409j7qx\1\xpressive\lib  -LIBPATH:C:\.conan\12g9l_ta\1\timer\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\OpenSSL\1.0.2o\conan\stable\package\009a50ddeb47afbc9361cbc63650560c127e1234\lib  -LIBPATH:C:\.conan\bd4wo7q_\1\any\lib  -LIBPATH:C:\.conan\pezb8vvg\1\format\lib  -LIBPATH:C:\.conan\7uvzgp63\1\multi_index\lib  -LIBPATH:C:\.conan\u24_2ed4\1\coroutine\lib  -LIBPATH:C:\.conan\73xf1_4x\1\python\lib  -LIBPATH:C:\.conan\dltnkstp\1\context\lib  -LIBPATH:C:\.conan\1mjcvo_e\1\date_time\lib  -LIBPATH:C:\.conan\1mjcvo_e\1\locale\lib  -LIBPATH:C:\.conan\1mjcvo_e\1\serialization\lib  -LIBPATH:C:\.conan\1mjcvo_e\1\thread\lib  -LIBPATH:C:\.conan\dmsp1z0p\1\algorithm\lib  -LIBPATH:C:\.conan\cmqz_af4\1\filesystem\lib  -LIBPATH:C:\.conan\lkrb_agz\1\phoenix\lib  -LIBPATH:C:\.conan\c5y130ep\1\chrono\lib  -LIBPATH:C:\.conan\48vwy9lx\1\foreach\lib  -LIBPATH:C:\.conan\xun12f1f\1\endian\lib  -LIBPATH:C:\.conan\crnnm5yp\1\iostreams\lib  -LIBPATH:C:\.conan\j7j24b45\1\tokenizer\lib  -LIBPATH:C:\.conan\xvcolo8j\1\tti\lib  -LIBPATH:C:\.conan\zf_v5fit\1\variant\lib  -LIBPATH:C:\.conan\6lrrehej\1\exception\lib  -LIBPATH:C:\.conan\dqfa8wnu\1\unordered\lib  -LIBPATH:C:\.conan\r6qxccyv\1\proto\lib  -LIBPATH:C:\.conan\cbp2vlhm\1\io\lib  -LIBPATH:C:\.conan\7wdnwtpy\1\random\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\zlib\1.2.11\conan\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\bzip2\1.0.6\conan\stable\package\d2eed54dd60974c85a4e38a37976c95179d53fa0\lib  -LIBPATH:C:\.conan\ga77pzie\1\ratio\lib  -LIBPATH:C:\Users\STENBRO\.conan\data\lzma\5.2.3\bincrafters\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\lib  -LIBPATH:C:\.conan\9snh7ghz\1\system\lib  -LIBPATH:C:\.conan\7v9j7ih3\1\rational\lib  -LIBPATH:C:\.conan\jgnkl6gy\1\winapi\lib  -LIBPATH:C:\.conan\7638n855\1\math\lib  -LIBPATH:C:\.conan\7gjbm9yb\1\range\lib  -LIBPATH:C:\.conan\f3026id6\1\atomic\lib  -LIBPATH:C:\.conan\obvqm6oe\1\container\lib  -LIBPATH:C:\.conan\00tujgmq\1\lambda\lib  -LIBPATH:C:\.conan\j0w6hbsu\1\array\lib  -LIBPATH:C:\.conan\4_khkk_8\1\tuple\lib  -LIBPATH:C:\.conan\k6b5gw1t\1\intrusive\lib  -LIBPATH:C:\.conan\66e8ob3o\1\regex\lib  -LIBPATH:C:\.conan\rolzggkg\1\numeric_conversion\lib  -LIBPATH:C:\.conan\tbsmp1x1\1\bind\lib  -LIBPATH:C:\.conan\8kv9uoto\1\smart_ptr\lib  -LIBPATH:C:\.conan\93q_s_to\1\preprocessor\lib  -LIBPATH:C:\.conan\a0mqe2tu\1\integer\lib  -LIBPATH:C:\.conan\h1e6ie63\1\throw_exception\lib  -LIBPATH:C:\.conan\i4gdpd5o\1\type_traits\lib  -LIBPATH:C:\.conan\u4z4o7my\1\predef\lib  -LIBPATH:C:\.conan\hm_55de5\1\move\lib  -LIBPATH:C:\.conan\alwyj0a_\1\core\lib  -LIBPATH:C:\.conan\i0d9rf7t\1\static_assert\lib  -LIBPATH:C:\.conan\veok39ai\1\assert\lib  -LIBPATH:C:\.conan\12u7nsry\1\config\lib ..\..\output\debug\lib\csecorecpp.lib C:\Users\STENBRO\.conan\data\FMILibrary\2.0.3\kyllingstad\testing\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\lib\fmilib.lib C:\Users\STENBRO\.conan\data\libzip\1.5.1\bincrafters\stable\package\c97299ee8d7194d38a91af89230200a9897c2ac6\lib\zip.lib C:\Users\STENBRO\.conan\data\libevent\2.0.22\bincrafters\stable\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\lib\libevent_core.lib ws2_32.lib C:\.conan\dltnkstp\1\context\lib\libboost_context.lib C:\.conan\65ffv82q\1\fiber\lib\libboost_fiber.lib C:\.conan\696yzwhj\1\log\lib\libboost_log.lib C:\.conan\696yzwhj\1\log\lib\libboost_log_setup.lib C:\.conan\9snh7ghz\1\system\lib\libboost_system.lib C:\.conan\1mjcvo_e\1\thread\lib\libboost_thread.lib kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib 
<<
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build

# Rule to build all files generated by this target.
test\cpp\CMakeFiles\cpp_hello_world_test.dir\build: output\debug\bin\cpp_hello_world_test.exe

.PHONY : test\cpp\CMakeFiles\cpp_hello_world_test.dir\build

test\cpp\CMakeFiles\cpp_hello_world_test.dir\clean:
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp
	$(CMAKE_COMMAND) -P CMakeFiles\cpp_hello_world_test.dir\cmake_clean.cmake
	cd C:\Dev\osp\cse-core-branches\master\cse-core\clion_build
.PHONY : test\cpp\CMakeFiles\cpp_hello_world_test.dir\clean

test\cpp\CMakeFiles\cpp_hello_world_test.dir\depend:
	$(CMAKE_COMMAND) -E cmake_depends "NMake Makefiles" C:\Dev\osp\cse-core-branches\master\cse-core C:\Dev\osp\cse-core-branches\master\cse-core\test\cpp C:\Dev\osp\cse-core-branches\master\cse-core\clion_build C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp C:\Dev\osp\cse-core-branches\master\cse-core\clion_build\test\cpp\CMakeFiles\cpp_hello_world_test.dir\DependInfo.cmake --color=$(COLOR)
.PHONY : test\cpp\CMakeFiles\cpp_hello_world_test.dir\depend

