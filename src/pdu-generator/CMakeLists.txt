add_executable(pdu-generator pdu_generator.cpp)

target_compile_definitions(pdu-generator PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
target_compile_features(pdu-generator PUBLIC "cxx_std_17")
target_link_libraries(pdu-generator
    PUBLIC
        Boost::boost
    PRIVATE
        nlohmann_json
        )

add_custom_target(
        generate-pdu-code
        COMMAND pdu-generator
        DEPENDS pdu-generator pdus.json
        COMMENT "Generating PDU code -- custom target"
)

add_dependencies(csecorecpp generate-pdu-code)
