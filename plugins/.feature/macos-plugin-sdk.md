# macOS Plugin SDK — feature-change tracker

**Profile:** `/feature-change` (additive) for Stage 1; Stages 2–3 switch to `/architecture-change` per the blueprint's Execution notes (§11 of the plan doc).
**Blueprint:** [plugins/.architecture/04-macos-implementation-plan.md](../.architecture/04-macos-implementation-plan.md) — grounded design, locked decisions D1–D11, spikes S1/S2m/S3/S4/S6m all PASS (2026-07-06).

## Status

### Stage 1 — The declared contract (manifest + loader gate)

- **U1.1 — `PluginManifest` value type + validation + tests** — DONE (2026-07-09).
  - New: `src/PluginSystem/PluginManifest.h/.cc` — value type (`id, name, version, vendor, description, tier, apiVersion, hostVersionMin/Max, hostBuildId, contributes`), `fromJson()`, `validateForHost()`.
  - Wired into `src/PluginSystem/CMakeLists.txt`.
  - Tests: `test/PluginSystem/PluginManifestTest.{h,cc}` (11 cases) registered in `test/CMakeLists.txt` + `test/PluginSystem/CMakeLists.txt`. All pass (`--unittest:PluginManifestTest`).
  - Pure logic, no libraries loaded — matches the plan's test description exactly.
  - Next: U1.2 (loader gate: metadata before code) in a fresh chat.

- **U1.2 — Loader gate: metadata before code** — DONE (2026-07-09).
  - `QGCPluginLoader::_loadPlugin` split into `_inspect(filePath)` (metaData → IID → `PluginManifest::fromMetaData` → `validateForHost`, zero code execution) + `_activate(info)` (instance/cast/createPlugin). `PluginLoadInfo` grew `manifest`/`state` (full `PluginState` enum incl. not-yet-used `Disabled`/`Quarantined`)/`errorString`; loader stores all inspected results, `loadedPluginInfos()` stays Active-only so `QGCPluginManager` is untouched; new `knownPluginInfos()` exposes everything for U1.3/U1.4.
  - `PluginManifest::fromMetaData(envelope, expectedIid)` added (pure, tested); host identity via `QGCPluginLoader::hostInfo()` from `QGC_APP_VERSION_STR` + new `QGC_GIT_HASH` in `qgc_version.h.in`; `inline constexpr QGCPluginApiVersion = 1` in `QGCPluginInterface.h` (U2.1 bumps this one place).
  - Both plugins stamp `Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")` with `qgcplugin.json.in` configured per plugin (example: `org.qgroundcontrol.example`; qdrive: `org.qdrive.plugin` — ids still cheap to change until U1.3 keys settings by them). QDrive changes are a separate commit in its nested repo.
  - Verified: 18 PluginManifestTest cases green; live boot logs "Validated <name> (internal, build <hash>) before load" for both plugins; wrong-IID dylib rejected legibly without crash. `/code-review` medium: 8 findings, 6 fixed (incl. null-host-version rejecting all plugins on tagless checkouts — range check now skipped when host version unparseable), 2 skipped (git-less "0000000" build-id sentinel match; inspect→activate TOCTOU — closed later by U3.3's file-hash consent).

- **U1.3 — Manager: id-keyed records, no execution of disabled plugins** — DONE (2026-07-10).
  - `QGCPluginManager` now keeps `QList<PluginLoadInfo> _records` (one per discovered plugin, any state) — the plan's `PluginRecord` realized by reusing U1.2's `PluginLoadInfo` rather than a parallel struct. Flow: `inspectDirectories` → `_processInspected` (register id with settings → duplicate-id guard → skip disabled **without ever calling `instance()`** → `_activateRecord`). The 01 §3.5 defect (disabled plugins executing code) is dead.
  - `QGCPluginLoader` became a fully **static, stateless** inspect/activate utility (no QObject, no `_pluginInfos`, `loadPlugins`/`loadedPluginInfos`/`knownPluginInfos`/signals deleted; directory scan sorted by name for deterministic duplicate resolution).
  - `PluginSettings` keys Facts by manifest id (D5, no shim): `registerPlugin(id, displayName, defaultEnabled)`, `registeredPluginIds`. Interim default rule (Example off by id `org.qgroundcontrol.example`, rest on) lives in the manager, awaiting D10/U3.3.
  - API: `unloadPlugin`/`reloadPlugin(name)` → `setPluginEnabled(id, bool)` (writes Fact + reconciles activation, idempotent/reentrancy-safe) + `reloadPlugin(id)` (stored path only, scan fallback deleted; refuses id-change-on-disk to keep settings keys coherent). QML page minimally updated (full revamp = U1.4). Contribution maps carry `pluginId` (identity) + `name` (display, from manifest).
  - Tests: `test/PluginSystem/QGCPluginManagerTest` (9 cases; fixtures use nonexistent paths so wrongful activation is detectable as Failed-instead-of-Disabled). Full Unit suite green except 3 pre-existing keychain-timeout tests (environmental). `/code-review` high: 7 findings — 4 fixed (reload id-drift, name-keyed default, unsorted scan, static loader), 3 plausible-skipped (latent record-pointer reentrancy; same-display-name QML panel-key collision (pre-existing, U2.2 reworks consumption); stale Fact label after same-id reload (U1.4 reads names from records)).

- **U1.4 — Settings page shows the contract** — DONE (2026-07-10).
  - `QGCPluginManager` gained `knownPlugins()` (`Q_PROPERTY`, NOTIFY reuses `loadedPluginsChanged` — every `_records` mutation site already emits it) returning every record's id/name/version/vendor/description/state (raw enum name, for QML color-coding)/statusText (composed human line: "Active", "Disabled", "Incompatible: <reason>", "Failed to load: <reason>", "Quarantined: <reason>").
  - `PluginSettings.qml` Repeater now iterates `knownPlugins` instead of `registeredPluginIds`: each row shows name/version/vendor (read from the record, not `fact.label`) + the enable toggle + a colored status line beneath (green/red/default by state). Absorbed both U1.3 review leftovers: display name no longer goes stale after a reload, and Incompatible/Failed rows now show why they aren't running instead of just an ambiguous toggle. Empty-state check switched from `loadedPlugins` to `knownPlugins`.
  - Tests: `_knownPluginsReflectsRecords_test` added to `QGCPluginManagerTest` (Active + Incompatible fixtures, verifies map shape and composed statusText). Full suite green except the 3 pre-existing keychain-timeout tests (environmental, same as U1.3). `/code-review low`: no findings.
  - Next: U1.5 (loader hygiene + docs truth) in a fresh chat.

- **U1.5 — Loader hygiene + docs truth** — DONE (2026-07-10).
  - `QGCPluginManager::_activateRecord` ([QGCPluginManager.cc](../../src/PluginSystem/QGCPluginManager.cc)): second plugin to report a replay extension now logs `qCWarning(QGCPluginManagerLog)` naming the plugin id and states first-registration-wins; behavior unchanged (still first wins), just no longer silent.
  - `plugins/README.md` and `src/PluginSystem/README.md` fully rewritten — both were describing a submodule/git-distributed, name-keyed, `pluginInterfaceVersion()`-checked plugin model that predated U1.1–U1.4 entirely. Now describe: manifest schema + fields actually validated, two-phase inspect/activate, id-keyed `PluginSettings`, real search paths (from `QGCPluginLoader::defaultPluginPaths()`), real linkage (`-undefined dynamic_lookup`/`-Wl,--allow-shlib-undefined`, no `qgc_add_plugin()` yet — that's U1.6), and a tier table framed as roadmap (`internal` is the only tier that works today; `sdk`/`qml` are Stage 2/3 targets, not present features).
  - Also removed a stray gitignored `plugins/example/build/` directory (leftover manual build, not part of the commit either way — `plugins/.gitignore` already excludes `build/`).
  - No tests (docs + one `qCWarning`, per plan). `/code-review low`: no findings.
  - Next: U1.6 (`qgc_add_plugin()` becomes the single path) in a fresh chat.

- **U1.6 — `qgc_add_plugin()` becomes the single path** — DONE (2026-07-10). **Stage 1 complete.**
  - `cmake/modules/PluginHelpers.cmake` rewritten: `qgc_add_plugin()` now absorbs manifest plumbing (`MANIFEST` arg → `configure_file` into the binary dir + include path, previously hand-rolled per plugin), `TIER` validation (only `INTERNAL` accepted; `SDK`/`QML` `FATAL_ERROR` pointing at Stage 2/3 — D7's "errors helpfully" framing pulled forward), the undefined-symbol link options (`-undefined dynamic_lookup` / `-Wl,--allow-shlib-undefined`), and the dev-loop auto-deploy `POST_BUILD` step — all previously duplicated verbatim in both plugins' CMakeLists.
  - **Deploy dir is now computed, not hardcoded**: mirrors `QGCApplication.cc`'s `_setInstanceInfo()` applicationName logic exactly (`"${QGC_APP_NAME} Daily"` unless `QGC_STABLE_BUILD`, using `QGC_ORG_NAME`) instead of the literal string `"QGroundControl Daily"` that was in both plugins' CMakeLists before. Correct for any `QGC_APP_NAME`/`QGC_ORG_NAME`/`QGC_STABLE_BUILD` combination now, not just the default.
  - `plugins/example/CMakeLists.txt` and `plugins/qdrive/CMakeLists.txt` (nested repo, separate commit there) both collapse to a `qgc_add_plugin(...)` call; QDrive keeps its extra `target_link_libraries(... Qt6::Sql Qt6::Gui)` after the call (helper doesn't guess a plugin's extra Qt modules — by design, extras stay caller-side per the header doc).
  - `plugins/README.md` "Creating a New Plugin" section updated with the real `qgc_add_plugin()` signature and usage.
  - `plugins/example/build.sh`/`build.bat` (manual single-target rebuild+deploy scripts) left as-is — still correct since the computed deploy dir is unchanged for the default dev config they target.
  - No tests (pure CMake consolidation, no new logic, per plan). Verified: clean build of both plugin targets + full app; both `.dylib`s landed in the build output and were auto-deployed to the live runtime plugin dir; live app run confirmed QDrive plugin initialized and ran normally (log lines, no load errors); manifest content correct in both configured `qgcplugin.json` outputs. `/code-review low`: no findings.
  - Next: Stage 2, U2.1 (library skeleton + type moves) — first unit of the SDK boundary. Entered as an **`/architecture-change`** per the plan's Execution notes (§11): Stages 2–3 are a clean-cutover profile (linkage model reshapes, old model deleted), not additive. Fresh chat.

### Stage 2–5

Not started. **Stage 1 (the declared contract) is now fully complete** — U1.1 through U1.6 all DONE. Stage 2 (SDK boundary) is next, entered via `/architecture-change`.

## Notes for the next unit (U2.1)

- U2.1 = library skeleton + type moves (plan §5, U2.1): new `QGCPluginAPI` target in `src/PluginAPI/` (D2/D3) — `qgc_plugin_api_global.h` (export macro), `QGCPluginInterface.h` (IID bump to `.../2.0`, `apiVersion` 2 per D6), `QGCPlugin.h/.cc` (d-pointer, only `init(QGCHostServices*)`/`cleanup()`/`replayExtension()` — U2.2 strips the rest per D1), `QGCHostServices.h/.cc` (abstract `service(id)`), `QGCReplayExtension.h/.cc` (moved verbatim, header comment freezes the vtable).
- This is an ABI-sensitive design unit — plan §11 calls for **Opus**, not the Sonnet-default used for U1.x/U1.6.
- Run S2m's lessons apply directly (already PASS'd 2026-07-06): host-provided `@rpath` dylib, hidden visibility + export macro, zero rpath config needed by plugin authors.
- Verify: build-level (app links, unit suite green); `apiVersion` host constant becomes 2, manifest tests updated. S3 (cross-build survival) reruns after U2.4, not this unit.
