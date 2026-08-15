cmake_minimum_required(VERSION 3.25)

project(ExamplePlugin VERSION 1.0.0 LANGUAGES CXX)

# This plugin builds as part of the QGC build system (dev loop), but is a pure
# Tier B plugin: it links only the published QGCPluginAPI SDK + Qt, never a QGC
# header outside src/PluginAPI/ or a QGC object file. See plugins/README.md.
#
# The bundled reference plugin, dual-mode (out-of-tree-verifier.md U1) — swept up
# by plugins/CMakeLists.txt's glob (PROJECT_IS_TOP_LEVEL false) uses the in-tree
# qgc_add_plugin() dev-loop helper below; built standalone against an installed
# QGCPluginSDK package (PROJECT_IS_TOP_LEVEL true, no QGC checkout present) uses
# find_package(QGCPluginAPI) instead, mirroring how any out-of-tree plugin builds. The
# invariant this branch exists to hold: the in-tree arm must be a strict substitute
# for find_package(QGCPluginAPI) — same target, same headers, nothing the standalone
# arm doesn't also get.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

    # Major version pin: rejects an SDK whose major doesn't match what this plugin
    # was written against (D3's SOVERSION rule, surfaced through find_package()).
    find_package(QGCPluginAPI 2 REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Core)

    # The build marker the host reads back from the mapped image to report which
    # build of this plugin is actually running. qgc_plugin_build_marker() comes
    # from the SDK package itself (find_package(QGCPluginAPI) above includes it),
    # so this arm calls the same generator qgc_add_plugin() calls in-tree instead
    # of carrying a second copy of it.
    qgc_plugin_build_marker(ExamplePlugin
        OUT_SOURCE EXAMPLE_PLUGIN_BUILD_MARKER
        MANIFEST   qgcplugin.json.in
        SOURCES
            ExamplePlugin.h
            ExamplePlugin.cc
        QRC_FILES
            ExamplePlugin.qrc
    )

    add_library(ExamplePlugin MODULE
        ExamplePlugin.h
        ExamplePlugin.cc
        ExamplePlugin.qrc
        ${EXAMPLE_PLUGIN_BUILD_MARKER}
    )

    # QGCPluginAPI_VERSION_MAJOR comes from find_package() above
    # (QGCPluginAPIConfigVersion.cmake) — reading it here instead of hardcoding keeps
    # the manifest's declared apiVersion tied to the SDK actually being linked, not a
    # value that can silently drift from it.
    configure_file(qgcplugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/qgcplugin.json" @ONLY)
    target_include_directories(ExamplePlugin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    target_link_libraries(ExamplePlugin PRIVATE QGCPluginAPI::QGCPluginAPI)

    # No -undefined dynamic_lookup / -Wl,--allow-shlib-undefined here: every symbol
    # this plugin needs resolves from the linked QGCPluginAPI dylib (D2/D3), the
    # SDK-tier shape qgc_add_plugin() uses in-tree.
    if(APPLE)
        set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dylib")
    elseif(WIN32)
        set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dll")
    else()
        set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".so")
    endif()

    # Standalone POST_BUILD deploy: mirrors qgc_add_plugin()'s dev-loop auto-deploy
    # (cmake/modules/PluginHelpers.cmake) so a self-installed SDK author gets
    # the same "build it, relaunch QGC" loop the in-tree arm gets. QGCPluginAPI_APP_NAME/
    # _ORG_NAME/_STABLE_BUILD come from QGCPluginAPIConfig.cmake.in — the SDK's own
    # export of the host's identity, not a second hardcoded copy of it.
    if(QGCPluginAPI_STABLE_BUILD)
        set(_example_plugin_app_name "${QGCPluginAPI_APP_NAME}")
    else()
        set(_example_plugin_app_name "${QGCPluginAPI_APP_NAME} Daily")
    endif()

    if(APPLE)
        set(EXAMPLE_PLUGIN_DEPLOY_DIR
            "$ENV{HOME}/Library/Application Support/${QGCPluginAPI_ORG_NAME}/${_example_plugin_app_name}/plugins")
    elseif(UNIX)
        set(EXAMPLE_PLUGIN_DEPLOY_DIR
            "$ENV{HOME}/.local/share/${QGCPluginAPI_ORG_NAME}/${_example_plugin_app_name}/plugins")
    elseif(WIN32)
        set(EXAMPLE_PLUGIN_DEPLOY_DIR
            "$ENV{APPDATA}/${QGCPluginAPI_ORG_NAME}/${_example_plugin_app_name}/plugins")
    endif()

    add_custom_command(TARGET ExamplePlugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EXAMPLE_PLUGIN_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ExamplePlugin>
            "${EXAMPLE_PLUGIN_DEPLOY_DIR}/"
        COMMENT "Deploying ExamplePlugin to ${EXAMPLE_PLUGIN_DEPLOY_DIR}"
    )
else()
    # In-tree arm (dev loop): QGC's plugin helper function configures the plugin,
    # including its own POST_BUILD auto-deploy.
    qgc_add_plugin(ExamplePlugin
        TIER SDK
        MANIFEST qgcplugin.json.in
        SOURCES
            ExamplePlugin.h
            ExamplePlugin.cc
        QRC_FILES
            ExamplePlugin.qrc
    )
endif()
