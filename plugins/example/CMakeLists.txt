cmake_minimum_required(VERSION 3.25)

project(ExamplePlugin VERSION 1.0.0 LANGUAGES CXX)

# This plugin builds as part of the QGC build system (dev loop), but is a pure
# Tier B plugin: it links only the published QGCPluginAPI SDK + Qt, never a QGC
# header outside src/PluginAPI/ or a QGC object file. See plugins/README.md.

qgc_add_plugin(ExamplePlugin
    TIER SDK
    MANIFEST qgcplugin.json.in
    SOURCES
        ExamplePlugin.h
        ExamplePlugin.cc
    QRC_FILES
        ExamplePlugin.qrc
)
