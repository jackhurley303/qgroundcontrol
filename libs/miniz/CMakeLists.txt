# ============================================================================
# miniz — vendored zip read/write library (see README.md for provenance)
# Used only by PluginInstaller for in-process .qgcplugin extraction (D8).
# ============================================================================

add_library(miniz STATIC
    miniz.c
    miniz.h
)

target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# QGC only needs the ZIP archive read/write API (PluginInstaller); the zlib-compatibility
# shim layer (deflate/inflate/compress/... wrappers) is unused and, being static-inline,
# trips -Werror=unused-function in any consumer that doesn't call it.
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_APIS)

set_target_properties(miniz PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
