# CMAKE generated file: DO NOT EDIT!
# Generated by "NMake Makefiles" Generator, CMake Version 3.12

# compile CXX with C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/VC/Tools/MSVC/14.15.26726/bin/Hostx64/x64/cl.exe
CXX_FLAGS = /DWIN32 /D_WINDOWS /W3 /GR /EHsc   /MDd /Zi /Ob0 /Od /RTC1    /W4 /WX /wd4996 -DLZMA_API_STATIC -DBOOST_ALL_NO_LIB=1 -std:c++17

CXX_DEFINES = -DBOOST_ALL_NO_LIB=1 -DFMILibrary_STATIC_LIB_ONLY -DLIBZIP_STATIC_LIB_ONLY -D_SCL_SECURE_NO_WARNINGS -Dcsecorecpp_EXPORTS

CXX_INCLUDES = -IC:\Users\STENBRO\.conan\data\FMILibrary\2.0.3\kyllingstad\testing\package\8cf01e2f50fcd6b63525e70584df0326550364e1\include -IC:\Users\STENBRO\.conan\data\libevent\2.0.22\bincrafters\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\include -IC:\Users\STENBRO\.conan\data\libzip\1.5.1\bincrafters\stable\package\bde6e51fe4777fd4036de780a4cb01d00558aab5\include -IC:\Users\STENBRO\.conan\data\OpenSSL\1.0.2o\conan\stable\package\009a50ddeb47afbc9361cbc63650560c127e1234\include -IC:\Users\STENBRO\.conan\data\zlib\1.2.11\conan\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\include -IC:\Users\STENBRO\.conan\data\bzip2\1.0.6\conan\stable\package\d2eed54dd60974c85a4e38a37976c95179d53fa0\include -IC:\Users\STENBRO\.conan\data\lzma\5.2.3\bincrafters\stable\package\8cf01e2f50fcd6b63525e70584df0326550364e1\include -IC:\Dev\osp\cse-core-branches\master\cse-core\include -IC:\Dev\osp\cse-core-branches\master\cse-core\src\cpp -IC:\.conan\65ffv82q\1\fiber\include -IC:\.conan\696yzwhj\1\log\include -IC:\.conan\8w78ha1p\1\test\include -IC:\.conan\lb164xev\1\uuid\include -IC:\Users\STENBRO\.conan\data\gsl_microsoft\1.0.0\bincrafters\stable\package\5ab84d6acfe1f23c4fae0ab88f26e3a396351ac9\include -IC:\.conan\gdr7rwuk\1\property_tree\include -IC:\.conan\8m3ivvij\1\align\include -IC:\.conan\5lx3n0v0\1\asio\include -IC:\.conan\n7z6xi1v\1\interprocess\include -IC:\.conan\9c02i2_k\1\parameter\include -IC:\.conan\_409j7qx\1\xpressive\include -IC:\.conan\12g9l_ta\1\timer\include -IC:\.conan\bd4wo7q_\1\any\include -IC:\.conan\pezb8vvg\1\format\include -IC:\.conan\7uvzgp63\1\multi_index\include -IC:\.conan\u24_2ed4\1\coroutine\include -IC:\.conan\73xf1_4x\1\python\include -IC:\.conan\73xf1_4x\1 -IC:\.conan\dltnkstp\1\context\include -IC:\.conan\1mjcvo_e\1\date_time\include -IC:\.conan\1mjcvo_e\1\locale\include -IC:\.conan\1mjcvo_e\1\pool\include -IC:\.conan\1mjcvo_e\1\serialization\include -IC:\.conan\1mjcvo_e\1\spirit\include -IC:\.conan\1mjcvo_e\1\thread\include -IC:\.conan\dmsp1z0p\1\algorithm\include -IC:\.conan\cmqz_af4\1\filesystem\include -IC:\.conan\lkrb_agz\1\phoenix\include -IC:\.conan\c5y130ep\1\chrono\include -IC:\.conan\48vwy9lx\1\foreach\include -IC:\.conan\xun12f1f\1\endian\include -IC:\.conan\crnnm5yp\1\iostreams\include -IC:\.conan\j7j24b45\1\tokenizer\include -IC:\.conan\xvcolo8j\1\tti\include -IC:\.conan\zf_v5fit\1\variant\include -IC:\.conan\6lrrehej\1\exception\include -IC:\.conan\dqfa8wnu\1\unordered\include -IC:\.conan\r6qxccyv\1\proto\include -IC:\.conan\cbp2vlhm\1\io\include -IC:\.conan\7wdnwtpy\1\random\include -IC:\.conan\ga77pzie\1\ratio\include -IC:\.conan\9snh7ghz\1\system\include -IC:\.conan\7v9j7ih3\1\rational\include -IC:\.conan\jgnkl6gy\1\winapi\include -IC:\.conan\7638n855\1\lexical_cast\include -IC:\.conan\7638n855\1\math\include -IC:\.conan\7gjbm9yb\1\range\include -IC:\.conan\f3026id6\1\atomic\include -IC:\.conan\obvqm6oe\1\container\include -IC:\.conan\00tujgmq\1\lambda\include -IC:\.conan\j0w6hbsu\1\array\include -IC:\.conan\4_khkk_8\1\tuple\include -IC:\.conan\k6b5gw1t\1\intrusive\include -IC:\.conan\66e8ob3o\1\regex\include -IC:\.conan\rolzggkg\1\numeric_conversion\include -IC:\.conan\1vbxq964\1\concept_check\include -IC:\.conan\1vbxq964\1\conversion\include -IC:\.conan\1vbxq964\1\detail\include -IC:\.conan\1vbxq964\1\function\include -IC:\.conan\1vbxq964\1\function_types\include -IC:\.conan\1vbxq964\1\functional\include -IC:\.conan\1vbxq964\1\fusion\include -IC:\.conan\1vbxq964\1\iterator\include -IC:\.conan\1vbxq964\1\mpl\include -IC:\.conan\1vbxq964\1\optional\include -IC:\.conan\1vbxq964\1\type_index\include -IC:\.conan\1vbxq964\1\typeof\include -IC:\.conan\1vbxq964\1\utility\include -IC:\.conan\tbsmp1x1\1\bind\include -IC:\.conan\8kv9uoto\1\smart_ptr\include -IC:\.conan\93q_s_to\1\preprocessor\include -IC:\.conan\a0mqe2tu\1\integer\include -IC:\.conan\h1e6ie63\1\throw_exception\include -IC:\.conan\i4gdpd5o\1\type_traits\include -IC:\.conan\u4z4o7my\1\predef\include -IC:\.conan\hm_55de5\1\move\include -IC:\.conan\alwyj0a_\1\core\include -IC:\.conan\i0d9rf7t\1\static_assert\include -IC:\.conan\veok39ai\1\assert\include -IC:\.conan\12u7nsry\1\config\include -IC:\Users\STENBRO\.conan\data\FMILibrary\2.0.3\kyllingstad\testing\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\include -IC:\Users\STENBRO\.conan\data\libzip\1.5.1\bincrafters\stable\package\c97299ee8d7194d38a91af89230200a9897c2ac6\include -IC:\Users\STENBRO\.conan\data\libevent\2.0.22\bincrafters\stable\package\6cc50b139b9c3d27b3e9042d5f5372d327b3a9f7\include 

