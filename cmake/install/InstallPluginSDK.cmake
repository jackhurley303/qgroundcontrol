# ============================================================================
# InstallPluginSDK.cmake
#
# Packages the QGCPluginAPI SDK (D2/D3, src/PluginAPI/) as a standalone,
# distributable artifact: public headers, the versioned shared library, a
# CMake package config so `find_package(QGCPluginAPI)` works from a project
# that has never cloned QGC, the bundled reference plugin (plugins/example/,
# dual-mode), and the compatibility-contract doc (plugins/example/SDK-README.md).
#
# Everything here installs under one component, QGCPluginSDK, so it never
# rides along with a plain `cmake --install .` of the app bundle — CI (and
# any local rehearsal of definition-of-done #1) selects it explicitly:
#   cmake --install <build-dir> --component QGCPluginSDK --prefix <out-dir>
# ============================================================================

include(CMakePackageConfigHelpers)

# No INCLUDES DESTINATION here: src/PluginAPI/CMakeLists.txt's own
# target_include_directories() already sets every install-side include dir this
# target needs (the headers dir plus, since U1, the two mavlink dirs below) via
# $<INSTALL_INTERFACE:...> — adding INCLUDES DESTINATION too would just duplicate
# those same entries in the exported target's interface.
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

# Pinned MAVLink dialect headers (out-of-tree-plugins.md U1): the generated tree
# CPM builds for src/MAVLink/CMakeLists.txt's QGC_MAVLINK_INCLUDE_DIRS, republished
# here so a plugin with no QGC source tree still gets the bare `#include <mavlink.h>` /
# `<mavlink_types.h>` style plugins/qdrive/src/utilities/MAVLinkLib.h relies on. Whole
# tree, not just the pinned dialect subdir, since QGC's own QGC_MAVLINK_DIALECT="all"
# already generates every dialect under one root. QGC_MAVLINK_INCLUDE_DIRS is a
# CACHE INTERNAL list (mavlink_BINARY_DIR itself, a plain CPM variable, doesn't
# propagate to this root-level scope) — its first entry is that root include dir.
list(GET QGC_MAVLINK_INCLUDE_DIRS 0 QGC_MAVLINK_GENERATED_ROOT)
install(DIRECTORY "${QGC_MAVLINK_GENERATED_ROOT}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QGCPluginAPI/mavlink"
    COMPONENT QGCPluginSDK
)

# The published QML contract (out-of-tree-plugins.md U2): the QGroundControl.PluginUI
# module, flattened for a package that has no host resource tree to point into.
# Composite types are shipped as bodies rather than metadata because Qt generates no
# metadata for them at all — these are copies made by this rule from the host's own
# sources, never hand-edited, and they exist only to be linted against. The host still
# supplies every implementation at runtime, since a plugin's QML runs in the host's
# engine. src/PluginSystem/CMakeLists.txt derives all three inputs from one roster.
get_target_property(QGC_PLUGIN_UI_SDK_DIR QGCPluginUISDKModule QGC_PLUGIN_UI_SDK_DIR)
get_target_property(QGC_PLUGIN_UI_QML_BODIES QGCPluginUISDKModule QGC_PLUGIN_UI_QML_BODIES)
install(FILES
    "${QGC_PLUGIN_UI_SDK_DIR}/qmldir"
    "${QGC_PLUGIN_UI_SDK_DIR}/QGroundControl.PluginUI.qmltypes"
    ${QGC_PLUGIN_UI_QML_BODIES}
    DESTINATION "qml/QGroundControl/PluginUI"
    COMPONENT QGCPluginSDK
)

install(EXPORT QGCPluginAPITargets
    FILE QGCPluginAPITargets.cmake
    NAMESPACE QGCPluginAPI::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
    COMPONENT QGCPluginSDK
)

set(QGC_PLUGIN_API_QML_DIR "qml")
# Where the headers land, as a PATH_VAR so the config file's set_and_check() resolves the
# real location rather than a hardcoded "include/" — the build-marker generator reads this
# to build its DEPENDS set, so a non-default CMAKE_INSTALL_INCLUDEDIR would otherwise fail
# find_package(QGCPluginAPI) outright. Same install destination as the install(FILES) above.
set(QGC_PLUGIN_API_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/QGCPluginAPI")
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/install/QGCPluginAPIConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
    PATH_VARS QGC_PLUGIN_API_QML_DIR QGC_PLUGIN_API_INCLUDE_DIR
)
# QGC_APP_NAME/QGC_ORG_NAME/QGC_STABLE_BUILD are plain @VAR@ substitutions (not
# PATH_VARS — they aren't paths), resolved from this scope's existing cache variables
# (cmake/CustomOptions.cmake) by configure_package_config_file()'s underlying
# configure_file() call — no extra PATH_VARS entry needed for them.

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

# QGCPluginBuildMarker.cmake ships beside the config file that includes it, so a
# standalone plugin gets the build-marker generator itself rather than a second
# copy of its logic (plugin-enable-disable-correctness U1b). The in-tree helper
# includes the very same file from cmake/install/ — one implementation, two
# callers, which is what keeps the marker on out-of-tree plugins, the primary
# plugin form.
install(FILES
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfig.cmake"
    "${CMAKE_BINARY_DIR}/QGCPluginAPIConfigVersion.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/install/QGCPluginBuildMarker.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QGCPluginAPI"
    COMPONENT QGCPluginSDK
)

# Copy-and-build starting point (plan §5 U2.7): the one bundled reference plugin,
# dual-mode (out-of-tree-verifier.md U1) — in-tree it builds via qgc_add_plugin() for
# the dev loop; packaged here, it's a standalone CMake project that links the SDK via
# find_package(QGCPluginAPI), the actual out-of-tree path a real SDK consumer uses.
# EXCLUDEs drop the in-tree dev-loop's own docs/scripts (SDK-README.md installs
# separately below; build.sh/build.bat/README.md assume a $QGC_ROOT/build tree that
# doesn't exist in a standalone-extracted SDK zip, so they'd only confuse a
# third-party author) — the package ships plugin sources plus the standalone
# CMakeLists.txt only.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/plugins/example/"
    DESTINATION "example"
    COMPONENT QGCPluginSDK
    PATTERN "SDK-README.md" EXCLUDE
    PATTERN "README.md" EXCLUDE
    PATTERN "build.sh" EXCLUDE
    PATTERN "build.bat" EXCLUDE
)

install(FILES "${CMAKE_SOURCE_DIR}/plugins/example/SDK-README.md"
    DESTINATION "."
    COMPONENT QGCPluginSDK
)

# The out-of-tree gate itself (out-of-tree-verifier.md U2), so a third-party author runs
# the same tool CI runs rather than a description of it. It is stdlib-only and single-file
# precisely so it can ship here: nothing else under tools/ comes with it, and there is no
# QGC checkout to fall back on. PROGRAMS, not FILES, so the shebang is usable. The schema
# ships beside it as the human- and editor-readable statement of the manifest contract; the
# tool validates manifests itself and never depends on it being present.
install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/verify_plugin_out_of_tree.py"
    DESTINATION "tools"
    COMPONENT QGCPluginSDK
)

install(FILES "${CMAKE_SOURCE_DIR}/tools/plugin-verify.schema.json"
    DESTINATION "tools"
    COMPONENT QGCPluginSDK
)

# The .qgcplugin packer. Ships for the same reason the gate above does: the rules it
# enforces are PluginInstaller's, and an author who cannot run them locally only discovers
# a bad package when a user fails to install it. Stdlib-only and single-file, so it runs
# from the unpacked zip with no QGC checkout.
install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/pack_plugin.py"
    DESTINATION "tools"
    COMPONENT QGCPluginSDK
)

# CI reads this to name qgc-plugin-sdk-<platform>-<version>.zip without re-deriving
# `git describe` in the workflow (and risking divergence from what this configure run
# already computed via cmake/modules/Git.cmake).
file(WRITE "${CMAKE_BINARY_DIR}/qgc-plugin-sdk-version.txt" "${PROJECT_VERSION}")
