# ============================================================================
# Vehicle Setup Subsystem
# Vehicle configuration, calibration, and firmware upgrade
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        JoystickConfigController.cc
        JoystickConfigController.h
        RemoteControlCalibrationController.cc
        RemoteControlCalibrationController.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT QGC_NO_SERIAL_LINK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            Bootloader.cc
            Bootloader.h
            FirmwareImage.cc
            FirmwareImage.h
            FirmwareUpgradeController.cc
            FirmwareUpgradeController.h
            PX4FirmwareUpgradeThread.cc
            PX4FirmwareUpgradeThread.h
    )
endif()

qt_add_resources(${CMAKE_PROJECT_NAME} vehiclesetup_qmlimages
    PREFIX "/qmlimages"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/FirmwareUpgradeIcon.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/VehicleSummaryIcon.png"
)

qt_add_resources(${CMAKE_PROJECT_NAME} vehiclesetup_firmware_images
    PREFIX "/res/firmware"
    BASE "${CMAKE_SOURCE_DIR}/resources/firmware"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/firmware/3drradio.png"
        "${CMAKE_SOURCE_DIR}/resources/firmware/apm.png"
        "${CMAKE_SOURCE_DIR}/resources/firmware/px4.png"
)

qt_add_library(VehicleSetupModule STATIC)

qt_add_qml_module(VehicleSetupModule
    URI QGroundControl.VehicleSetup
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        FirmwareUpgrade.qml
        JoystickComponent.qml
        JoystickComponentButtons.qml
        JoystickComponentButtonMonitor.qml
        JoystickComponentSettings.qml
        OpticalFlowSensor.qml
        RemoteControlCalibration.qml
        RemoteControlChannelMonitor.qml
        RemoteControlChannelValueDisplay.qml
        SetupParameterEditor.qml
        VehicleConfigView.qml
        VehicleSummary.qml
    NO_PLUGIN
)

