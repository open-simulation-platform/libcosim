
add_executable("pdu-generator" "pdu_generator.cpp")

target_compile_definitions(pdu-generator PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
target_compile_features(pdu-generator PUBLIC "cxx_std_17")
target_link_libraries(pdu-generator
        PRIVATE
        Boost::boost
        Boost::filesystem
        gsl
        nlohmann_json
        )

#add_custom_command(
#        OUTPUT generated_pdus.cpp
#        COMMAND pdu-generator
##        DEPENDS generate_protobuf.py ${ProtoName}.proto
#
##        COMMENT "Generating code for ${ProtoName}."
#)
#
#add_custom_target(
#        generate-pdus
#        DEPENDS generated_pdus.cpp
#)
