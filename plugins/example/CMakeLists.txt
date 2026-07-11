cmake_minimum_required(VERSION 3.25)

project(ExamplePlugin VERSION 1.0.0 LANGUAGES CXX)

# This plugin builds as part of the QGC build system
# It has access to all QGC headers and can link against QGC object files

qgc_add_plugin(ExamplePlugin
    TIER INTERNAL
    MANIFEST qgcplugin.json.in
    SOURCES
        ExamplePlugin.h
        ExamplePlugin.cc
    QRC_FILES
        ExamplePlugin.qrc
)
