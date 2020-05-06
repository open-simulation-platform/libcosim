set(sources
    "osp-xsd-embedder.cpp"
)

add_executable(osp-xsd-embedder ${sources})
target_compile_features(osp-xsd-embedder PUBLIC "cxx_std_17")
