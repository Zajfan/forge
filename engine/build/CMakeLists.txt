add_library(forge-build STATIC
    src/MeshBuilder.cpp
)

add_library(forge::build ALIAS forge-build)

target_include_directories(forge-build
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-build
    PUBLIC
        forge::scene
        forge_compile_options
)

target_compile_features(forge-build PUBLIC cxx_std_23)
