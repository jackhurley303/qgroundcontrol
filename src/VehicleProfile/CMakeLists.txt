# ============================================================================
# Vehicle Profile Module
# The .vehicle file format: one vehicle's saved profile (name, airframe, sensors,
# flight controller, image), read from and written to JSON, and the manager that
# keeps the list of saved vehicles in the Vehicles folder.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE VehicleProfile.cc VehicleProfile.h VehicleProfileEntry.cc
                                             VehicleProfileEntry.h VehicleProfileManager.cc VehicleProfileManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
