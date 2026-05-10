find_package(nlohmann_json CONFIG REQUIRED)

add_library(forge-serial STATIC
    src/SceneSerializer.cpp
)

add_library(forge::serial ALIAS forge-serial)

target_include_directories(forge-serial
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-serial
    PUBLIC
        forge::scene
        forge_compile_options
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_features(forge-serial PUBLIC cxx_std_23)
