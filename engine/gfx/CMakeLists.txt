find_package(SDL3   CONFIG REQUIRED)
find_package(GLEW   REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Stb       REQUIRED)
find_package(tinygltf  CONFIG REQUIRED)

add_library(forge-gfx STATIC
    src/Camera.cpp
    src/Shader.cpp
    src/GPUMesh.cpp
    src/Window.cpp
    src/Renderer.cpp
    src/Framebuffer.cpp
    src/GridRenderer.cpp
    src/TextureCache.cpp
    src/SkyboxRenderer.cpp
    src/OrthoCamera.cpp
    src/stb_impl.cpp
    src/ShadowMap.cpp
    src/MeshAssetCache.cpp
    src/BloomRenderer.cpp
)

add_library(forge::gfx ALIAS forge-gfx)

target_include_directories(forge-gfx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src   # for Shaders.hpp
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(forge-gfx
    PUBLIC
        forge::build
        forge_compile_options
    PRIVATE
        SDL3::SDL3
        GLEW::GLEW
        OpenGL::GL
        
)

target_compile_features(forge-gfx PUBLIC cxx_std_23)
