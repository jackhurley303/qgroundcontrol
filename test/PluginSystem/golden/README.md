# Golden plugin fixture

`ExamplePlugin-v2.dylib` is a frozen, committed build of `plugins/example`
(the Tier B/SDK-tier reference plugin) — the permanent regression fixture for
the "different-commit-host" ABI proof (definition-of-done #1, 04 §11 Spike
S3): `PluginLoaderGateTest::_activateGoldenPluginAgainstCurrentHost_test()`
loads it through `QGCPluginLoader` on whatever host build CI happens to be
running, proving today's `QGCPluginAPI` ABI still accepts a binary built at
an earlier commit.

It is deliberately **not** rebuilt by CI. Any red here means an ABI rule
broke on the host side — fix that by reverting the break, never by
regenerating this file.

## Provenance

- Built from commit `73f2b4ea40f434bc65c746928fad1fbf73dc9f4f`
  (`plugins/.architecture/05-completion-plan.md` U3.5, the last commit
  before U5.1).
- Universal (`x86_64h;arm64`) Release build, matching
  `cmake/presets/macOS.json`'s shipped architecture — a single-arch fixture
  would only exercise one slice on the arm64 GH runner.
- `QGC_PLUGIN_API_VERSION_MAJOR` = 2 at build time (see the `-v2` filename).

## When to regenerate

Only on a **deliberate** ABI-major bump (`QGC_PLUGIN_API_VERSION_MAJOR`
changes in `src/PluginAPI/QGCPluginInterface.h`) — never to "fix" a red
golden test, which means the ABI broke without a version bump.

```bash
cmake -B build/golden-universal -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$QT_ROOT_DIR/lib/cmake/Qt6/qt.toolchain.cmake \
  -DCMAKE_OSX_ARCHITECTURES="x86_64h;arm64" \
  -DCMAKE_BUILD_TYPE=Release \
  -DQGC_BUILD_TESTING=OFF
cmake --build build/golden-universal --target ExamplePlugin
cp build/golden-universal/Release/plugins/libExamplePlugin.dylib \
   test/PluginSystem/golden/ExamplePlugin-v<NEW_MAJOR>.dylib
```

Update `QGC_GOLDEN_PLUGIN_PATH` in `.github/workflows/macos.yml` and this
file's provenance section to match.
