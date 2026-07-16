# miniz

Vendored single-header/single-source amalgamation of [miniz](https://github.com/richgel999/miniz)
3.1.2 (MIT license, see `LICENSE`), taken from the project's `3.1.2` release asset
(`miniz-3.1.2.zip`), not the repository tree (which has since split into multiple
`miniz_*.h/.c` files).

Used by `PluginInstaller` (`src/PluginSystem/PluginInstaller.cc`) to extract
`.qgcplugin` zip packages in-process — required so extracted files don't inherit
`com.apple.quarantine` from the downloaded zip (see `04-macos-implementation-plan.md`
§1.4). No Qt private API is used for this (D8).

**Why not QGCCompression (libarchive)?** QGC's vendored xz-utils build has all LZMA
encoders disabled (`XZ_ENCODERS ""` in `src/Utilities/Compression/CMakeLists.txt`) —
decoder-only by design, since nothing previously needed to *write* an archive.
libarchive's ZIP writer needs the LZMA encoder internally even for plain (non-LZMA)
zip output, so `QGCCompression` cannot create zip archives without re-enabling
encoders app-wide — a larger change than this plugin-installer feature warrants.
`PluginInstaller` therefore uses miniz for extraction; the read side has no such
limitation, but miniz is used throughout for symmetry with the (test-only) writer
need in `PluginInstallerTest`.

To update: download the amalgamated release zip for the desired version from
https://github.com/richgel999/miniz/releases and replace `miniz.c`/`miniz.h`.
