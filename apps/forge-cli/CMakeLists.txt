add_executable(forge-cli main.cpp)

target_link_libraries(forge-cli
    PRIVATE
        forge::export
        forge_compile_options
)

target_compile_features(forge-cli PRIVATE cxx_std_23)

# Install to bin/ alongside the libraries
install(TARGETS forge-cli RUNTIME DESTINATION bin)
