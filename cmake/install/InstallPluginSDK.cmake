# ============================================================================
# InstallPluginSDK.cmake
#
# Packages the QGCPluginAPI SDK (D2/D3, src/PluginAPI/) as a standalone,
# distributable artifact: public headers, the versioned shared library, a
# CMake package config so `find_package(QGCPluginAPI)` works from a project
# that has never cloned QGC, a copy-and-build plugin-project template, and
# the compatibility-contract doc (plugins/template/SDK-README.md).
#
# Everything here installs under one component, QGCPluginSDK, so it never
# rides along with a plain `cmake --install .` of the app bundle — CI (and
# any local rehearsal of definition-of-done #1) selects it explicitly:
#   cmake --install <build-dir> --component QGCPluginSDK --prefix <out-dir>
# ============================================================================

include(CMakePackageConfigHelpers)

# No INCLUDES DESTINATION here: src/PluginAPI/CMakeLists.txt's own
# target_include_directories() already sets $<INSTALL_INTERFACE:include>, the only
# install-side include dir this target needs — adding INCLUDES DESTINATION too would
# just duplicate that same entry in the exported target's interface.
install(TARGETS QGCPluginAPI
    EXPORT QGCPluginAPITargets
    COMPONENT QGCPluginSDK
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

# QGC_PLUGIN_API_PUBLIC_HEADERS holds bare filenames (correct as-is for
# src/PluginAPI/CMakeLists.txt's own qt_add_library call, in that directory's
# context); resolve them against src/PluginAPI/ here since install() runs in the
# root CMakeLists.txt's directory context instead.
list(TRANSFORM QGC_PLUGIN_API_PUBLIC_HEADERS PREPEND "${CMAKE_SOURCE_DIR}/src/PluginAPI/"
    OUTPUT_VARIABLE QGC_PLUGIN_API_PUBLIC_HEADER_PATHS
)
install(FILES ${QGC_PLUGIN_API_PUBLIC_HEADER_PATHS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QGCPluginAPI"
    COMPONENT QGCPluginSDK
)

install(EXPORT QGCPluginAPITargets
    FILE QGCPluginAPITargets.cmake
    NAMESPACE QGCPluginAPI::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
    COMPONENT QGCPluginSDK
)

configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/install/QGCPluginAPIConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
)

# Tracks the SDK target's own VERSION/SOVERSION (src/PluginAPI/CMakeLists.txt), which
# is itself kept in lockstep with QGC_PLUGIN_API_VERSION_MAJOR by F7's cross-reference
# comment — reading the CMake target property here avoids yet a third copy of that
# fact. SameMajorVersion so find_package(QGCPluginAPI 2) rejects a mismatched SDK
# major and accepts any minor/patch within it (D3's SOVERSION rule, mirrored into the
# find_package() axis a plugin author actually uses).
get_target_property(QGC_PLUGIN_API_PACKAGE_VERSION QGCPluginAPI VERSION)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfigVersion.cmake"
    VERSION "${QGC_PLUGIN_API_PACKAGE_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfig.cmake"
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
    COMPONENT QGCPluginSDK
)

# Copy-and-build starting point (plan §5 U2.7): a standalone CMake project that
# links the SDK via find_package(QGCPluginAPI), never qgc_add_plugin() — that helper
# is an in-tree build convenience, not part of the published package.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/plugins/template/"
    DESTINATION "template"
    COMPONENT QGCPluginSDK
    PATTERN "SDK-README.md" EXCLUDE
)

install(FILES "${CMAKE_SOURCE_DIR}/plugins/template/SDK-README.md"
    DESTINATION "."
    COMPONENT QGCPluginSDK
)

# CI reads this to name qgc-plugin-sdk-<platform>-<version>.zip without re-deriving
# `git describe` in the workflow (and risking divergence from what this configure run
# already computed via cmake/modules/Git.cmake).
file(WRITE "${CMAKE_BINARY_DIR}/qgc-plugin-sdk-version.txt" "${PROJECT_VERSION}")
