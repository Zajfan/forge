find_package(Jolt CONFIG REQUIRED)

add_library(forge-runtime STATIC
    src/PhysicsWorld.cpp
    src/FPSCamera.cpp
    src/PlayerController.cpp
    src/GameRuntime.cpp
)

add_library(forge::runtime ALIAS forge-runtime)

target_include_directories(forge-runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(forge-runtime
    PUBLIC
        forge::scene
        forge::gfx         # for InputState
        forge_compile_options
    PRIVATE
        Jolt::Jolt
        forge::script      # GameRuntime.cpp uses ScriptEnv (one-way: script has no link dep on runtime)
)

target_compile_features(forge-runtime PUBLIC cxx_std_23)

# Jolt requires NDEBUG or JPH_ENABLE_ASSERTS to be consistent
target_compile_definitions(forge-runtime
    PRIVATE
        $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)
