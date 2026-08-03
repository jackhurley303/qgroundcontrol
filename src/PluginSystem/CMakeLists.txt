# ============================================================================
# Plugin System Module
# Host-side runtime plugin loading and management, built on the QGCPluginAPI
# SDK boundary (src/PluginAPI/)
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        HostServices/QGCAppServiceImpl.cc
        HostServices/QGCAppServiceImpl.h
        HostServices/QGCHostServicesImpl.cc
        HostServices/QGCHostServicesImpl.h
        HostServices/QGCMissionServiceImpl.cc
        HostServices/QGCMissionServiceImpl.h
        HostServices/QGCParameterServiceImpl.cc
        HostServices/QGCParameterServiceImpl.h
        HostServices/QGCReplayServiceImpl.cc
        HostServices/QGCReplayServiceImpl.h
        HostServices/QGCTelemetryLoggingServiceImpl.cc
        HostServices/QGCTelemetryLoggingServiceImpl.h
        HostServices/QGCVehicleServiceImpl.cc
        HostServices/QGCVehicleServiceImpl.h
        PluginContributions.cc
        PluginContributions.h
        PluginInstaller.cc
        PluginInstaller.h
        PluginJsonUtils.h
        PluginManifest.cc
        PluginManifest.h
        PluginRecordStore.cc
        PluginRecordStore.h
        QGCPluginLoader.cc
        QGCPluginLoader.h
        QGCPluginManager.cc
        QGCPluginManager.h
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCPluginAPI)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
