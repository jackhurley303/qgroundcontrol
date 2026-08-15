# ============================================================================
# QGCPluginAPI — the plugin SDK boundary
# Interfaces only: links Qt only, includes nothing from src/ outside this
# directory. Dependency arrows: app -> QGCPluginAPI <- plugins.
# Ships as a host-provided @rpath dylib; plugins link it, never bundle it.
# ============================================================================

# Public headers, listed once and reused for the SDK's packaged install (U2.7,
# cmake/install/InstallPluginSDK.cmake, included from the root CMakeLists.txt — hence
# CACHE INTERNAL, needed because that install script runs past this directory's
# add_subdirectory() scope) so the shipped include/QGCPluginAPI/ set can never
# drift from what this target actually builds against.
set(QGC_PLUGIN_API_PUBLIC_HEADERS
    QGCAppService.h
    QGCHostServices.h
    QGCMissionService.h
    QGCParameterDiffService.h
    QGCParameterService.h
    QGCPlugin.h
    QGCPluginInterface.h
    QGCReplayExtension.h
    QGCReplayService.h
    QGCTelemetryLoggingService.h
    QGCVehicleService.h
    qgc_plugin_api_global.h
    CACHE INTERNAL "Public headers of the QGCPluginAPI SDK target"
)

qt_add_library(QGCPluginAPI SHARED
    QGCAppService.cc
    QGCHostServices.cc
    QGCMissionService.cc
    QGCParameterDiffService.cc
    QGCParameterService.cc
    QGCPlugin.cc
    QGCReplayExtension.cc
    QGCReplayService.cc
    QGCTelemetryLoggingService.cc
    QGCVehicleService.cc
    ${QGC_PLUGIN_API_PUBLIC_HEADERS}
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

# Namespaced alias so a plugin that must build both in-tree and standalone against
# find_package(QGCPluginAPI) (out-of-tree.md QO1) can link QGCPluginAPI::QGCPluginAPI
# unconditionally — find_package()'s own exported target already carries this name.
add_library(QGCPluginAPI::QGCPluginAPI ALIAS QGCPluginAPI)

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

# BUILD_INTERFACE/INSTALL_INTERFACE split: an exported target (U2.7's install(EXPORT ...))
# may not reference build-tree paths in its installed INTERFACE_INCLUDE_DIRECTORIES, so the
# in-tree shim dirs above are build-only and the packaged SDK gets its own installed
# include/ tree instead (see InstallPluginSDK.cmake) — same <QGCPluginAPI/...> spelling
# either way.
#
# The mavlink BUILD_INTERFACE/INSTALL_INTERFACE entries (out-of-tree-plugins.md U1)
# mirror QGC_MAVLINK_INCLUDE_DIRS' own shape (src/MAVLink/CMakeLists.txt) exactly —
# both its root dir (mavlink_types.h) and its dialect subdir (mavlink.h itself only
# exists under mavlink/<dialect>/) — install-rooted for a packaged plugin and
# build-rooted for an in-tree one, so a plugin's bare `#include <mavlink.h>` /
# `<mavlink_types.h>` resolves through this target in both modes with no
# QGC_MAVLINK_INCLUDE_DIRS reach required (an out-of-tree plugin migration surfaced
# this gap; see QO1).
target_include_directories(QGCPluginAPI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${QGC_PLUGIN_API_INCLUDE_SHIM_DIR}>
    "$<BUILD_INTERFACE:$<JOIN:${QGC_MAVLINK_INCLUDE_DIRS},$<SEMICOLON>>>"
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/QGCPluginAPI/mavlink>
    $<INSTALL_INTERFACE:include/QGCPluginAPI/mavlink/${QGC_MAVLINK_DIALECT}>
)

# In-tree only: the mavlink headers above are generated by src/MAVLink's build step,
# so anything compiling against them in-tree must wait for it. Out-of-tree, the
# packaged SDK's headers are already on disk and the `mavlink` target doesn't exist —
# this is pure build-order glue on this SDK target, not a plugin-visible reach.
if(TARGET mavlink)
    add_dependencies(QGCPluginAPI mavlink)
endif()

target_link_libraries(QGCPluginAPI
    PUBLIC
        Qt6::Core
)
