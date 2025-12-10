# ============================================================================
# API Module
# Core plugin architecture and QML component interfaces
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCCorePlugin.cc
        QGCCorePlugin.h
        QGCPluginInterface.h
        QGCPlugin.cc
        QGCPlugin.h
        QGCPluginLoader.cc
        QGCPluginLoader.h
        QGCPluginManager.cc
        QGCPluginManager.h
        QGCOptions.cc
        QGCOptions.h
        QmlComponentInfo.cc
        QmlComponentInfo.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
