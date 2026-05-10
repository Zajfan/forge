add_library(forge-geometry STATIC
    src/Plane.cpp
    src/Brush.cpp
    src/CSG.cpp
    src/Primitives.cpp
)

add_library(forge::geometry ALIAS forge-geometry)

# ── Include paths ─────────────────────────────────────────────────────────────
# PUBLIC: consumers of forge-geometry also get these paths
target_include_directories(forge-geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ── Dependencies ──────────────────────────────────────────────────────────────
target_link_libraries(forge-geometry
    PUBLIC
        glm::glm
        forge_compile_options
)

# ── Compiler features ─────────────────────────────────────────────────────────
target_compile_features(forge-geometry PUBLIC cxx_std_23)

# ── GLM configuration ─────────────────────────────────────────────────────────
target_compile_definitions(forge-geometry
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL       # GLM_GTX extensions
        GLM_FORCE_DEPTH_ZERO_TO_ONE   # Vulkan-compatible projection (future)
        GLM_FORCE_RADIANS
)
