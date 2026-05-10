include(Catch)

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

# Auto-register Catch2 test cases with CTest
catch_discover_tests(test-geometry
    TEST_PREFIX  "geo."
    REPORTER     console
)
