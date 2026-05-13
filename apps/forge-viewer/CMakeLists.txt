add_executable(forge-viewer main.cpp)

target_link_libraries(forge-viewer
    PRIVATE
        forge::gfx
        forge::imgui
        forge_compile_options
)

target_compile_features(forge-viewer PRIVATE cxx_std_23)

install(TARGETS forge-viewer RUNTIME DESTINATION bin)
