add_subdirectory(forge-cli)
add_subdirectory(forge-viewer)

if(FORGE_BUILD_EDITOR)
	add_subdirectory(forge-editor)
endif()
