add_library(forge-scene STATIC
    src/Transform.cpp
    src/Scene.cpp
)

add_library(forge::scene ALIAS forge-scene)

target_include_directories(forge-scene
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-scene
    PUBLIC
        forge::geometry
        forge_compile_options
)

target_compile_features(forge-scene PUBLIC cxx_std_23)
