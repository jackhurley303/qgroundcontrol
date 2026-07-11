# ============================================================================
# QGroundControl Plugin Helper Functions
# ============================================================================

#[=======================================================================[.rst:
qgc_add_plugin
--------------

Single entry point for building a QGroundControl plugin. Configures the
plugin as a MODULE library, wires up its manifest, applies the common QGC
plugin compile definitions/include directories/Qt linkage, sets the
platform-appropriate undefined-symbol link options, and auto-deploys the
built library to the host's runtime plugin search path for the dev loop.

Example usage:
  qgc_add_plugin(MyPlugin
      TIER INTERNAL
      MANIFEST qgcplugin.json.in
      SOURCES
          MyPlugin.h
          MyPlugin.cc
      QRC_FILES
          MyPlugin.qrc
  )

Plugin-specific extras (extra Qt modules, extra include dirs, test
subdirectories, ...) are added by the caller with normal CMake commands
after calling qgc_add_plugin().

#]=======================================================================]

function(qgc_add_plugin PLUGIN_NAME)
    set(options "")
    set(oneValueArgs TIER MANIFEST)
    set(multiValueArgs SOURCES QRC_FILES)
    cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT PLUGIN_TIER STREQUAL "INTERNAL")
        message(FATAL_ERROR
            "qgc_add_plugin(${PLUGIN_NAME}): TIER ${PLUGIN_TIER} is not supported yet. "
            "Only INTERNAL works today; SDK arrives in Stage 2, QML in Stage 3 "
            "(see plugins/.architecture/04-macos-implementation-plan.md).")
    endif()

    if(NOT PLUGIN_MANIFEST)
        message(FATAL_ERROR "qgc_add_plugin(${PLUGIN_NAME}): MANIFEST is required.")
    endif()

    # Create the plugin as a MODULE library
    add_library(${PLUGIN_NAME} MODULE ${PLUGIN_SOURCES} ${PLUGIN_QRC_FILES})

    # Manifest, embedded into the library via Q_PLUGIN_METADATA(... FILE "qgcplugin.json").
    # Configured into the binary dir so hostBuildId matches the host build; the binary dir
    # joins the include path below so moc can resolve the FILE reference.
    configure_file(${PLUGIN_MANIFEST} "${CMAKE_CURRENT_BINARY_DIR}/qgcplugin.json" @ONLY)
    target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    # Enable Qt features
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    )

    # Apply common compile definitions from main QGC build
    if(DEFINED QGC_PLUGIN_COMPILE_DEFINITIONS)
        target_compile_definitions(${PLUGIN_NAME}
            PRIVATE
                ${QGC_PLUGIN_COMPILE_DEFINITIONS}
        )
    endif()

    # Apply common include directories from main QGC build
    if(DEFINED QGC_PLUGIN_INCLUDE_DIRECTORIES)
        target_include_directories(${PLUGIN_NAME}
            PRIVATE
                ${QGC_PLUGIN_INCLUDE_DIRECTORIES}
        )
    endif()

    # Ensure mavlink-generated headers exist before this plugin compiles.
    # The mavlink CPM target generates headers at build time; without this dependency
    # a parallel build can compile the plugin before the headers are ready.
    if(TARGET mavlink)
        add_dependencies(${PLUGIN_NAME} mavlink)
    endif()

    # Link against required Qt libraries
    target_link_libraries(${PLUGIN_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Qml
            Qt6::Quick
            Qt6::Widgets
            Qt6::Network
    )

    # Platform-specific plugin extension
    if(WIN32)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dylib")
    else()
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".so")
    endif()

    # Allow undefined symbols - they'll be resolved from the main executable at runtime.
    # This is the internal tier's linkage model (D7); SDK-tier plugins (Stage 2) link
    # QGCPluginAPI directly instead and don't need this.
    if(APPLE)
        target_link_options(${PLUGIN_NAME} PRIVATE
            -undefined dynamic_lookup  # Resolve symbols from loading executable
        )
    elseif(UNIX)
        target_link_options(${PLUGIN_NAME} PRIVATE
            -Wl,--allow-shlib-undefined
        )
    elseif(WIN32)
        message(STATUS "${PLUGIN_NAME}: Windows plugin loading may require additional configuration")
    endif()

    # Auto-deploy plugin to runtime location after build (for development).
    # Mirrors QGCApplication::_setInstanceInfo()'s applicationName computation
    # (src/QGCApplication.cc) exactly, so the deploy dir always matches where
    # QGCPluginLoader::defaultPluginPaths() looks at runtime for any
    # QGC_APP_NAME / QGC_ORG_NAME / QGC_STABLE_BUILD combination.
    if(QGC_STABLE_BUILD)
        set(_qgc_plugin_app_name "${QGC_APP_NAME}")
    else()
        set(_qgc_plugin_app_name "${QGC_APP_NAME} Daily")
    endif()

    if(APPLE)
        set(PLUGIN_DEPLOY_DIR "$ENV{HOME}/Library/Application Support/${QGC_ORG_NAME}/${_qgc_plugin_app_name}/plugins")
    elseif(UNIX)
        set(PLUGIN_DEPLOY_DIR "$ENV{HOME}/.local/share/${QGC_ORG_NAME}/${_qgc_plugin_app_name}/plugins")
    elseif(WIN32)
        set(PLUGIN_DEPLOY_DIR "$ENV{APPDATA}/${QGC_ORG_NAME}/${_qgc_plugin_app_name}/plugins")
    endif()

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PLUGIN_NAME}>
            "${PLUGIN_DEPLOY_DIR}/"
        COMMENT "Deploying ${PLUGIN_NAME} to ${PLUGIN_DEPLOY_DIR}"
    )

    message(STATUS "  Configured plugin: ${PLUGIN_NAME} (tier ${PLUGIN_TIER})")
    message(STATUS "  ${PLUGIN_NAME}: Will auto-deploy to ${PLUGIN_DEPLOY_DIR}")
endfunction()
