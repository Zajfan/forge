find_package(Lua REQUIRED)
find_package(imgui    CONFIG REQUIRED)
find_package(portable_file_dialogs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

add_executable(forge-editor
    main.cpp
    EditorApp.cpp
    EntityRegistry.cpp
    PrefabSystem.cpp
    LevelValidator.cpp
)

target_include_directories(forge-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LUA_INCLUDE_DIR})

target_link_libraries(forge-editor
    PRIVATE
        forge::gfx
        forge::export
        forge::runtime
        imgui::imgui
        imguizmo::imguizmo
        portable_file_dialogs::portable_file_dialogs
        ${LUA_LIBRARIES}
        forge_compile_options
)

target_compile_features(forge-editor PRIVATE cxx_std_23)

install(TARGETS forge-editor RUNTIME DESTINATION bin)
