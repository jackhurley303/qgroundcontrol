if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    return()
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        MockConfiguration.cc
        MockConfiguration.h
        MockLink.cc
        MockLink.h
        MockLinkCamera.cc
        MockLinkCamera.h
        MockLinkFTP.cc
        MockLinkFTP.h
        MockLinkGimbal.cc
        MockLinkGimbal.h
        MockLinkWorker.cc
        MockLinkWorker.h
        MockLinkMissionItemHandler.cc
        MockLinkMissionItemHandler.h
)

# Add resource files
set_source_files_properties(MockLink.General.MetaData.json PROPERTIES QT_RESOURCE_ALIAS General.MetaData.json)
set_source_files_properties(MockLink.General.MetaData.json.xz PROPERTIES QT_RESOURCE_ALIAS General.MetaData.json.xz)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_precompile_headers(${CMAKE_PROJECT_NAME} PRIVATE "MockLink.h")
