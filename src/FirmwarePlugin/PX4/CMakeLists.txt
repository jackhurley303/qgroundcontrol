if(NOT QGC_DISABLE_PX4_PLUGIN_FACTORY)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            PX4FirmwarePluginFactory.cc
            PX4FirmwarePluginFactory.h
    )
endif()

if(NOT QGC_DISABLE_PX4_PLUGIN)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            px4_custom_mode.h
            PX4FirmwarePlugin.cc
            PX4FirmwarePlugin.h
            PX4ParameterMetaData.cc
            PX4ParameterMetaData.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Add qml module
qt_add_library(PX4FirmwareModule STATIC)

qt_add_qml_module(PX4FirmwareModule
    URI QGroundControl.FirmwarePlugin.PX4
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        PX4BatteryIndicator.qml
        PX4FlightModeIndicator.qml
        PX4MainStatusIndicator.qml
    NO_PLUGIN
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PX4FirmwareModule)
