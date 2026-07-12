# ============================================================================
# QGCPluginAPI — the plugin SDK boundary
# Interfaces only: links Qt only, includes nothing from src/ outside this
# directory. Dependency arrows: app -> QGCPluginAPI <- plugins.
# Ships as a host-provided @rpath dylib; plugins link it, never bundle it.
# ============================================================================

qt_add_library(QGCPluginAPI SHARED
    QGCAppService.cc
    QGCAppService.h
    QGCHostServices.cc
    QGCHostServices.h
    QGCMissionService.cc
    QGCMissionService.h
    QGCPlugin.cc
    QGCPlugin.h
    QGCPluginInterface.h
    QGCReplayExtension.cc
    QGCReplayExtension.h
    QGCReplayService.cc
    QGCReplayService.h
    QGCTelemetryLoggingService.cc
    QGCTelemetryLoggingService.h
    QGCVehicleService.cc
    QGCVehicleService.h
    qgc_plugin_api_global.h
)

set_target_properties(QGCPluginAPI PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    # Hidden visibility + explicit QGCPLUGINAPI_EXPORT keeps the export surface
    # to exactly the API (and makes the eventual Windows declspec build a no-op).
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # SOVERSION == the API major (QGC_PLUGIN_API_VERSION_MAJOR in
    # QGCPluginInterface.h); bump both together. A frozen plugin binds to
    # libQGCPluginAPI.<SOVERSION>.dylib, so they must never diverge.
    VERSION 2.0
    SOVERSION 2
)

target_compile_definitions(QGCPluginAPI PRIVATE QGCPLUGINAPI_LIBRARY)

# Canonical include spelling for every consumer, in-tree or out-of-tree:
# <QGCPluginAPI/QGCPlugin.h>. U2.7 installs real headers at include/QGCPluginAPI/
# in the packaged SDK zip; this shim directory (a symlink, falling back to a copy
# where symlinks aren't available) gives in-tree builds — this build and any
# plugin linking this target — the identical spelling today, so plugin source
# never differs between the dev loop and an out-of-tree SDK build.
set(QGC_PLUGIN_API_INCLUDE_SHIM_DIR "${CMAKE_BINARY_DIR}/include")
set(_qgc_plugin_api_shim "${QGC_PLUGIN_API_INCLUDE_SHIM_DIR}/QGCPluginAPI")
if(NOT EXISTS "${_qgc_plugin_api_shim}")
    file(MAKE_DIRECTORY "${QGC_PLUGIN_API_INCLUDE_SHIM_DIR}")
    # COPY_ON_ERROR's fallback does NOT work for a directory <original>: per
    # CMake's own docs it creates the destination directory but copies no
    # files, which would silently leave <QGCPluginAPI/...> resolving to
    # nothing wherever a symlink can't be created (e.g. Windows without
    # SeCreateSymbolicLinkPrivilege) — a hard compile break with no configure-
    # time diagnostic. Detect that case via RESULT and do a real recursive
    # copy ourselves instead of trusting COPY_ON_ERROR for a directory.
    file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}" "${_qgc_plugin_api_shim}"
        SYMBOLIC RESULT _qgc_plugin_api_shim_result
    )
    if(NOT _qgc_plugin_api_shim_result STREQUAL "0")
        file(REMOVE_RECURSE "${_qgc_plugin_api_shim}")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/" DESTINATION "${_qgc_plugin_api_shim}")
    endif()
endif()

target_include_directories(QGCPluginAPI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QGC_PLUGIN_API_INCLUDE_SHIM_DIR}
)

target_link_libraries(QGCPluginAPI
    PUBLIC
        Qt6::Core
)
