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

- U1.3–U1.6: not started.

### Stage 2–5

Not started — blocked on Stage 1 completing (U1.3–U1.6).

## Notes for the next unit (U1.3)

- Loader now exposes `knownPluginInfos()` (all states) — the manager's `PluginRecord` list consumes this instead of `loadedPluginInfos()`; to stop executing disabled plugins the manager must drive `_inspect`/`_activate` separately, so U1.3 likely promotes them to public `inspect()`/`activate()` on the loader (plan U1.2 framing: "inspects and activates on request").
- Latent sharp edge to kill in U1.3: `QGCPluginManager::_loadPlugins` deletes disabled plugins *after* activation (the 01 §3.5 defect this unit deliberately left), and the local loader's `_pluginInfos` then briefly holds a dangling `plugin` pointer still marked Active (unreachable today — loader is function-local — but don't carry the shape forward).
- `reloadPlugin`'s scan-everything fallback gets deleted per plan U1.3; `PluginSettings` re-keys Facts by manifest id (D5, no migration shim).
