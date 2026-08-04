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
        PluginTrustGate.cc
        PluginTrustGate.h
        PluginUIGlobal.cc
        PluginUIGlobal.h
        PluginUIModule.cc
        PluginUIModule.h
        QGCPluginLoader.cc
        QGCPluginLoader.h
        QGCPluginManager.cc
        QGCPluginManager.h
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCPluginAPI)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# QGroundControl.PluginUI — the published QML vocabulary
#
# PluginUI/qmldir is the roster and the only hand-maintained list. Shipped
# verbatim as a resource, it is what the running host resolves against; reduced
# by tools/derive_plugin_ui_sdk.py, it is what an out-of-tree plugin lints
# against. Two forms, one source.
# ============================================================================

set(QGC_PLUGIN_UI_ROSTER "${CMAKE_CURRENT_SOURCE_DIR}/PluginUI/qmldir")

# The engine finds this because QGCCorePlugin::createQmlApplicationEngine() puts
# qrc:/qml on the import path, and every QML module here uses RESOURCE_PREFIX /qml
# with its URI as the subdirectory — so the roster's ../Controls/… entries land on
# the same URL QGroundControl.Controls registered, which is what makes the host's
# type and the plugin's type one type.
qt_add_resources(${CMAKE_PROJECT_NAME} qgc_plugin_ui_module
    PREFIX "/qml/QGroundControl/PluginUI"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/PluginUI"
    FILES "${QGC_PLUGIN_UI_ROSTER}"
)

# A qmldir cannot declare C++ types, so these are re-registered under the second
# URI by PluginUIModule.cc and described to out-of-tree tooling by the derived
# .qmltypes. Keep the two in step: PluginUIModuleTest fails if a name listed here
# does not resolve under the module's URI at runtime.
set(QGC_PLUGIN_UI_CPP_TYPES
    QGCPalette
    QGCFileDialogController
    PlanMasterController
    QGCPluginUIGlobal
)

set(_qgc_plugin_ui_sdk_dir "${CMAKE_BINARY_DIR}/plugin-sdk/qml/QGroundControl/PluginUI")

set(_qgc_plugin_ui_cpp_args "")
foreach(qgc_plugin_ui_type IN LISTS QGC_PLUGIN_UI_CPP_TYPES)
    list(APPEND _qgc_plugin_ui_cpp_args --cpp-type "${qgc_plugin_ui_type}")
endforeach()

# Depends on the app's qmltyperegistration target rather than on the .qmltypes
# path: a custom-command output can only be named as a dependency from the
# directory that produced it, and that one is produced at the root.
add_custom_command(
    OUTPUT
        "${_qgc_plugin_ui_sdk_dir}/qmldir"
        "${_qgc_plugin_ui_sdk_dir}/QGroundControl.PluginUI.qmltypes"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/derive_plugin_ui_sdk.py"
        --qmldir "${QGC_PLUGIN_UI_ROSTER}"
        --out-qmldir "${_qgc_plugin_ui_sdk_dir}/qmldir"
        --qmltypes "${CMAKE_BINARY_DIR}/qml/QGC/QGroundControl.qmltypes"
        --out-qmltypes "${_qgc_plugin_ui_sdk_dir}/QGroundControl.PluginUI.qmltypes"
        ${_qgc_plugin_ui_cpp_args}
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/derive_plugin_ui_sdk.py"
        "${QGC_PLUGIN_UI_ROSTER}"
        ${CMAKE_PROJECT_NAME}_qmltyperegistration
    COMMENT "Deriving the packaged QGroundControl.PluginUI module"
    VERBATIM
)

add_custom_target(QGCPluginUISDKModule ALL
    DEPENDS
        "${_qgc_plugin_ui_sdk_dir}/qmldir"
        "${_qgc_plugin_ui_sdk_dir}/QGroundControl.PluginUI.qmltypes"
    COMMENT "Packaged QGroundControl.PluginUI module is up to date"
)

# The control bodies the package ships beside that qmldir, read out of the roster
# so the file list can never disagree with it. Resolved against the build tree's
# qml/ dir, where qt_add_qml_module materialises every module at the same relative
# layout the resource tree uses — the roster's paths therefore work unchanged.
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/derive_plugin_ui_sdk.py"
        --qmldir "${QGC_PLUGIN_UI_ROSTER}" --print-sources
    OUTPUT_VARIABLE _qgc_plugin_ui_sources
    ERROR_VARIABLE _qgc_plugin_ui_error
    RESULT_VARIABLE _qgc_plugin_ui_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _qgc_plugin_ui_result EQUAL 0)
    message(FATAL_ERROR "QGC: cannot read the PluginUI roster: ${_qgc_plugin_ui_error}")
endif()

string(REPLACE "\n" ";" _qgc_plugin_ui_sources "${_qgc_plugin_ui_sources}")
set(_qgc_plugin_ui_bodies "")
foreach(qgc_plugin_ui_path IN LISTS _qgc_plugin_ui_sources)
    cmake_path(SET _qgc_plugin_ui_body NORMALIZE
        "${CMAKE_BINARY_DIR}/qml/QGroundControl/PluginUI/${qgc_plugin_ui_path}"
    )
    list(APPEND _qgc_plugin_ui_bodies "${_qgc_plugin_ui_body}")
endforeach()

# Carried on the target rather than in the cache: cmake/install/InstallPluginSDK.cmake
# runs at root scope after this directory, and these are packaging inputs, not knobs.
set_target_properties(QGCPluginUISDKModule PROPERTIES
    QGC_PLUGIN_UI_SDK_DIR "${_qgc_plugin_ui_sdk_dir}"
    QGC_PLUGIN_UI_QML_BODIES "${_qgc_plugin_ui_bodies}"
)

# Editing the roster changes both the resource and the packaged file list, and the
# latter is computed above at configure time.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QGC_PLUGIN_UI_ROSTER}")
