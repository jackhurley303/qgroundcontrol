# ============================================================================
# QGroundControl Plugin Helper Functions
# ============================================================================

#[=======================================================================[.rst:
qgc_add_plugin
--------------

Single entry point for building a QGroundControl plugin. Configures the
plugin as a MODULE library, wires up its manifest, and auto-deploys the built
library to the host's runtime plugin search path for the dev loop. TIER SDK
links only the published QGCPluginAPI + Qt (D2's include boundary) — the only
supported linkage.

Example usage:
  qgc_add_plugin(MyPlugin
      TIER SDK
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

Pass NO_DEPLOY to skip the runtime auto-deploy step — for build-only fixtures
(e.g. test dylibs) that must never land in the real runtime plugin directory.

#]=======================================================================]

function(qgc_add_plugin PLUGIN_NAME)
    set(options NO_DEPLOY)
    set(oneValueArgs TIER MANIFEST)
    set(multiValueArgs SOURCES QRC_FILES)
    cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT PLUGIN_TIER STREQUAL "SDK")
        message(FATAL_ERROR
            "qgc_add_plugin(${PLUGIN_NAME}): TIER ${PLUGIN_TIER} is not supported yet. "
            "SDK works today; QML is not implemented yet.")
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

    # Link only the published SDK + stock Qt. D2's include-boundary enforcement
    # is the build itself — with no src/ path on the include list, a plugin
    # reaching for a QGC internal fails to compile, not just to link.
    target_link_libraries(${PLUGIN_NAME} PRIVATE QGCPluginAPI)

    # Platform-specific plugin extension
    if(WIN32)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".dylib")
    else()
        set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".so")
    endif()

    if(PLUGIN_NO_DEPLOY)
        message(STATUS "  Configured plugin: ${PLUGIN_NAME} (tier ${PLUGIN_TIER}, no auto-deploy)")
        return()
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
