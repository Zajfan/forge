include(Catch)

# ── Mesh builder tests (no GL dep) ───────────────────────────────────────────
add_executable(test-build
    build/test_meshbuilder.cpp
)

target_link_libraries(test-build
    PRIVATE
        forge::build
        Catch2::Catch2WithMain
)

catch_discover_tests(test-build
    TEST_PREFIX  "build."
    REPORTER     console
)

# ── Geometry kernel tests ─────────────────────────────────────────────────────
add_executable(test-geometry
    geo/test_plane.cpp
    geo/test_brush.cpp
    geo/test_csg.cpp
)

target_link_libraries(test-geometry
    PRIVATE
        forge::geometry
        Catch2::Catch2WithMain
)

catch_discover_tests(test-geometry
    TEST_PREFIX  "geo."
    REPORTER     console
)

# ── Scene graph tests ─────────────────────────────────────────────────────────
add_executable(test-scene
    scene/test_scene.cpp
)

target_link_libraries(test-scene
    PRIVATE
        forge::scene
        Catch2::Catch2WithMain
)

catch_discover_tests(test-scene
    TEST_PREFIX  "scene."
    REPORTER     console
)

# ── Export tests ──────────────────────────────────────────────────────────────
add_executable(test-export
    export/test_export.cpp
)

target_link_libraries(test-export
    PRIVATE
        forge::export
        Catch2::Catch2WithMain
)

catch_discover_tests(test-export
    TEST_PREFIX  "export."
    REPORTER     console
)

# ── Graphics (material) tests ─────────────────────────────────────────────────
add_executable(test-gfx
    gfx/test_material.cpp
)

target_link_libraries(test-gfx
    PRIVATE
        forge::gfx
        Catch2::Catch2WithMain
)

catch_discover_tests(test-gfx
    TEST_PREFIX  "gfx."
    REPORTER     console
)
