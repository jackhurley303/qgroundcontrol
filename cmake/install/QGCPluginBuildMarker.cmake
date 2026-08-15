# ============================================================================
# QGCPluginBuildMarker.cmake
#
# The per-build discriminator a plugin exports so the host can tell which build
# of it is actually executing (plugin-enable-disable-correctness, Pillar 4):
# an extern "C" qgcPluginBuildMarker() the loader reads from the MAPPED IMAGE
# via QLibrary::resolve(), never from the file — anything read from the file
# names the build that was deployed, not the one that is running.
#
# ONE implementation, two callers, because out-of-tree is the primary plugin
# form and an in-tree-only marker would leave a real released plugin reporting
# nothing:
#   * in-tree dev loop  — cmake/modules/PluginHelpers.cmake includes this file
#                         and qgc_add_plugin() calls the function below;
#   * packaged SDK      — this file installs beside QGCPluginAPIConfig.cmake
#                         (cmake/install/InstallPluginSDK.cmake), which includes
#                         it, so a plugin's standalone PROJECT_IS_TOP_LEVEL
#                         branch calls the same function after
#                         find_package(QGCPluginAPI).
#
# The function is what is shared, not its inputs. QGCPluginAPIConfig.cmake.in
# already exports QGCPluginAPI_APP_NAME/_ORG_NAME/_STABLE_BUILD "as one source
# of truth" and each arm still re-implements the deploy-dir derivation from
# them — three copies of one rule. Exporting values invites that; exporting the
# function does not.
# ============================================================================

include_guard(GLOBAL)

# The one thing that genuinely differs per arm: in-tree the SDK headers live in
# the source tree, in a packaged SDK they live under the install prefix. The
# includer states which, because only it knows — the config file has its own
# PACKAGE_PREFIX_DIR, and deriving the prefix from this file's location instead
# would break on any CMAKE_INSTALL_LIBDIR deeper than lib/ (Debian multiarch).
#
# Recorded as a GLOBAL property rather than left as the variable: a normal
# variable set at include time is visible only in the including directory's
# scope and its children, and qgc_add_plugin() is also called from
# test/PluginSystem/TestPlugin/ — a sibling scope, where that variable would
# simply be empty.
if(NOT QGC_PLUGIN_API_HEADER_DIR)
    message(FATAL_ERROR
        "QGCPluginBuildMarker.cmake was included without QGC_PLUGIN_API_HEADER_DIR. "
        "Set it to the directory holding the QGCPluginAPI public headers first.")
endif()
if(NOT IS_DIRECTORY "${QGC_PLUGIN_API_HEADER_DIR}")
    message(FATAL_ERROR
        "QGC_PLUGIN_API_HEADER_DIR is ${QGC_PLUGIN_API_HEADER_DIR}, which is not a "
        "directory — the build marker would stop tracking SDK header changes.")
endif()
# Normalised before it is stored: the in-tree includer states the path relative to its
# own location, and file(GLOB) does not collapse the ".." segments that leaves behind.
get_filename_component(_qgc_plugin_api_header_dir "${QGC_PLUGIN_API_HEADER_DIR}" REALPATH)
set_property(GLOBAL PROPERTY QGC_PLUGIN_API_HEADER_DIR "${_qgc_plugin_api_header_dir}")

#[=======================================================================[.rst:
qgc_plugin_build_marker
-----------------------

Generates the build-marker translation unit for one plugin and returns its
path; the caller adds it to the plugin's own target.

  qgc_plugin_build_marker(MyPlugin
      OUT_SOURCE MY_PLUGIN_MARKER_SOURCE
      MANIFEST   qgcplugin.json.in
      SOURCES    MyPlugin.h MyPlugin.cc
      QRC_FILES  MyPlugin.qrc
  )
  add_library(MyPlugin MODULE MyPlugin.h MyPlugin.cc MyPlugin.qrc
              ${MY_PLUGIN_MARKER_SOURCE})

SOURCES/QRC_FILES/MANIFEST are the plugin's real inputs — pass exactly what the
target is built from, since they are what the marker's regeneration depends on.

#]=======================================================================]

function(qgc_plugin_build_marker PLUGIN_NAME)
    set(oneValueArgs OUT_SOURCE MANIFEST)
    set(multiValueArgs SOURCES QRC_FILES)
    cmake_parse_arguments(MARKER "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MARKER_OUT_SOURCE)
        message(FATAL_ERROR "qgc_plugin_build_marker(${PLUGIN_NAME}): OUT_SOURCE is required.")
    endif()
    if(NOT MARKER_MANIFEST)
        message(FATAL_ERROR "qgc_plugin_build_marker(${PLUGIN_NAME}): MANIFEST is required.")
    endif()
    if(NOT MARKER_SOURCES)
        message(FATAL_ERROR "qgc_plugin_build_marker(${PLUGIN_NAME}): SOURCES is required.")
    endif()
    # A misspelled keyword is the one mistake that stays silent otherwise. QRC_FILE for
    # QRC_FILES parses as a stray argument, the .qrc and every file it embeds drop out of
    # the DEPENDS below, and the plugin then keeps one marker across QML edits that relink
    # it — the exact case this whole mechanism exists to detect, reported as success. Now
    # that plugin authors write this call by hand (SDK-README.md), the parse has to be
    # checked rather than assumed.
    #
    # KEYWORDS_MISSING_VALUES is deliberately NOT an error here: qgc_add_plugin() forwards
    # QRC_FILES unconditionally, so a plugin with no .qrc of its own (plugins/qdrive lists
    # its own inside SOURCES) legitimately passes the keyword with nothing after it. The
    # required keywords are each checked for an empty value above instead.
    #
    # This catches a misspelling that precedes another keyword. One in trailing position
    # folds into the preceding multi-value list instead and is invisible here — measured,
    # not assumed: it then fails the build with "No rule to make target .../QRC_FILE,
    # needed by <Plugin>_BuildMarker.cc", because the stray token becomes a DEPENDS path.
    # Loud either way; neither position can silently drop the real dependencies.
    if(MARKER_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "qgc_plugin_build_marker(${PLUGIN_NAME}): unrecognised argument(s): "
            "${MARKER_UNPARSED_ARGUMENTS}. Expected OUT_SOURCE, MANIFEST, SOURCES, "
            "QRC_FILES.")
    endif()

    # Build marker: a timestamp+random value baked into an exported extern "C"
    # symbol, read at runtime via QLibrary::resolve() against the mapped image
    # (never the file) so the host can tell two builds of the same plugin
    # id/version apart.
    #
    # The marker must differ whenever the linked dylib differs, so it is the
    # OUTPUT of an ordinary custom command whose DEPENDS are the real files that
    # produce that dylib. An OUTPUT-producing command is ordered before the
    # compile of its own output by construction, so the string inside the linked
    # binary is always this build's — and a dylib that did not change keeps its
    # marker, which is the honest answer rather than churn.
    #
    # Three mechanisms were tried here and are WRONG — do not reintroduce them:
    #   * file(WRITE) alone runs only at configure time, so an incremental build
    #     that recompiles changed sources without touching CMakeLists.txt relinks
    #     new code against a stale marker object.
    #   * add_custom_command(TARGET ... PRE_BUILD) is silently remapped to
    #     PRE_LINK by every generator except Visual Studio, so it runs AFTER the
    #     marker .cc was compiled — the embedded value sits one build cycle
    #     behind, permanently and invisibly.
    #   * a phony always-dirty target named in DEPENDS is emitted by CMake as an
    #     order-only (`||`) prerequisite: it orders the edge but never makes it
    #     run, so the command simply stops firing after the first build.
    # Q_DECL_EXPORT, not QGCPLUGINAPI_EXPORT: the latter resolves to Q_DECL_IMPORT
    # outside the SDK target, which is __declspec(dllimport) on a definition on
    # Windows — a compile error there, and merely harmless on ELF/Mach-O where it
    # still yields default visibility. This symbol is only ever exported *from* a
    # plugin. Depending on QtGlobal alone also keeps the generated TU free of any
    # SDK include path.
    set(_qgc_plugin_marker_file "${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_BuildMarker.cc")
    set(_qgc_plugin_marker_script "${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_GenerateBuildMarker.cmake")
    string(CONCAT _qgc_plugin_marker_script_content
        "string(TIMESTAMP _ts \"%Y%m%d%H%M%S\")\n"
        "string(RANDOM LENGTH 16 _rand)\n"
        "file(WRITE \"${_qgc_plugin_marker_file}\"\n"
        "    \"// Generated by qgc_plugin_build_marker(). Do not edit.\\n\"\n"
        "    \"#include <QtCore/QtGlobal>\\n\"\n"
        "    \"extern \\\"C\\\" Q_DECL_EXPORT const char* qgcPluginBuildMarker() {\\n\"\n"
        "    \"    return \\\"\${_ts}-\${_rand}\\\";\\n\"\n"
        "    \"}\\n\"\n"
        ")\n"
    )
    # Written only when the content actually differs. A bare file(WRITE) always
    # touches the file, and since the script is a marker dependency below, that
    # would relink every plugin on every reconfigure.
    set(_qgc_plugin_marker_script_stale TRUE)
    if(EXISTS "${_qgc_plugin_marker_script}")
        file(READ "${_qgc_plugin_marker_script}" _qgc_plugin_marker_script_existing)
        if(_qgc_plugin_marker_script_existing STREQUAL _qgc_plugin_marker_script_content)
            set(_qgc_plugin_marker_script_stale FALSE)
        endif()
    endif()
    if(_qgc_plugin_marker_script_stale)
        file(WRITE "${_qgc_plugin_marker_script}" "${_qgc_plugin_marker_script_content}")
    endif()

    # Anything whose change yields a different dylib must also yield a different
    # marker: this plugin's own sources and resources, its manifest, and the SDK
    # it links. The SDK goes in as a FILE ($<TARGET_FILE:...>) rather than a
    # target name, because a target name in DEPENDS is order-only and would not
    # trigger regeneration — the trap the third bullet above describes. The
    # namespaced spelling is deliberate: it is an ALIAS in-tree and the imported
    # target out-of-tree, so one expression covers both arms.
    #
    # $<TARGET_FILE:QGCPluginAPI> alone is NOT enough to cover the SDK: it only
    # moves when the SDK dylib relinks, and QGCPluginInterface.h has no backing
    # .cc in that library, so editing it — the file carrying
    # QGC_PLUGIN_API_VERSION_MAJOR, the most ABI-relevant content there is —
    # recompiles every plugin without relinking the SDK. The public header set
    # therefore goes in explicitly.
    if(NOT TARGET QGCPluginAPI::QGCPluginAPI)
        message(FATAL_ERROR
            "qgc_plugin_build_marker(${PLUGIN_NAME}): no QGCPluginAPI::QGCPluginAPI target. "
            "Call this after find_package(QGCPluginAPI) (standalone) or from the in-tree "
            "plugin helper.")
    endif()
    get_property(_qgc_plugin_marker_header_dir GLOBAL PROPERTY QGC_PLUGIN_API_HEADER_DIR)
    # Globbed rather than listed: the set differs per arm (source tree vs the
    # installed include/QGCPluginAPI/) while the rule — every public SDK header
    # is an input — does not. In-tree a header added to the SDK surface is picked
    # up automatically, since adding one always edits src/PluginAPI/CMakeLists.txt
    # and reconfigures. Out-of-tree the glob is read at the plugin's own configure
    # time, so an SDK upgraded in place under an already-configured plugin only
    # gains new headers on the next reconfigure — narrow, since such an upgrade
    # also moves the SDK dylib, which is a dependency in its own right.
    file(GLOB _qgc_plugin_marker_sdk_headers "${_qgc_plugin_marker_header_dir}/*.h")
    if(NOT _qgc_plugin_marker_sdk_headers)
        message(FATAL_ERROR
            "qgc_plugin_build_marker(${PLUGIN_NAME}): no SDK headers found in "
            "${_qgc_plugin_marker_header_dir}. The build marker would silently stop "
            "tracking SDK header changes.")
    endif()
    # The generator script itself is a dependency: changing how the marker is
    # emitted must regenerate it, or a fix to the generated code sits unapplied
    # until some unrelated input happens to change.
    set(_qgc_plugin_marker_deps
        "$<TARGET_FILE:QGCPluginAPI::QGCPluginAPI>"
        "${_qgc_plugin_marker_script}"
        ${_qgc_plugin_marker_sdk_headers}
    )
    foreach(_qgc_plugin_marker_input IN LISTS MARKER_SOURCES MARKER_QRC_FILES MARKER_MANIFEST)
        if(IS_ABSOLUTE "${_qgc_plugin_marker_input}")
            list(APPEND _qgc_plugin_marker_deps "${_qgc_plugin_marker_input}")
        else()
            list(APPEND _qgc_plugin_marker_deps "${CMAKE_CURRENT_SOURCE_DIR}/${_qgc_plugin_marker_input}")
        endif()
    endforeach()

    # A .qrc's own mtime does not move when a file it embeds changes, but AUTORCC
    # rebuilds and relinks the dylib when one does — QML and SVG edits are the
    # commonest change of all in the dev loop. So the marker depends on the files
    # each .qrc lists, not merely on the .qrc. Each entry's path is relative to
    # its .qrc's directory, matching how rcc itself resolves them.
    #
    # The .qrc files also join CMAKE_CONFIGURE_DEPENDS: this list is derived at
    # configure time, so without that, a file newly added to a .qrc would not be
    # tracked until something else happened to trigger a reconfigure.
    #
    # .qrc files are collected from SOURCES as well as QRC_FILES on purpose: both
    # spellings work for building (AUTORCC keys off the extension, not the
    # keyword), so a caller that passes its .qrc in SOURCES — plugins/qdrive does
    # — must not silently lose resource tracking. Correctness here cannot depend
    # on which keyword the call site happened to choose.
    #
    # Known limitation: the extraction below is a regex, not an XML parser, so a
    # <file> element commented out inside the .qrc would still be picked up. No
    # .qrc in this tree does that, and an extra dependency is harmless anyway.
    set(_qgc_plugin_qrc_all ${MARKER_QRC_FILES})
    foreach(_qgc_plugin_source IN LISTS MARKER_SOURCES)
        # Case-insensitive, because AUTORCC's own suffix match is: a MyPlugin.QRC
        # in SOURCES would be compiled and linked by AUTORCC while a
        # case-sensitive test here silently skipped tracking its contents.
        get_filename_component(_qgc_plugin_source_ext "${_qgc_plugin_source}" LAST_EXT)
        string(TOLOWER "${_qgc_plugin_source_ext}" _qgc_plugin_source_ext)
        if(_qgc_plugin_source_ext STREQUAL ".qrc")
            list(APPEND _qgc_plugin_qrc_all "${_qgc_plugin_source}")
        endif()
    endforeach()
    if(_qgc_plugin_qrc_all)
        list(REMOVE_DUPLICATES _qgc_plugin_qrc_all)
    endif()

    foreach(_qgc_plugin_qrc IN LISTS _qgc_plugin_qrc_all)
        if(IS_ABSOLUTE "${_qgc_plugin_qrc}")
            set(_qgc_plugin_qrc_path "${_qgc_plugin_qrc}")
        else()
            set(_qgc_plugin_qrc_path "${CMAKE_CURRENT_SOURCE_DIR}/${_qgc_plugin_qrc}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_qgc_plugin_qrc_path}")

        # Scanning happens at configure time, so a .qrc generated by the build
        # cannot be read here. Say that plainly instead of failing with a bare
        # "file does not exist" pointing at a line the caller never wrote.
        if(NOT EXISTS "${_qgc_plugin_qrc_path}")
            message(FATAL_ERROR
                "qgc_plugin_build_marker(${PLUGIN_NAME}): resource file not found at configure "
                "time: ${_qgc_plugin_qrc_path}. A generated .qrc cannot have its contents "
                "tracked by the build marker; list its inputs in SOURCES instead.")
        endif()

        get_filename_component(_qgc_plugin_qrc_dir "${_qgc_plugin_qrc_path}" DIRECTORY)
        file(READ "${_qgc_plugin_qrc_path}" _qgc_plugin_qrc_xml)
        string(REGEX MATCHALL "<file[^>]*>[^<]+</file>" _qgc_plugin_qrc_entries "${_qgc_plugin_qrc_xml}")
        foreach(_qgc_plugin_qrc_entry IN LISTS _qgc_plugin_qrc_entries)
            string(REGEX REPLACE "<file[^>]*>([^<]+)</file>" "\\1" _qgc_plugin_qrc_rel "${_qgc_plugin_qrc_entry}")
            string(STRIP "${_qgc_plugin_qrc_rel}" _qgc_plugin_qrc_rel)
            list(APPEND _qgc_plugin_marker_deps "${_qgc_plugin_qrc_dir}/${_qgc_plugin_qrc_rel}")
        endforeach()
    endforeach()

    add_custom_command(
        OUTPUT "${_qgc_plugin_marker_file}"
        COMMAND ${CMAKE_COMMAND} -P "${_qgc_plugin_marker_script}"
        DEPENDS ${_qgc_plugin_marker_deps}
        COMMENT "Regenerating build marker for ${PLUGIN_NAME}"
        VERBATIM
    )

    # The generated marker declares no Q_OBJECT and no resources. Keeping it out
    # of autogen also keeps its custom command clear of the target's *_autogen
    # utility targets, which an earlier BYPRODUCTS-based attempt cycled against
    # ("cyclic dependencies are allowed only among static libraries"). Source
    # file properties are directory-scoped and a function call does not change
    # the current directory, so setting this here reaches the caller's target.
    set_source_files_properties("${_qgc_plugin_marker_file}" PROPERTIES SKIP_AUTOMOC ON SKIP_AUTORCC ON)

    set(${MARKER_OUT_SOURCE} "${_qgc_plugin_marker_file}" PARENT_SCOPE)
endfunction()
