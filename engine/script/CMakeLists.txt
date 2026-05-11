find_package(Lua    REQUIRED)
find_package(sol2   CONFIG REQUIRED)

add_library(forge-script STATIC
    src/ScriptEnv.cpp
)

add_library(forge::script ALIAS forge-script)

target_include_directories(forge-script
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(forge-script
    PUBLIC
        forge::scene
        forge_compile_options
    PRIVATE
        ${LUA_LIBRARIES}
        sol2::sol2
)

target_compile_features(forge-script PUBLIC cxx_std_23)
