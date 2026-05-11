cmake_minimum_required(VERSION 3.28)
project(Forge-Phase12-Demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -DGLM_ENABLE_EXPERIMENTAL")

# Core modules (geometry + material, no GUI deps)
add_library(forge-geo
    ../../engine/geometry/src/Plane.cpp
    ../../engine/geometry/src/Brush.cpp
    ../../engine/geometry/src/Primitives.cpp
    ../../engine/geometry/src/CSG.cpp
)
target_include_directories(forge-geo PUBLIC ../../engine/geometry/include)
target_compile_definitions(forge-geo PUBLIC GLM_ENABLE_EXPERIMENTAL)

add_library(forge-material
    ../../engine/gfx/src/Material.cpp
)
target_include_directories(forge-material 
    PUBLIC ../../engine/gfx/include
    PRIVATE ../../engine/geometry/include
)
target_link_libraries(forge-material PUBLIC forge-geo)
target_compile_definitions(forge-material PUBLIC GLM_ENABLE_EXPERIMENTAL)

# Demo executable
add_executable(phase12-demo main.cpp)
target_link_libraries(phase12-demo PRIVATE forge-material)
target_include_directories(phase12-demo PRIVATE ../../engine/geometry/include)
target_compile_definitions(phase12-demo PUBLIC GLM_ENABLE_EXPERIMENTAL)
