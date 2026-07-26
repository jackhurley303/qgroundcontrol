qt_add_library(QGCAudio STATIC
    AudioOutput.cc
    AudioOutput.h
)

target_link_libraries(QGCAudio
    PUBLIC
        Qt6::Core
        Qt6::TextToSpeech
    PRIVATE
        QGCLogging
)

target_include_directories(QGCAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/FactSystem
        ${CMAKE_SOURCE_DIR}/src/Utilities
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCAudio)

set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/audio/alert.wav" PROPERTIES QT_RESOURCE_ALIAS "Alert")
qt_add_resources(${CMAKE_PROJECT_NAME} audio_resources
    PREFIX "/res/audio"
    BASE "${CMAKE_SOURCE_DIR}/resources/audio"
    FILES "${CMAKE_SOURCE_DIR}/resources/audio/alert.wav"
)

target_precompile_headers(QGCAudio PRIVATE
    <QtCore/QObject>
    <QtCore/QString>
    <QtTextToSpeech/QTextToSpeech>
)
