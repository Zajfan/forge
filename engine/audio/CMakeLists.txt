# miniaudio is header-only — no CMake config package, find the header directly
find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)

add_library(forge-audio STATIC
    src/miniaudio_impl.cpp
    src/AudioEngine.cpp
)

add_library(forge::audio ALIAS forge-audio)

target_include_directories(forge-audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MINIAUDIO_INCLUDE_DIR}
)

target_link_libraries(forge-audio
    PUBLIC
        forge_compile_options
)

target_compile_features(forge-audio PUBLIC cxx_std_23)

# miniaudio needs pthreads on Linux
if(UNIX)
    target_link_libraries(forge-audio PRIVATE pthread dl m)
endif()
