# ============================================================================
# Vehicle Profile Unit Tests
# Tests for VehicleProfile (the .vehicle JSON file format and the image import rule)
# and VehicleProfileManager (the folder of saved vehicles)
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE VehicleProfileManagerTest.cc VehicleProfileManagerTest.h
                                             VehicleProfileTest.cc VehicleProfileTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(VehicleProfileTest LABELS Unit Utilities)
add_qgc_test(VehicleProfileManagerTest LABELS Unit Utilities)
