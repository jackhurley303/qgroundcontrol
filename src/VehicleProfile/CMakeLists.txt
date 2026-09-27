# ============================================================================
# Vehicle Profile Module
# The .vehicle file format: one vehicle's saved profile (name, airframe, sensors,
# flight controller, image), read from and written to JSON.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE VehicleProfile.cc VehicleProfile.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
