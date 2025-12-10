# ============================================================================
# Plugin System Module
# Runtime plugin loading, management, and interface
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCPlugin.cc
        QGCPlugin.h
        QGCPluginInterface.h
        QGCPluginLoader.cc
        QGCPluginLoader.h
        QGCPluginManager.cc
        QGCPluginManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
