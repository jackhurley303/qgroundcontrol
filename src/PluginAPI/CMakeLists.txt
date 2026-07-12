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

target_include_directories(QGCPluginAPI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(QGCPluginAPI
    PUBLIC
        Qt6::Core
)
