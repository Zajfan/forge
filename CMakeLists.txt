cmake_minimum_required(VERSION 3.28)

project(Forge
    VERSION     0.1.0
    DESCRIPTION "Brush geometry kernel and game engine"
    LANGUAGES   CXX
)

# ── C++23 ────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Output directories ────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ── Options ───────────────────────────────────────────────────────────────────
option(FORGE_BUILD_TESTS   "Build Catch2 test suite"           ON)
option(FORGE_BUILD_EDITOR  "Build the editor application"      OFF) # Phase 1+
option(FORGE_WERROR        "Treat warnings as errors"          OFF)
option(FORGE_USE_BUNDLED_IMGUI "Use vendored ImGui docking source" ON)

# ── Compiler warnings ─────────────────────────────────────────────────────────
add_library(forge_compile_options INTERFACE)

# GLM experimental extensions (gtx) used across all modules
target_compile_definitions(forge_compile_options INTERFACE GLM_ENABLE_EXPERIMENTAL)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forge_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wno-unused-parameter          # too noisy during early dev
        $<$<BOOL:${FORGE_WERROR}>:-Werror>
    )
elseif(MSVC)
    target_compile_options(forge_compile_options INTERFACE
        /W4
        $<$<BOOL:${FORGE_WERROR}>:/WX>
    )
endif()

# ── Dependencies (via vcpkg) ──────────────────────────────────────────────────
find_package(glm     CONFIG REQUIRED)

if(FORGE_USE_BUNDLED_IMGUI)
    find_package(SDL3   CONFIG REQUIRED)
    find_package(GLEW   REQUIRED)
    find_package(OpenGL REQUIRED)

    set(FORGE_IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
    if(NOT EXISTS "${FORGE_IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR "FORGE_USE_BUNDLED_IMGUI is ON but third_party/imgui is missing. Clone the docking branch into third_party/imgui.")
    endif()

    add_library(forge-imgui STATIC
        ${FORGE_IMGUI_DIR}/imgui.cpp
        ${FORGE_IMGUI_DIR}/imgui_demo.cpp
        ${FORGE_IMGUI_DIR}/imgui_draw.cpp
        ${FORGE_IMGUI_DIR}/imgui_tables.cpp
        ${FORGE_IMGUI_DIR}/imgui_widgets.cpp
        ${FORGE_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${FORGE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_library(forge::imgui ALIAS forge-imgui)

    target_include_directories(forge-imgui
        PUBLIC
            ${FORGE_IMGUI_DIR}
            ${FORGE_IMGUI_DIR}/backends
    )
    target_compile_definitions(forge-imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW)
    target_link_libraries(forge-imgui
        PUBLIC forge_compile_options
        PRIVATE SDL3::SDL3 GLEW::GLEW OpenGL::GL
    )
    target_compile_features(forge-imgui PUBLIC cxx_std_23)
else()
    find_package(imgui CONFIG REQUIRED)
    add_library(forge-imgui INTERFACE)
    add_library(forge::imgui ALIAS forge-imgui)
    target_link_libraries(forge-imgui INTERFACE imgui::imgui forge_compile_options)
endif()

if(FORGE_BUILD_TESTS)
    find_package(Catch2  3  CONFIG REQUIRED)
endif()

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(engine/geometry)
add_subdirectory(engine/scene)
add_subdirectory(engine/serial)
add_subdirectory(engine/bsp)
add_subdirectory(engine/audio)
add_subdirectory(engine/script)
add_subdirectory(engine/export)
add_subdirectory(engine/build)
add_subdirectory(engine/runtime)
add_subdirectory(engine/gfx)
add_subdirectory(apps)

if(FORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════╗")
message(STATUS "║  FORGE v${PROJECT_VERSION}               ║")
message(STATUS "╠══════════════════════════════╣")
message(STATUS "║  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "║  C++ std    : ${CMAKE_CXX_STANDARD}")
message(STATUS "║  Tests      : ${FORGE_BUILD_TESTS}")
message(STATUS "║  Editor     : ${FORGE_BUILD_EDITOR}")
message(STATUS "╚══════════════════════════════╝")
message(STATUS "")
