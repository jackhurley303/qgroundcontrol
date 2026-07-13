# macOS Implementation Plan — Detailed

**Date:** 2026-07-06
**Scope:** Execution-level plan for building the target architecture ([02-target-architecture.md](02-target-architecture.md)) on **macOS only**. Windows and Android are deferred (§10) but every decision here is checked against "does this paint Windows/Android into a corner" — none does.
**Relationship to [03-implementation-plan.md](03-implementation-plan.md):** 03 remains the platform-spanning overview and upstream-PR map. This document decomposes its Phases 0–3 (+ the macOS slice of Phase 5) into commit-sized units with concrete files, mechanisms, and tests, grounded in the code as of `plugin-infrastructure-with-qdrive`.

## Status — current position

Tracked in [plugins/.feature/macos-plugin-sdk.md](../.feature/macos-plugin-sdk.md) (per-unit ledger + notes for the next unit). Spike results live in §11 below.

## Definition of done (macOS)

1. A **Tier B** plugin built on a *different machine* against the published SDK zip + Qt 6.10.3 — never cloning QGC — loads into a QGC built at a *different commit*, and all its contributions work.
2. A **Tier A** package (manifest + QML, no binary) installs at runtime from the settings page and its panels/menu appear without an app rebuild.
3. A self-signed **hardened-runtime QGC.app** (simulating the notarized release) loads a Developer-ID-signed or ad-hoc-signed plugin — proving the distribution signing story, not just the dev loop.
4. Disabled or incompatible plugins **never execute code**; the Plugins settings page shows identity, version, and the reason a plugin isn't running.
5. `plugins/example` is pure Tier B; **QDrive still works** as declared Tier C (`hostBuildId`-gated) with a live burn-down list toward Tier B.
6. No `-Wl,-export_dynamic` in the upstream-intended configuration (fork keeps it behind an option for Tier C).

---

## 1. macOS platform mechanics that shape the design

These are the OS facts the units below are built around. Each is verified by a spike (§3) rather than trusted from documentation.

### 1.1 dyld, two-level namespace, and `@rpath`

- macOS binds symbols two-level (symbol + expected image). Today's plugins are linked `-undefined dynamic_lookup`, which flat-binds QGC symbols and resolves them from the exe at load — the coupling the SDK removes.
- The SDK library ships as a plain dylib with install name `@rpath/libQGCPluginAPI.dylib`. A plugin linking it records that reference; at `dlopen` time dyld resolves `@rpath` using the rpath stack of the *loading process*, which includes the executable's `LC_RPATH` entries. The bundled app already carries `@executable_path/../Frameworks` (Qt deploy sets it); dev builds get CMake build-rpaths automatically. So a plugin in `~/Library/Application Support/.../plugins/` finds the host's SDK dylib with **zero** plugin-side configuration.
- **Rule: the SDK dylib is host-provided.** A plugin package must never bundle its own copy of `libQGCPluginAPI.dylib` (or Qt) — two copies means split static state and ODR violations, the same reason you never ship a second QtCore. The SDK zip contains the dylib **for linking only**.

### 1.2 One Qt per process

Plugins link Qt by `@rpath`/framework reference and resolve to the **host's bundled Qt** at load. Qt guarantees forward binary compatibility within a major series: a plugin built against Qt 6.10.3 loads into hosts with Qt ≥ 6.10.3 (Qt 6.x). The SDK package therefore **pins the Qt kit version** (currently 6.10.3 from `.github/build-config.json`) and the manifest's `hostVersion.min` implies it. Plugin authors install that exact kit from the Qt installer.

### 1.3 Hardened runtime and library validation ← *the distribution gate*

- Release QGC is signed with `--options=runtime` (hardened runtime) by [SignMacBundle.cmake](../../cmake/install/SignMacBundle.cmake). Hardened runtime enables **library validation**: dyld refuses to load any dylib not signed by Apple or by the *same Team ID* as the app. Third-party plugins are by definition differently-signed → **blocked in today's signed builds**.
- The fix is the `com.apple.security.cs.disable-library-validation` entitlement on the main executable's signature. Notarization accepts it (it's a documented entitlement; OBS and other plugin hosts ship it). This is Stage 3 / U3.5.
- **Defect found during grounding:** `deploy/macos/qgroundcontrol.entitlements` already contains `disable-library-validation` — but `SignMacBundle.cmake` never passes `--entitlements`, so release signatures carry **no entitlements at all**. The file is only wired to the Xcode generator (`CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS` in [Apple.cmake](../../cmake/platform/Apple.cmake)), which CI doesn't use.
- **Second defect:** that same file sets `com.apple.security.app-sandbox: true`. If it were ever actually applied, QGC would launch sandboxed — breaking serial ports, the plugins directory, and most of the app. The plan introduces a **separate, minimal** entitlements file for the release signing path and leaves the sandbox question out of scope.
- Local unsigned dev builds have no hardened runtime → none of this affects the dev loop.

### 1.4 Gatekeeper and quarantine

- On Apple Silicon **all** code must be signed at least ad-hoc; the linker ad-hoc-signs automatically, so locally built plugins just work.
- A dylib downloaded via browser carries `com.apple.quarantine`. Loading a quarantined, un-notarized dylib into a notarized app is blocked by Gatekeeper on current macOS.
- **Design consequence:** the `.qgcplugin` installer (Stage 3) unzips **in-process**. Files written by QGC itself are not quarantined (QGC doesn't opt into `LSFileQuarantineEnabled`), so the extracted dylib loads cleanly — the same reason VS Code `.vsix` installs never fight Gatekeeper. Manually-unzipped packages may still carry the xattr; the loader detects it and offers a consent-gated strip (`removexattr`) instead of failing cryptically.
- For distribution *outside* the installer path, plugin authors sign with Developer ID and (optionally) notarize their dylib; document, don't enforce.

### 1.5 Architectures

CI builds `x86_64h;arm64` (from `cmake/presets/macOS.json`). The SDK dylib artifact is built universal. Plugins should be built universal (`x86_64;arm64` is fine — the `h` sub-type is compatible); a single-arch plugin loads on that arch only and the loader reports the mismatch (Qt's metadata reader already fails cleanly on a missing arch slice — S1 verifies the error is legible).

### 1.6 Load order inside the app

`QGCPluginManager::instance()->init()` runs in `_initForNormalAppBoot()` **before** the QML engine exists ([QGCApplication.cc:307](../../src/QGCApplication.cc#L307)). Two consequences baked into the design:

- Contributions must be ready before QML loads — satisfied, since manifest parsing happens right there.
- **First-run consent cannot be a modal dialog at load time.** Consent is a state (`NeedsApproval`) surfaced in the Plugins settings page; newly discovered user-dir plugins simply don't run until approved (§6 U3.3). No UI re-architecture needed.

---

## 2. Locked design decisions

Numbered so units can reference them. Each traces to grounded evidence.

**D1 — Contributions become manifest data; code is only for services.**
`QGCPlugin` today has 15 virtuals, 12 of which return *static declarations* (panel URLs, dock URLs, widths, heights, positions, tool-menu map, telemetry-logging claim — see [QGCPlugin.h](../../src/PluginSystem/QGCPlugin.h)). Every one of those is a vtable landmine (01 §3.4) for zero benefit: both existing plugins return constants. All of them move into the manifest's `contributes` object; the manager synthesizes the exact same `QVariantMap`s QML already consumes (keys `name/panelUrl/dockUrl/defaultWidth/defaultHeight/defaultXFraction/defaultYFraction`, and `title/icon/source` for the tool menu), so **no QML consumer changes**. `QGCPlugin` shrinks to `init(host)/cleanup()/replayExtension()`. This is what makes Tier A (no binary at all) fall out for free in Stage 3, and it eliminates the class of ABI breaks that falsified `pluginInterfaceVersion()` three times on this branch.

**D2 — SDK/host split: `src/PluginAPI/` vs `src/PluginSystem/`.**
`src/PluginAPI/` = the `QGCPluginAPI` shared-library target: interfaces only, links Qt only, no QGC includes. `src/PluginSystem/` keeps loader, manager, settings glue, and the host-side service implementations; it links `QGCPluginAPI`. Dependency arrows: app → PluginAPI ← plugins. Nothing in PluginAPI may include anything from `src/` outside itself — enforced by its own include path, not by review.

**D3 — SDK ships as a plain `@rpath` dylib, host-provided (§1.1), versioned `SOVERSION 2`,** built with `CXX_VISIBILITY_PRESET hidden` + an explicit `QGCPLUGINAPI_EXPORT` macro (`Q_DECL_EXPORT`/`IMPORT`). The macro discipline costs nothing on macOS and makes the eventual Windows build (declspec) a no-op.

**D4 — Service set v1 is exactly QDrive's burn-down, nothing more.**
Grounded grep of `plugins/qdrive/src` (usage counts in §7): `qgc.replay/1`, `qgc.telemetryLogging/1`, `qgc.vehicles/1`, `qgc.missions/1`, `qgc.app/1`. Five interfaces, each pure-virtual, id-versioned ATAK-style, acquired via `QGCHostServices::service(id)`. No speculative services; a new plugin need = a new unit adding one seam.

**D5 — Settings key becomes manifest `id` (reverse-DNS); no migration shim.**
Pre-release, two users. Old name-keyed Facts are abandoned; plugins re-default to enabled/disabled per the consent rule. Deliberately no compat code.

**D6 — IID unification now, bump at SDK cutover.**
Stage 1 fixes the mismatch (loader declares `org.qgroundcontrol.QGCPluginInterface/1.0`, plugins stamp `org.mavlink.qgroundcontrol.QGCPluginInterface` — 01 §4.1) by making every plugin use the header constant, and the loader starts *checking* the metadata IID before instantiating. Stage 2 bumps the constant to `/2.0` when `init(host)` lands; in-tree plugins rebuild in lockstep.

**D7 — `-Wl,-export_dynamic` moves behind `option(QGC_ENABLE_INTERNAL_PLUGINS)` — default ON for the fork, OFF upstream.**
It exists only to serve Tier C. Gating it (rather than deleting) keeps QDrive working while making the upstream-facing configuration clean (03's "keep it out of the PR series").

**D8 — Packages install per-id: `<plugins-dir>/<manifest.id>/` with `qgcplugin.json` at the root.**
The loader treats *directories containing a manifest* as packages and *bare dylibs* as the dev-loop path (both remain supported). In-process unzip via vendored **miniz** (tiny, no Qt private API, already-familiar C) for the quarantine reason in §1.4.

**D9 — Release signing gains a minimal plugin entitlements file.**
New `deploy/macos/qgroundcontrol-release.entitlements` containing only `com.apple.security.cs.disable-library-validation`; `SignMacBundle.cmake` passes it via `--entitlements` on the **final app-bundle codesign only** (entitlements live on the main executable's signature; dylib signatures don't carry them). The existing sandbox-bearing file is left for the Xcode path but flagged; do not merge the two.

**D10 — Trust default: bundle-shipped plugins are trusted; user-dir plugins start `NeedsApproval`.**
Replaces the "Example disabled by default" special case with a rule (02 §7.1). Plus a crash sentinel: persist `PluginSystem/loadingPluginId` before `instance()`, clear after the load loop; if present at next startup, quarantine that id and say so.

**D11 — Utility classes are vendored by plugins, never SDK-exported.**
`QmlObjectListModel` (16 QDrive uses), `QGCFormat` (2) are implementation utilities; exporting them would freeze QGC internals by the back door (02 §5 "what does not go in the SDK"). QDrive copies them into its own tree during migration. MAVLink headers are a pinned third-party dependency in the SDK package, not a QGC export.

---

## 3. Spikes (before Stage 2 commitments)

Each spike is throwaway code on a branch; the deliverable is a **fact**, recorded at the bottom of this file when run.

| # | Question | Method | Exit criterion | If it fails |
|---|---|---|---|---|
| S1 | Does `QPluginLoader::metaData()` return manifest JSON on macOS **without executing plugin code**, including from a universal (fat) dylib and a `configure_file`-generated JSON? | Add `Q_PLUGIN_METADATA(... FILE "qgcplugin.json")` to ExamplePlugin; print `metaData()` before `instance()`; check with a constructor-side `qFatal` guard that no code ran; test both arch slices. | Manifest fields print pre-load on arm64 + x86_64 slices. | Qt's Mach-O metadata reader has a gap → fall back to sidecar JSON file next to the dylib (loader reads file first); design already tolerates this (packages use sidecar anyway). |
| S2m | Does a MODULE linking only a trial `QGCPluginAPI.dylib` + Qt load from the **user plugins dir** with `@rpath` resolving to the host's copy — no `export_dynamic`, no `dynamic_lookup`? | Hand-build a 2-file SDK dylib + tiny plugin; `otool -L` the plugin; load into a dev QGC from `~/Library/Application Support/...`. | Plugin loads; `otool -L` shows `@rpath/libQGCPluginAPI.dylib`; removing `export_dynamic` from the exe doesn't break it. | rpath stack doesn't cover the case → add an explicit `LC_RPATH` on the exe or have the loader `dlopen` the SDK first; record which. |
| S3 | **The Tier B honesty test.** Build plugin at commit A; run against host at commit B with deliberate internal churn (add a data member to `Vehicle`, add a virtual to an internal class); exercise every extension point. | Two builds + churn patch; manual run-through of panels/menu/replay. | Zero crashes, all contributions live. | The failure names the violated ABI rule (inline member access, non-d-pointer class, vtable change) → fix the rule in the SDK header, re-spike. |
| S4 | Does a **directory** with only `qgcplugin.json` + QML behave as a full plugin (panel + tool menu) using `file://` URLs and the `QGroundControl` QML singleton? | Hand-write a package dir; hack the manager to synthesize contributions from it. | Panel renders, singleton calls work. | QML sandboxing/relative-import issue → adjust URL resolution (set `QQmlEngine` import path per package); record the rule. |
| S6m | **Signed-app loading matrix** — what actually loads under each signing state on current macOS? | Self-sign a local bundle (`codesign -s <self-signed-cert> --options=runtime [--entitlements …]`); try {ad-hoc plugin, self-signed plugin, quarantined plugin} × {with, without disable-library-validation}. | A filled-in 6-cell matrix documented in §11. | N/A — whatever the matrix says *is* the distribution story; D9/U3.5 wording adjusts to it. |

S1, S2m, S6m are half-day each. S4 is a day. S3 is 1–2 days and runs once the Stage 2 skeleton exists (it needs a real SDK header set to test).

---

## 4. Stage 1 — The declared contract (manifest + loader gate)

Goal: identity/compatibility become **data read before code runs**; linkage untouched. Ships alone; maps to upstream PR 1. Six units, one commit each.

### U1.1 — `PluginManifest` type + validation
- **New:** `src/PluginSystem/PluginManifest.h/.cc` — a value type: `id, name, version (QVersionNumber), vendor, description, tier (enum Qml|Sdk|Internal), apiVersion (int), hostMin/hostMax (QVersionNumber), hostBuildId (QString), contributes (QJsonObject, opaque here)`. Static `fromJson(const QJsonObject&, QString* errorOut)` and `validateForHost(const HostInfo&, QString* reasonOut)` where `HostInfo{version, apiVersion, buildId}`.
- Host version derives from `QGC_APP_VERSION_STR` (git-describe, e.g. `v5.0.7-…`): strip `v`, `QVersionNumber::fromString` up to the suffix. Host build id = `QGC_GIT_HASH` (short hash from `cmake/modules/Git.cmake`), added as a compile definition for the PluginSystem sources.
- Rules encoded: unknown `tier` → invalid; `tier==internal` requires `hostBuildId` == host's; `hostVersion` range half-open `[min, max)`, empty max = unbounded; `apiVersion` must equal the host's supported major (a constant, `1` until Stage 2 bumps it).
- **Manifest schema v1** (documented in the header and `plugins/README.md`):

```json
{
    "id": "org.example.qgc.example",
    "name": "Example",
    "version": "1.0.0",
    "vendor": "Example Org",
    "description": "Demonstrates the QGC plugin system",
    "tier": "internal",
    "apiVersion": 1,
    "hostVersion": { "min": "5.0", "max": "" },
    "hostBuildId": "@QGC_GIT_HASH@",
    "contributes": { }
}
```

- **Tests:** `test/PluginSystem/PluginManifestTest` (`add_qgc_test(... LABELS Unit)`): malformed JSON, missing required fields, bad tier, version-range edges (min==host, host==max rejected, empty max), internal-tier hash match/mismatch, apiVersion mismatch. Pure logic — no libraries loaded.

### U1.2 — Loader gate: metadata before code
- [QGCPluginLoader.cc](../../src/PluginSystem/QGCPluginLoader.cc): `_loadPlugin()` becomes two-phase. Phase 1 `inspect(filePath)`: `QPluginLoader::metaData()` → check `IID == QGCPluginInterface_iid` → parse `MetaData` into `PluginManifest` → `validateForHost`. Phase 2 `activate()`: only for validated+enabled plugins — `instance()`, `qobject_cast`, `createPlugin()` (the runtime `pluginInterfaceVersion()` check stays as a belt-and-braces assert but the metadata is now authoritative).
- `PluginLoadInfo` grows: `manifest`, `state (Discovered|Incompatible|Disabled|Active|Failed|Quarantined)`, `errorString`. Loader stops deciding policy — it inspects and activates on request; the **manager** owns the enable/consent decision (U1.3).
- Fix the IID at both producers: `plugins/example/ExamplePlugin.h` and `plugins/qdrive` change `Q_PLUGIN_METADATA(IID "org.mavlink...")` → `Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")` (D6).
- Both plugins gain `qgcplugin.json.in` (tier `internal`, `hostBuildId` `@QGC_GIT_HASH@`), configured into the build dir; the generated dir joins the target include path so moc finds the `FILE`.
- **Tests:** extend `PluginManifestTest` with metadata-envelope parsing (IID mismatch, missing MetaData). Real-library load test arrives with the Stage 2 fixture plugin; until then, manual verify = build + launch, log shows "validated Example (internal, build abc1234) before load".

### U1.3 — Manager: id-keyed records, no execution of disabled plugins
- [QGCPluginManager](../../src/PluginSystem/QGCPluginManager.cc) replaces `QList<PluginInfo>` with `QList<PluginRecord>` — one per *discovered* plugin (any state), holding manifest + state + reason + `QGCPlugin*` when active. `_loadPlugins()` becomes: inspect all → register each **id** with `PluginSettings` → skip disabled without ever calling `instance()` (kills 01 §3.5) → activate the rest → `init()`.
- [PluginSettings](../../src/Settings/PluginSettings.h) keys Facts by manifest id (D5). Display name comes from the record, not the Fact key.
- `unloadPlugin`/`reloadPlugin` (QML-invokable) become `setPluginEnabled(id, bool)` + `reloadPlugin(id)`; reload uses the stored path only — the scan-everything fallback is deleted (01 §4.5). "Unload" semantics = deactivate instance + drop contributions; the library mapping stays (01 §4.6's honest framing), full drop on restart.
- **Tests:** manager-level unit test with fixture manifests (no libraries): disabled → never activated; incompatible → state+reason recorded; id used as settings key.

### U1.4 — Settings page shows the contract
- [src/UI/AppSettings/PluginSettings.qml](../../src/UI/AppSettings/PluginSettings.qml) consumes a richer model from `QGCPluginManager::knownPlugins()` (`QVariantList` of id/name/version/vendor/description/state/reason): per-plugin row = name + version + vendor, toggle, and a status line ("Active", "Disabled", "Incompatible: needs QGC ≥ 5.1", "Failed to load: <dlopen error>").
- **Verify:** screenshot loop (build + deploy, drop in `.screenshots/`).

### U1.5 — Loader hygiene + docs truth
- Warn on second replay-extension registration (first still wins) (01 §4.4).
- Rewrite [plugins/README.md](../../plugins/README.md) and [src/PluginSystem/README.md](../../src/PluginSystem/README.md): manifest requirement, real linkage model (no fictitious `target_link_libraries(... QGroundControl)`), actual init-order semantics, tier table as roadmap (01 §4.3).
- **Tests:** none (docs + one `qCWarning`).

### U1.6 — `qgc_add_plugin()` becomes the single path
- Rewrite [PluginHelpers.cmake](../../cmake/modules/PluginHelpers.cmake): absorb everything both plugins hand-roll — MODULE, AUTOMOC/AUTORCC, C++20, output dir, **manifest plumbing** (`configure_file` of `MANIFEST` arg → binary dir → include path), platform suffix, undefined-symbol options for the internal tier (moved out of example's CMakeLists), auto-deploy dir **computed from `QGC_ORG_NAME`/`QGC_APP_NAME`** (kills the hardcoded "QGroundControl Daily", 01 §4.3). Signature:

```cmake
qgc_add_plugin(ExamplePlugin
    TIER INTERNAL                    # QML | SDK | INTERNAL (SDK arrives in Stage 2)
    MANIFEST qgcplugin.json.in
    SOURCES ExamplePlugin.h ExamplePlugin.cc
    QRC_FILES ExamplePlugin.qrc
)
```

- `plugins/example/CMakeLists.txt` and `plugins/qdrive`'s plugin target collapse to calls of it.
- **Verify:** clean build, both plugins produced + deployed; `plugins/example/build.sh` still works or is deleted in favor of the in-tree build.

---

## 5. Stage 2 — The SDK boundary (`QGCPluginAPI`)

Goal: Tier B exists and is proven cross-commit on macOS; `export_dynamic` gated off; SDK publishable. Maps to upstream PR 2. **Run S2m before U2.1, S3 after U2.4.**

### U2.1 — Library skeleton + type moves
- **New target** `QGCPluginAPI` in `src/PluginAPI/` (D2, D3): `qgc_plugin_api_global.h` (export macro), `QGCPluginInterface.h` (IID bumped `org.qgroundcontrol.QGCPluginAPI/2.0`, `apiVersion` 2 — D6), `QGCPlugin.h/.cc` (d-pointer, only `init(QGCHostServices*)`, `cleanup()`, `replayExtension()` — D1 strips the rest in U2.2), `QGCHostServices.h/.cc` (abstract `QObject* service(const QString& id)`), `QGCReplayExtension.h/.cc` (moved verbatim; header comment freezes it — additions go to a `…/2` interface, never this vtable).
- CMake: `qt_add_library(QGCPluginAPI SHARED)`, hidden visibility, `VERSION 2.0 SOVERSION 2`, install name `@rpath/…`; app target links it; macOS bundle install puts it in `Contents/Frameworks/` (the existing dylib-signing glob in SignMacBundle already covers a plain dylib there — verified against the `find` filter).
- `src/PluginSystem/` sources include from `src/PluginAPI/` and drop their own copies. In-tree plugins keep compiling (they see the same headers through the internal-tier include set).
- **Tests:** build-level (app links, unit suite still green). `apiVersion` host constant becomes `2`; manifest tests updated.

### U2.2 — Contributions move to the manifest (D1)
- Delete the 12 static-declaration virtuals from `QGCPlugin`. Define the `contributes` schema and its reader `PluginContributions::fromManifest(manifest, urlResolver)` in `src/PluginSystem/`:

```json
"contributes": {
    "toolMenu":      { "title": "Example", "icon": "qrc:/images/example.svg", "source": "qrc:/qml/ExamplePluginView.qml" },
    "flyViewPanel":  { "panel": "qrc:/qml/ExampleFlyViewPanel.qml", "dock": "qrc:/qml/ExampleFlyViewDockItem.qml",
                       "defaultWidth": 35, "defaultHeight": 18, "defaultPosition": [0.0, 0.0] },
    "planViewPanel": { "panel": "qrc:/qml/ExamplePlanViewPanel.qml", "defaultWidth": 35, "defaultHeight": 14 },
    "replay": false,
    "telemetryLogging": false
}
```

- URL rule: `qrc:/…` = compiled-in resource (Tier B/C); relative path = package-relative (Tier A/packages, resolved in Stage 3). The manager synthesizes the **identical** `QVariantMap`s it builds today (same keys), still filtered by enabled state, same change signals — FlyView/PlanView/toolbar QML untouched.
- `MAVLinkProtocol`'s check moves from `plugin->controlsTelemetryLogging()` to the manager's manifest-derived `hasLoggingController()` (already the consumed surface).
- Example + QDrive manifests gain their real `contributes` blocks; their C++ overrides are deleted.
- **Tests:** `PluginContributionsTest` — synthesis from manifest fixtures produces the exact legacy map shapes (this pins the QML contract); enabled-state filtering; `replay`/`telemetryLogging` flags.

### U2.3 — Host services: `qgc.replay/1`, `qgc.telemetryLogging/1`
- **SDK:** `QGCReplayService.h` (create/connect a replay link for a tlog path → returns the link as `QObject*`, disconnect, `registerReplayParamFile(path)`, plan-registry hook — wrapping what `FlightReplayController` + `TelemetryParamsController` use today: `LinkManager` ×2, `ParameterManager::registerReplayParamFile` ×3, `LogReplayLink`), `QGCTelemetryLoggingService.h` (start/stop/save/discard pending tlog + `tlogLoggingChanged` — the `MAVLinkProtocol` surface, 11 call sites).
- **Host:** `src/PluginSystem/HostServices/` — `QGCHostServicesImpl` (id → QObject registry) + one wrapper per service delegating to the internal singletons. Manager constructs it and passes to `QGCPlugin::init(host)`.
- Each interface: pure-virtual `Q_OBJECT` class, `.cc` anchor in the SDK (metaobject lives in the dylib so cross-boundary `qobject_cast` works), id constant `"qgc.replay/1"` in the header.
- **Tests:** `HostServicesTest` — registry lookup, unknown id → nullptr; wrapper smoke tests where the wrapped singleton allows it headlessly.

### U2.4 — Host services: `qgc.vehicles/1`, `qgc.missions/1`, `qgc.app/1`
- `QGCVehicleService`: `activeVehicle()` as `QObject*` + change signal + vehicle list (covers the 16 `MultiVehicleManager::instance()` sites; Vehicle/Fact interaction from QML stays on the existing QML API — the service only hands over the object).
- `QGCMissionService`: the tlog-mission surface `TelemetryMissionController` uses (`snapshotVehicleIds`, `loggedVehicleIds`, `savePendingLogWithMission`, `discardPendingLogWithMission`, `allMissionsReady` + signals). Plan-preview needs (`PlanMasterController` et al.) are **deferred to the QDrive migration unit** that touches `MissionPreviewController` — decide there whether QML-side creation suffices (`PlanMasterController` is QML-creatable) or the service grows a seam. Named per D4: no seam before its consumer.
- `QGCAppService`: app/org name, version string, key storage paths (the `qgcApp()`/`AppSettings` usage).
- **After this unit, run S3** (cross-build survival) against this header set.
- **Tests:** as U2.3, per service.

### U2.5 — Example becomes pure Tier B
- `plugins/example/CMakeLists.txt`: `TIER SDK` — links `QGCPluginAPI` + Qt only; include dirs restricted to the SDK dir (the build itself is the include-boundary enforcement — no `src/` on the path means violations fail to compile); `-undefined dynamic_lookup` gone for this target; manifest `tier: "sdk"`, `apiVersion: 2`, `hostBuildId: null`.
- Example's code adjusts to `init(QGCHostServices*)` (it needs no services yet — stores the pointer, demonstrates a lookup in comments).
- **New test fixture:** `test/PluginSystem/TestPlugin/` — a minimal SDK-tier MODULE built by the test tree; `PluginLoaderGateTest` loads it for real through `QGCPluginLoader` (offscreen): metadata-gate pass, tier rules, activation, contribution synthesis end-to-end. This is the permanent regression harness for the loader.
- **Verify:** run app; example panels/menu work identically. `otool -L` shows only Qt + `@rpath/libQGCPluginAPI.dylib`.

### U2.6 — `export_dynamic` gated; Tier C formalized (D7)
- Root [CMakeLists.txt:301-308](../../CMakeLists.txt#L301-L308): wrap in `option(QGC_ENABLE_INTERNAL_PLUGINS "Support internals-native (Tier C) plugins" ON)` — fork default ON (QDrive), upstream story OFF. `qgc_add_plugin(TIER INTERNAL)` errors helpfully when the option is off.
- QDrive manifest stays `tier: "internal"`, now truly gated by `hostBuildId` at load (mismatched build → clean "built for another QGC build" in settings instead of undefined behavior).
- **Verify:** `-DQGC_ENABLE_INTERNAL_PLUGINS=OFF` builds and runs with example (SDK tier) only; ON restores QDrive.

### U2.7 — SDK packaging (the "never build QGC" artifact)
- Install component `QGCPluginSDK`: SDK headers (`include/QGCPluginAPI/…`), the universal `libQGCPluginAPI.dylib`, `QGCPluginAPIConfig.cmake` + version file (authors use `find_package(QGCPluginAPI)`), a plugin-project template (CMakeLists + manifest.in + stub class), and `SDK-README.md` stating the **compatibility contract verbatim** (02 §5.3: same SDK major, host in declared range, macOS, Qt 6 ≥ 6.10.3 pinned kit, libc++) and the ABI rules (d-pointers, no new virtuals on shipped classes, append via new service ids).
- CI: `macos.yml` job step packs `qgc-plugin-sdk-macos-<version>.zip` (`cmake --install --component QGCPluginSDK` + `ditto -c -k`).
- **Verify (definition-of-done #1 rehearsal):** on a second machine (or clean checkout dir), build the template plugin against the zip + stock Qt 6.10.3, drop the dylib into the user plugins dir, watch it load into a QGC built from a different commit.

---

## 6. Stage 3 — Packages, install UX, trust (macOS distribution)

Goal: `.qgcplugin` install/remove from the settings page; Tier A live; consent + crash quarantine; release-signing fixed. Maps to upstream PRs 7 + the macOS half of 8's groundwork.

### U3.1 — Package discovery + Tier A synthesis
- Loader learns **package dirs** (D8): for each search root, any child directory with `qgcplugin.json` is a package. Tier A (no `bin/`): skip `QPluginLoader` entirely — record + contributions come from manifest alone (the machinery U2.2 already built), QML/asset URLs resolved `file://<pkg>/…`. Tier B package: binary at `bin/macos-universal/<name>.dylib` (key documented; loader falls back to any single dylib under `bin/macos-*`).
- Declare the **QML API level** (02 §5.4): an integer on the host (`1`), `"qmlApiVersion"` optional in manifests, checked for Tier A.
- **Tests:** loader-gate test grows package fixtures: Tier A dir (no binary → active, contributions synthesized), Tier B package dir (binary loaded), missing-arch/bad-layout errors legible. **Verify:** S4's package promoted into `test/PluginSystem/fixtures/`.

### U3.2 — Install / remove UX
- Vendor **miniz** (`libs/miniz/`, two files). New `src/PluginSystem/PluginInstaller.h/.cc`: `installFromFile(zipPath)` → read manifest from zip root → validate (schema + tier + collision on id) → extract to `<user-plugins>/<id>/` (in-process ⇒ no quarantine, §1.4) → rescan that id; `removePlugin(id)` → deactivate + delete dir.
- On macOS, pre-load quarantine check on package binaries: if `com.apple.quarantine` present (manually-installed package), state = `NeedsApproval` with reason "downloaded plugin — approve to run"; approval strips the xattr then activates.
- Settings page: "Install plugin…" (file dialog for `.qgcplugin`), per-plugin "Remove", details pane (version/vendor/tier/status).
- **Tests:** `PluginInstallerTest` — good zip installs, manifest-less zip rejected, id collision prompts replace, remove cleans. **Verify:** screenshot loop for the page.

### U3.3 — Consent model (D10)
- Manager rule: source dir ⇒ trust class. Bundle dirs (`Contents/PlugIns`, exe-adjacent `plugins/`) = trusted, default-enabled. User dir = `NeedsApproval` on first sight (id+version+file hash remembered in settings); the settings page shows "New — not yet enabled [Enable]". Removes the Example-disabled-by-default special case.
- **Tests:** manager test — new user-dir plugin never activates pre-approval; approval persists across restarts; changed hash re-prompts.

### U3.4 — Crash sentinel (VS-Code-bisect-lite)
- Before each `activate()`: write `PluginSystem/loadingPluginId=<id>` (QSettings, synced); clear after the whole load loop. On startup, a lingering id ⇒ mark `Quarantined` (skip activation) + settings-page banner "QGC crashed while loading <name> last run — re-enable to retry".
- **Tests:** unit test simulates the lingering key → record state Quarantined, not activated; re-enable clears.

### U3.5 — Release signing carries the plugin entitlement (D9)
- Add `deploy/macos/qgroundcontrol-release.entitlements` (only `disable-library-validation`); `SignMacBundle.cmake` final app codesign gains `--entitlements`. Comment in the script states why (plugin loading under hardened runtime) and points at S6m's matrix.
- Docs: `plugins/README.md` gains the **plugin author signing guide**: ad-hoc = local dev; Developer ID sign (+ notarize if distributing the dylib outside `.qgcplugin`) = distribution; universal-build advice (§1.5).
- **Verify:** re-run the S6m matrix against a self-signed hardened-runtime bundle of the real app: differently-signed plugin loads *with* the entitlement, is blocked *without* — proving definition-of-done #3.

---

## 7. Stage 4 — QDrive migration (fork-only, parallel after Stage 2)

QDrive stays Tier C and shippable throughout; each unit burns down one internal dependency onto a seam. The baseline (grounded grep of `plugins/qdrive/src`):

| Internal dependency | Sites | Seam | Unit |
|---|---|---|---|
| `PluginSystem/QGCPlugin.h`, `QGCPluginInterface.h`, `QGCReplayExtension.h` | — | SDK headers (same names) | Q1: mechanical include swap + `init(host)` adoption + manifest `contributes` (panels/toolMenu/telemetryLogging flags) |
| `MultiVehicleManager` (16), `Vehicle` | replay + upload controllers | `qgc.vehicles/1` | Q2 |
| `MAVLinkProtocol` (11) | telemetry logging takeover | `qgc.telemetryLogging/1` | Q2 |
| `LinkManager` (2), `ParameterManager::registerReplayParamFile` (3), `LogReplayLink` | `FlightReplayController`, `TelemetryParamsController` | `qgc.replay/1` | Q3 |
| `MissionController` tlog-mission methods | `TelemetryMissionController` | `qgc.missions/1` | Q3 |
| `PlanMasterController`/`MissionManager`/`GeoFence`/`RallyPoint` | `MissionPreviewController` | decide: QML-side `PlanMasterController` or new seam (D4) | Q4 |
| `QGCApplication`/`SettingsManager`/`AppSettings` | paths/version | `qgc.app/1` | Q4 |
| `QmlObjectListModel` (16), `QGCFormat` (2) | models everywhere | **vendor** into qdrive (D11) | Q5 |
| `QGCLoggingCategory` (24) | logging | plain `QLoggingCategory` (mechanical) | Q5 |
| `MAVLinkLib.h` | tlog parsing | pinned mavlink headers from SDK package | Q5 |

When the table is empty: flip manifest to `tier: "sdk"`, `apiVersion: 2` — QDrive becomes distributable under the Tier B contract (the original goal). Track progress by re-running the grep from 01 §1 (recorded in `plugins/qdrive/docs/` when Q1 starts; QDrive units follow its own repo's workflow).

---

## 8. Stage 5 — Keeping it honest (CI + docs)

- **Golden-plugin cross-build test:** when SDK 2.0 ships (end of Stage 2), archive the built example plugin as a versioned artifact; a macOS CI job downloads it and runs `PluginLoaderGateTest --golden <path>` against today's host — spike S3 automated forever. Any red = an ABI rule was broken; the fix is reverting the break, not rebuilding the golden.
- **SDK docs:** doxygen group for `src/PluginAPI/` headers; "Writing your first plugin (macOS)" tutorial replacing template-copy instructions (uses the SDK zip + template from U2.7).
- **Defect ledger check** (from 01 §4, all should be closed by now): IID mismatch → U1.2 · export_dynamic → U2.6 · docs drift → U1.5 · replay double-slot → U1.5 · reload-scans-everything → U1.3 · unload pretense → U1.3 · security posture → U3.3/U3.4/U3.5 · helper drift → U1.6.

---

## 9. Risks (macOS-specific, beyond 03's table)

| Risk | Mitigation |
|---|---|
| Gatekeeper/library-validation behavior shifts across macOS releases (docs lag reality) | S6m measures empirically; U3.5 re-runs it; the matrix lives in this file as the record |
| `QPluginLoader::metaData()` quirk on fat binaries or huge manifests | S1 first; sidecar-JSON fallback is designed-in (packages already use a plain file) |
| Qt kit skew between plugin author and host (6.10.x vs 6.11) | SDK zip pins the kit; manifest `hostVersion.min` implies it; Qt forward-BC covers plugin-older-than-host; document "host older than plugin's Qt = unsupported" |
| Two SDK dylib copies loaded (author bundles it) | Rule in SDK-README + installer rejects packages containing `libQGCPluginAPI.dylib`/Qt frameworks (U3.2 validation) |
| `x86_64h` slice confusion for Intel plugin authors | SDK-README: build `x86_64;arm64`; loader error message names the missing arch (U3.1 test) |
| Debug-vs-Release plugin/host mixing (Tier C especially) | Accepted looseness: hash gate is per-commit, not per-config; noted in README; libc++ makes mixed configs mostly-work on macOS |
| Contributions-in-manifest drift from what the plugin's code expects (stale manifest) | Manifest is the *only* source (no code path to drift against) — D1's point; contributions tests pin the map shapes QML consumes |

---

## 10. Deferred: what Windows and Android will need (and what's pre-paid)

**Windows (later):** MSVC build of `QGCPluginAPI` (+`.lib` import library — the `QGCPLUGINAPI_EXPORT` macro from D3 already does declspec), package key `bin/windows-x64/*.dll`, loader-side PE metadata read (Qt handles it), signing story n/a (SmartScreen guidance only). **Nothing in Stages 1–3 assumes macOS-only:** manifest, gate, tiers, services, package layout, consent are platform-neutral; only U3.5 and the §1 mechanics are macOS-specific. Explicitly *not* doing `ENABLE_EXPORTS` on the exe (03's tactical interim) — Tier C stays macOS/Linux-only, which is fine because QDrive development happens on macOS.

**Android (later):** Tier A packages already work by construction (data, not code — U3.1's synthesis path is what the Android v1 story runs on); plugin-APK (ATAK model) stays its own future effort behind spike S5 (03 Phase 4).

**Platform forward-look, recorded by the whole-stage ABI review (2026-07-12) so the ports don't rediscover them** (U2.7's SDK-README.md carries the plugin-author-facing versions of these):

- **Windows has no soname mechanism.** `qt_add_library` produces `QGCPluginAPI.dll` regardless of `VERSION`/`SOVERSION` — the macOS/Linux "frozen plugin binds to a versioned filename" story has no DLL equivalent. Decide at port time: encode the major in the filename (`QGCPluginAPI2.dll`) or rely solely on the manifest's `apiVersion` gate. Either works because the gate rejects before any plugin code runs.
- **The out-of-line `~QGCPlugin()` (F3) is load-bearing on MSVC, not just style:** a dllexported class with a `std::unique_ptr<Incomplete>` member needs its dtor out-of-line or MSVC fails to instantiate it. Already true today; keep it true. The compat contract must additionally pin the **same MSVC toolset family + CRT** (both `std` types and Qt require it) — stronger than the macOS/Linux "libc++ mostly tolerates mixed configs" looseness.
- **`pluginInterfaceVersion()`'s belt-and-braces runtime check earns its keep on Windows** — no two-level namespace means symbol-clash failure modes are messier there than on macOS. Keep the check forever, not just until Stage 2 stabilizes.
- **Android Tier B from the user plugins directory is impossible by OS policy, not by choice:** W^X enforcement (target SDK 29+) forbids `dlopen` from app-writable storage. "Android v1 = Tier A only, native plugins ship as their own APK later" is therefore the *only* possible story. When the Android port happens, `defaultPluginPaths()` should skip binary scanning entirely on Android — those directories can only ever hold Tier A packages.
- **Qt kit skew (all platforms):** the manifest deliberately doesn't record the plugin's own Qt version. Qt's plugin gate already rejects a plugin built against a newer Qt than the host with a legible error at activation (via the existing `errorString()` plumbing); forward-BC covers plugin-older-than-host. No design change needed — just document it (done in SDK-README.md).

---

## 11. Execution notes

- **Workflow entry:** Stage 1 can run as a `/feature-change` (additive gate beside unchanged linkage). Stages 2–3 are an **`/architecture-change`** (the linkage cutover deletes the old model per D1/D6/D7 — carrying both shapes past Stage 2 is failure). One unit per fresh chat; this document is the grounded blueprint both profiles start from.
- **Run settings per unit:** most units are Sonnet-fit with `/code-review` as the gate (mechanical moves, CMake, QML page work: U1.4–U1.6, U2.5, U2.7, U3.2). Use **Opus** for the ABI-sensitive design units (U2.1–U2.4 service/vtable decisions), the loader state machine (U1.2/U1.3), and trust logic (U3.3/U3.4).
- **Spike results ledger** — all five run **2026-07-06** on Intel x86_64, macOS 26.5.1, Qt 6.10.0, as throwaway harnesses (session scratchpad, deleted). Every exit criterion met; no design changes needed, three implementation notes captured below.

  - **S1 — PASS.** `QPluginLoader::metaData()` returned the full `configure_file`-generated manifest with **zero code execution**: a static-initializer tripwire and a constructor tripwire both stayed silent on `metaData()` and both fired on `instance()`. Fat (x86_64+arm64) dylib works (host slice picked). Wrong-arch-only dylib: `metaData()` returns empty `{}` — the legible "wrong architecture" message comes from `errorString()`, so **U1.2's gate must surface `errorString()` whenever metadata comes back empty**. Envelope keys: `IID`, `className`, `MetaData`, `debug`, `version`, `archlevel`.

  - **S2m — PASS.** A plugin MODULE linking only a trial SDK dylib + Qt (no `dynamic_lookup`; default two-level linking, so any unresolved symbol would have failed the build), carrying **zero LC_RPATHs of its own**, loaded from `~/Library/Application Support/…/plugins` into a bundle-geometry host: `@rpath/libTrialSdk.2.dylib` resolved via the **main executable's** `LC_RPATH @executable_path/../Frameworks` — even in a host variant that did not itself link the SDK. Negative control (SDK removed from Frameworks): clean, legible dlopen error through `QPluginLoader::errorString()`. Hidden visibility + export macro confined the SDK's export surface to exactly the API (`nm -gU`); plugin undefineds were SDK/Qt/system only. **Plugin authors need zero rpath configuration; D3's host-provided-dylib rule confirmed.**

  - **S3 — PASS, and the negative control is vivid.** A frozen plugin binary survived: d-pointer `Private` layout shifted (fields prepended), SDK impl changed, host-internal class gained a vtable + members — output showed the old binary calling the new SDK impl. Negative control: adding **one** virtual to the shipped base class → frozen plugin **SIGSEGV (exit 139) with zero output, no load error** — the silent vtable corruption 01 §3.1 predicted. Validates the append-only rule, the golden-plugin CI (Stage 5), and the crash sentinel (U3.4) as load-bearing. The real-QGC leg (plugin@commit-A vs host@commit-B) re-runs after U2.4 as planned.
    **Re-run 2026-07-11 (after U2.4, real header set) — PASS.** An out-of-tree pure Tier B plugin (SDK dylib + Qt only, `@rpath`, no `dynamic_lookup`, `sdk`-tier manifest) exercising all five v1 services was frozen; the host was then rebuilt with `Vehicle`'s layout shifted (32 bytes inserted before `_initialPlanRequestComplete`) and a virtual added to `MultiVehicleManager` — the byte-identical frozen binary loaded and every service answered identically. Details in the tracker's U2.4 entry.

  - **S4 — PASS, no import-path adjustments needed.** A package dir (manifest + QML + asset, **no binary**) consumed manifest-first: panel URL resolved package-relative to a `file://` document that loaded READY; `import QGroundControl` (a compiled-in `qt_add_qml_module`, QGC's exact registration style) resolved from the file-origin document; the C++ singleton's property + invokable evaluated correctly; a QML component from inside the module instantiated; a package-relative `Image` loaded; and the panel **rendered** offscreen (300×200 grab, background pixel exactly `#4682b4`). The spike table's fallback (per-package import paths) was not needed.

  - **S6m matrix — measured** (Intel; arm64 noted where it differs):

    | Host executable signature | Plugin state | Result |
    |---|---|---|
    | ad-hoc + hardened runtime, **no entitlement** | — | **Fails at launch**: dyld rejects the app's *own* ad-hoc SDK dylib and re-signed QtCore copy — "different Team IDs" (ad-hoc = no team, so nothing non-Apple ever matches) |
    | ad-hoc + runtime + `disable-library-validation` | ad-hoc | **Loads** |
    | same | unsigned | **Loads** (Intel leniency; arm64 requires at least ad-hoc) |
    | same | ad-hoc + quarantine xattr | **Blocked** — "library load disallowed by system policy" (Gatekeeper), clean `errorString()`, plus the user-facing "could not verify … malware" dialog |
    | unsigned host (dev loop) | quarantined | **Blocked** — quarantine gates independently of host signing |
    | entitled host | quarantine **stripped** (`removexattr`) | **Loads** |

    Consequences: **D9/U3.5 is mandatory, not belt-and-braces** — a hardened-runtime build without the entitlement can't load *any* non-same-team library, which is also why SignMacBundle's re-sign-everything approach works today and why the release entitlements change is the single gate for third-party plugins. The U3.2 in-process-unzip + consent-gated quarantine strip is required for any plugin dylib that arrived via browser, on every host signing configuration. Not measured (needs real Developer ID certs): same-team plugin under a no-entitlement host — fold into U3.5's verify step.
