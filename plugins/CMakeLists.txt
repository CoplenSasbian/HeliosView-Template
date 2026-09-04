


function(add_plugins name dir)
    message(STATUS "Adding plugin: ${name} from ${dir}")
    file(GLOB_RECURSE plugin_files "${dir}/*.cpp" "${dir}/*.h")
    add_library(Plugin_${name} SHARED ${plugin_files})
    target_link_libraries(Plugin_${name} PluginInterface)

    set_target_properties(Plugin_${name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_PLUGINS_OUTPUT_DIRECTORY}"   # Linux .so / macOS .dylib
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_PLUGINS_OUTPUT_DIRECTORY}"   # Windows .dll
    )

    # Build the plugin whenever the app target is built: plugins are loaded at
    # runtime (not linked), so without this dependency ninja would not rebuild
    # a deleted plugin DLL when only the app target is requested.
    # (The app target name is configurable via app-config.cmake.)
    if(TARGET ${HELIOSVIEW_TEMPLATE_APP_NAME})
        add_dependencies(${HELIOSVIEW_TEMPLATE_APP_NAME} Plugin_${name})
    endif()

    # Stage the plugin into the distributable (dist\plugins) via
    # `cmake --install` — AppFilePath::PluginDir resolves "plugins" relative
    # to the app root, pointing at dist\plugins.
    install(TARGETS Plugin_${name} RUNTIME DESTINATION plugins)
endfunction()


add_plugins(sticky_keys ${CMAKE_CURRENT_SOURCE_DIR}/sticky_keys_plugin)
add_plugins(health ${CMAKE_CURRENT_SOURCE_DIR}/health_plugin)
add_plugins(hdr ${CMAKE_CURRENT_SOURCE_DIR}/hdr_plugin)
add_plugins(power ${CMAKE_CURRENT_SOURCE_DIR}/power_plugin)
add_plugins(nv_dvc ${CMAKE_CURRENT_SOURCE_DIR}/nv_dvc_plugin)
add_plugins(runscript ${CMAKE_CURRENT_SOURCE_DIR}/runscript_plugin)