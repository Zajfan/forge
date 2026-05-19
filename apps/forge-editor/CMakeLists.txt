find_package(Lua REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
# portable-file-dialogs is header-only
find_path(PFD_INCLUDE_DIR NAMES portable-file-dialogs.h REQUIRED)

add_executable(forge-editor
    main.cpp
    EditorApp.cpp
    EntityRegistry.cpp
    PrefabSystem.cpp
    LevelValidator.cpp
    MaterialEditor.cpp
    UndoStack.cpp
    PropertyInspector.cpp
)

target_include_directories(forge-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LUA_INCLUDE_DIR} ${PFD_INCLUDE_DIR})

target_link_libraries(forge-editor
    PRIVATE
        forge::audio
        forge::bsp
        forge::gfx
        forge::export
        forge::runtime
        forge::script
        forge::serial
        forge::imgui
        imguizmo::imguizmo
        ${LUA_LIBRARIES}
        forge_compile_options
)

target_compile_features(forge-editor PRIVATE cxx_std_23)

install(TARGETS forge-editor RUNTIME DESTINATION bin)
