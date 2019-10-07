set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")
set(publicHeaders
)

set(sources
    "cse_xsd_embedder.cpp"
)

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${privateHeaders} ${sources})

add_executable(csexsdembedder ${allSources})
target_compile_features(csexsdembedder PUBLIC "cxx_std_17")
