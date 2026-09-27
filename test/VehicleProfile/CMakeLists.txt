# ============================================================================
# Vehicle Profile Unit Tests
# Tests for VehicleProfile: the .vehicle JSON file format and the image import rule
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE VehicleProfileTest.cc VehicleProfileTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(VehicleProfileTest LABELS Unit Utilities)
