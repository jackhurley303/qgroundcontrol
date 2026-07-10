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

- U1.5–U1.6: not started.

### Stage 2–5

Not started — blocked on Stage 1 completing (U1.5–U1.6).

## Notes for the next unit (U1.5)

- U1.5 = loader hygiene + docs truth (plan §4, U1.5): warn on second replay-extension registration (first still wins); rewrite `plugins/README.md` and `src/PluginSystem/README.md` for manifest requirement, real linkage model, actual init-order semantics, tier table as roadmap. No tests (docs + one `qCWarning`).
- Run settings: Sonnet, thinking off (docs + one warning; per plan §11).
