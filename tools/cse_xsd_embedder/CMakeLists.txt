set(sources
    "cse_xsd_embedder.cpp"
)

add_executable(csexsdembedder ${sources})
target_compile_features(csexsdembedder PUBLIC "cxx_std_17")
