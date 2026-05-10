add_library(forge-export STATIC
    src/OBJExporter.cpp
    src/MAPExporter.cpp
)

add_library(forge::export ALIAS forge-export)

target_include_directories(forge-export
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-export
    PUBLIC
        forge::scene
        forge_compile_options
)

target_compile_features(forge-export PUBLIC cxx_std_23)
