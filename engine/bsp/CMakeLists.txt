add_library(forge-bsp STATIC
    src/BSPTree.cpp
)

add_library(forge::bsp ALIAS forge-bsp)

target_include_directories(forge-bsp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-bsp
    PUBLIC
        forge::scene
        forge_compile_options
)

target_compile_features(forge-bsp PUBLIC cxx_std_23)
