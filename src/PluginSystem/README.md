# QGroundControl Plugin Architecture

This directory contains the **host side** of the plugin system: loader, manager, and
settings glue. The **plugin-facing types** (`QGCPlugin`, `QGCPluginInterface`,
`QGCReplayExtension`, `QGCHostServices`) live in [src/PluginAPI/](../PluginAPI/) — the
`QGCPluginAPI` shared library that forms the SDK boundary. Dependency arrows:
app → `QGCPluginAPI` ← plugins; nothing in `PluginAPI` includes anything from `src/`
outside itself.

## Architecture Overview

QGroundControl separates the **Core Plugin** (singleton managing core app behavior) from
**Runtime Plugins** (dynamically loaded extensions managed by `QGCPluginManager`).

### Core Components

#### `QGCCorePlugin` — Core Application Manager (Singleton)
Manages application-wide settings/options, default analyze pages, toolbar indicators,
MAVLink message routing, and custom map items/video receivers.

#### `PluginManifest` — Declared Identity/Compatibility ([PluginManifest.h](PluginManifest.h))
A pure value type parsed from a plugin's `qgcplugin.json`: `id` (reverse-DNS identity),
`name`, `version`, `vendor`, `description`, `tier` (`Qml`/`Sdk`/`Internal`), `apiVersion`,
`qmlApiVersion` (tier `Qml` only — the `QGroundControl` QML singleton tree's API level,
checked against the host's `QGCPluginQmlApiLevel` when declared), `hostVersionMin`/
`hostVersionMax` (half-open range, empty max = unbounded), `hostBuildId` (checked only
for `Internal` tier), and `contributes` (opaque here; parsed by `PluginContributions`,
see below).
`PluginManifest::fromJson()`/`fromMetaData()` and `validateForHost()` are pure functions:
no plugin code runs to produce or check a manifest.

#### `PluginContributions` — Declared Contributions ([PluginContributions.h](PluginContributions.h))
A pure value type parsed from the manifest's `contributes` object:
ready-made `QVariantMap`s for the tool menu entry and the fly/plan-view panels (the
exact shapes the QML consumers read, keyed by `pluginId`), plus the
`replay`/`telemetryLogging` flags. `fromManifest(manifest, packageDir)` is a pure
function; a malformed `contributes` block fails inspection with a legible reason. URLs
are resolved against `packageDir` (empty for non-package plugins) per the URL rule
documented in the header and in [plugins/README.md](../../plugins/README.md).

#### `QGCPluginLoader` — Stateless Inspect/Activate Mechanism ([QGCPluginLoader.h](QGCPluginLoader.h))
A static utility, not a QObject — it holds no state between calls:
- `inspect(filePath)` reads `QPluginLoader::metaData()`, checks the IID, parses the
  embedded manifest, and validates it against the host — **without instantiating the
  plugin**. Returns a `PluginLoadInfo` in state `Discovered`, `Incompatible`, or `Failed`.
- `inspectPackage(packageDir)` reads `qgcplugin.json` directly (not via `QPluginLoader`
  metadata) from a package directory (D8): a package's identity/contributions always come
  from this sidecar file, never a binary's own embedded metadata. Tier `qml` requires no
  binary; other tiers resolve one binary under `bin/<platform>-*/` (documented key
  `bin/macos-universal/` on macOS, with a same-platform fallback) or fail legibly.
- `inspectDirectories(dirs)` runs `inspect()` over every bare plugin file, and
  `inspectPackage()` over every child directory containing `qgcplugin.json`, found under
  the given directories (sorted by filename for deterministic duplicate-id resolution).
  Both forms are discovered side by side.
- `activate(info)` instantiates a plugin that passed inspection (`Discovered` →
  `Active`/`Failed`). A no-op for tier `Qml` (no binary — nothing to instantiate;
  `info.plugin` stays null).
- `defaultPluginPaths()` returns the platform's search directories.
- `hostInfo()` returns the running host's `HostInfo` (version from `QGC_APP_VERSION_STR`,
  `apiVersion` from `QGCPluginApiVersion`, `qmlApiVersion` from `QGCPluginQmlApiLevel`,
  build id from `QGC_GIT_HASH`), used to validate manifests.

Policy (which plugins are enabled, which get activated, record-keeping) is **not** the
loader's job — that's `QGCPluginManager`.

#### `PluginState` and `PluginLoadInfo`
```cpp
enum class PluginState {
    Discovered,     // Manifest read and validated; code has not run
    Incompatible,   // Manifest valid but rejected for this host (see errorString)
    Disabled,       // Valid but disabled by settings; never activated
    Active,         // Instantiated and running
    Failed,         // Metadata unreadable/invalid, or activation failed (see errorString)
    Quarantined,    // Skipped after a crash during a previous load attempt
    NeedsApproval,  // Discovered but requires explicit user consent before activation
};

struct PluginLoadInfo {
    QGCPlugin* plugin = nullptr;        // non-null only when state == Active and tier != Qml
    QString filePath;                   // binary path (bare dylib, or a package's resolved binary); the package dir itself for tier Qml
    QString packageDir;                 // non-empty for a package (dir with qgcplugin.json); empty for a bare dev-loop dylib
    PluginManifest manifest;            // valid unless state == Failed
    PluginContributions contributions;  // valid unless state == Failed
    PluginState state = PluginState::Failed;
    QString errorString;                // reason for Incompatible/Failed/Quarantined/NeedsApproval
};
```

`NeedsApproval` is set by `QGCPluginManager`'s trust gate (D10/D15, U3.3): every user-dir
plugin without a recorded consent digest — including one whose content changed since it
was approved — and, on macOS, any plugin whose manifest or resolved binary carries
`com.apple.quarantine`. Bundle-dir plugins (inside the app bundle or exe-adjacent
`plugins/`) are trusted and skip the consent check.

#### `QGCPluginManager` — Runtime Plugin Manager (Singleton) ([QGCPluginManager.h](QGCPluginManager.h))
Owns **one `PluginLoadInfo` record per discovered plugin, in any state** — not just the
active ones — plus the policy decisions: which get activated, and what each contributes
to QML (`toolMenuItems`, `flyViewPanelItems`, `planViewPanelItems`, `replayExtension`,
`hasLoggingController`). A disabled or incompatible plugin's record is populated entirely
from its manifest; its code never executes.

Key surface:
- `loadedPlugins()` — only `Active` records, minimal shape (`name`), for existing QML
  consumers.
- `knownPlugins()` — every record regardless of state, richer shape (`id`, `name`,
  `version`, `vendor`, `description`, `tier`, `state`, `statusText`, `removable`) for the
  Plugins settings page.
- `setPluginEnabled(id, bool)` — persists the setting (keyed by manifest `id`, via
  `PluginSettings`) and activates/deactivates immediately to match. Idempotent.
- `reloadPlugin(id)` — deactivate if active, re-inspect the stored path (`inspect()` for
  a bare dylib, `inspectPackage()` for a package — keyed off `packageDir`), activate
  again if enabled. No directory rescan (unlike the old "reload = rescan everything"
  behavior, since dropped — a stale scan could silently pick up an unrelated file at the
  same path).
- `installPlugin(zipPath)`/`removePlugin(id)` (U3.2) — thin wrappers around
  `PluginInstaller` that also keep `_records` in sync: install re-inspects the freshly
  extracted package and adds/replaces its record; remove deactivates first, then deletes.
  Both return an empty string on success, a human-readable error otherwise. Picking the
  file in the install dialog counts as the D10 consent (same intent reasoning as D14), so
  install routes the fresh record through `approvePlugin()`.
- `approvePlugin(id)` (U3.3) — the one consent flow: strips quarantine (macOS), records
  the consent digest (manifest version + SHA-256 over the manifest and resolved binary,
  in `PluginSettings`) so approval persists across restarts until the plugin's content
  changes, then activates if enabled. Fails closed: an unreadable plugin stays
  unapproved.

#### `PluginInstaller` — Package Install/Remove ([PluginInstaller.h](PluginInstaller.h))
A static utility backing the Plugins settings page's "Install plugin…"/"Remove" actions
(U3.2, D8):
- `installFromFile(zipPath)` reads and validates `qgcplugin.json` at the archive root
  *before* extracting anything else, then extracts to `<user-plugins-dir>/<manifest.id>/`,
  replacing any existing install of the same id. Extraction is in-process via
  [QGCCompression](../Utilities/Compression/QGCCompression.h) rather than shelling out, so
  the extracted files are never quarantined by Gatekeeper the way a browser download would
  be (01 §1.4) — a per-entry path check rejects zip-slip attempts (`../` escapes) before
  any file is written.
- `removePlugin(id)` deletes `<user-plugins-dir>/<id>/` entirely. The caller
  (`QGCPluginManager::removePlugin`) is responsible for deactivating the plugin first.
- `isFileQuarantined(path)`/`stripQuarantine(path)` (macOS only) check/clear
  `com.apple.quarantine` — for plugins that arrived by some other means than
  `installFromFile()` (e.g. dropped into the plugins directory by hand) and still carry
  the attribute. `QGCPluginManager`'s trust gate checks a package's manifest plus its
  resolved binary (D15 — the only files whose quarantine status matters; QML/assets are
  read as data) and gates hits into `PluginState::NeedsApproval`; `approvePlugin(id)`
  strips the whole package tree (or the bare file) before activating.

#### `QGCPlugin` — Runtime Plugin Base Class ([QGCPlugin.h](../PluginAPI/QGCPlugin.h))
Base class for the loaded plugin instance itself. Code is only for behaviour:
`init(QGCHostServices*)`/`cleanup()` lifecycle and `replayExtension()` (queried only
when the manifest declares `"replay": true`). Everything static — tool menu entry,
panels, the telemetry-logging claim, the display name — is manifest data, never a
virtual.

#### Host Services ([HostServices/](HostServices/))
The host side of the SDK's service seam. `QGCHostServicesImpl` is the id → `QObject`
registry the manager constructs (lazily, on first plugin activation) and passes to every
`QGCPlugin::init()`. Each service is a thin wrapper delegating to the internal owner:

| Service id | SDK interface | Wraps |
|---|---|---|
| `qgc.replay/1` | [QGCReplayService.h](../PluginAPI/QGCReplayService.h) | `LinkManager::startLogReplay()` + the active `LogReplayLink`, `ParameterManager::registerReplayParamFile()`, `Vehicle::registerReplayPlanFile()` |
| `qgc.telemetryLogging/1` | [QGCTelemetryLoggingService.h](../PluginAPI/QGCTelemetryLoggingService.h) | `MAVLinkProtocol`'s tlog recording surface |
| `qgc.vehicles/1` | [QGCVehicleService.h](../PluginAPI/QGCVehicleService.h) | `MultiVehicleManager`: active vehicle, vehicle list, add/remove signals (vehicles handed over as `QObject*`) |
| `qgc.missions/1` | [QGCMissionService.h](../PluginAPI/QGCMissionService.h) | Per-vehicle mission readiness (`Vehicle::initialPlanRequestComplete`) and mission snapshot to a `.plan` file via a transient `PlanMasterController` |
| `qgc.parameters/1` | [QGCParameterService.h](../PluginAPI/QGCParameterService.h) | Per-vehicle parameter readiness (`ParameterManager::parametersReady`) and parameter snapshot to a `.params` file via `ParameterManager::writeParametersToStream` |
| `qgc.app/1` | [QGCAppService.h](../PluginAPI/QGCAppService.h) | `QCoreApplication` identity (name/org/version) and `AppSettings` storage paths |

Plugins acquire a service with `host->service(id)` and `qobject_cast` to the SDK
interface; an unknown id returns `nullptr` (plugins must tolerate absent services).
Service ids are append-only: a breaking change ships as a new id (`qgc.replay/2`),
never as a change to an existing interface.

#### `QGCPluginInterface` — Qt Plugin Factory Interface ([QGCPluginInterface.h](../PluginAPI/QGCPluginInterface.h))
```cpp
#define QGCPluginInterface_iid "org.qgroundcontrol.QGCPluginAPI/2.0"
inline constexpr int QGCPluginApiVersion = 2;
```
`pluginInterfaceVersion()` is a belt-and-braces runtime check; the manifest's
`apiVersion` (checked during `inspect()`, before any code runs) is authoritative.

## The QML Contract — `QGroundControl.PluginUI`

The second published surface. Where the C++ services say what a plugin may *call*, this
says what its QML may be *built from*.

A plugin's QML has no compile step: it ships in the plugin's own resources and every
`import` resolves at runtime, inside the host's `QQmlEngine`. So the module is a
**resolution contract, not a code library** — it exists so plugin QML can be checked
against a named, curated vocabulary at lint time, while the host keeps supplying every
implementation at runtime. Nothing is vendored, nothing is reimplemented, and a
contributed panel cannot drift from the host's look or theming.

### One roster, two forms

[PluginUI/qmldir](PluginUI/qmldir) is the whole declaration, and the only hand-maintained
list. It ships verbatim as a resource at `:/qml/QGroundControl/PluginUI/qmldir`, which the
engine finds because `QGCCorePlugin::createQmlApplicationEngine()` already puts
`qrc:/qml` on the import path.

Its entries use **explicit relative paths** into the owning module's resource directory
(`QGCLabel 1.0 ../Controls/QGCLabel.qml`). That is load-bearing, not cosmetic:

- `QQmlTypeLoader` keys composite types by URL, so pointing both URIs at one URL yields
  **one type and one singleton instance**. Registering copies of the same `.qml` under a
  second URI instead — the obvious approach, a second `qt_add_qml_module` over the same
  sources — produces two distinct types and two singleton instances that still render
  identically. `test/PluginSystem/PluginUIModuleTest.cc` asserts against that, because
  appearance cannot catch it.
- A `prefer` line achieves the same shared URL but resolves *any* `.qml` sitting in the
  preferred directory, which defeats curation. So does a qmldir `import` directive, which
  re-exports a whole module.

C++ types cannot be declared in a qmldir, so the few the module publishes are
re-registered under the second URI by [PluginUIModule.cc](PluginUIModule.cc). That
registration is additive and yields the *same* C++ type under both URIs — no class moves
out of the executable to be published.

For a package, `tools/derive_plugin_ui_sdk.py` reduces the same roster to the flat form an
out-of-tree consumer needs: a qmldir with bare filenames, copies of the control bodies
(Qt generates no metadata for composite types, so the bodies themselves are the lint
fixture), and a `.qmltypes` carved out of the host build's own generated metadata with its
exports retargeted. `find_package(QGCPluginAPI)` exposes the result as
`QGCPluginAPI_QML_IMPORT_PATH`.

### Two stability tiers

Both tiers are published metadata and resolve identically; they differ in what a consumer
may rely on. The **frozen tier** — the controls, the singletons and the C++ types — accepts
additions freely, but a removal or a shape change is a breaking change needing a version
bump. The **unstable tier** — the map and mission visuals — is published so plugin QML
resolves at lint time, and is explicitly exempt from the freeze: freezing a core
controller's QML surface is not a commitment the host can credibly keep. The roster marks
which is which.

Engine-level services need no publishing. The `coloredsvg` image provider that
`QGCColoredImage` routes through is registered on the host engine, which the plugin's QML
is already running in.

### The host-global facade

`QGroundControlQmlGlobal` — the app's god object, exposed to base QML as the `QGroundControl`
singleton under URI `QGC` — is itself **not** published, and neither is the `import QGC`
re-export that would drag along `ParameterEditorController` and every other app-level type.
Instead [PluginUIGlobal.h](PluginUIGlobal.h) publishes `QGCPluginUIGlobal`, a narrow facade
carrying only the members that are contract: `multiVehicleManager`, `corePlugin`,
`settingsManager`, `pluginManager`, `globalPalette`, `zOrderTopMost`, `flightMapPosition`,
`flightMapZoom`, `showMessageDialog`, `copyToClipboard`.

It is registered under `QGroundControl.PluginUI` as `QGCPluginUIGlobal` — its own class
name, not the host singleton's QML name (`QGroundControl`). That is a consequence of how
the SDK derivation works, not a stylistic choice: `tools/derive_plugin_ui_sdk.py` carves
this type's SDK-side metadata from its auto-registration under the executable's own `QGC`
module (needed only to give it an `exports:` line at all — composite-style name aliasing
isn't visible to the carve step), so the display name used in `PluginUIModule.cc` must match
the class name exactly or the derived package would publish a type under a name no plugin
actually imports.

Every member **forwards** to the one real `QGroundControlQmlGlobal` singleton instance
(located via `QQmlEngine::singletonInstance()`, the same mechanism `QGCFileDialogController`'s
delegate lambda in [PluginUIModule.cc](PluginUIModule.cc) uses) rather than holding a second
copy of its state — a write through either URI is visible through the other, asserted by
`PluginUIModuleTest::_facadeDelegatesToHostGlobal_test`.

## Plugin Lifecycle

```
1. Application Startup (before the QML engine exists — QGCApplication.cc)
   └── QGCPluginManager::init()
       └── QGCPluginManager::_loadPlugins()
           ├── QGCPluginLoader::defaultPluginPaths()
           ├── QGCPluginLoader::inspectDirectories() — reads manifests + contributions,
           │   zero code run
           └── _processInspected():
               ├── Registers each valid plugin's id with PluginSettings
               ├── Duplicate id → Failed, recorded, never activated
               ├── Disabled by settings → recorded as Disabled, _activateRecord()
               │   is never called (its code never runs)
               └── Otherwise → _activateRecord():
                   ├── QGCPluginLoader::activate() — instance()/qobject_cast/createPlugin();
                   │   a no-op for tier Qml (no binary — info.plugin stays null)
                   ├── plugin->init(host) iff a plugin instance exists (tier != Qml)
                   ├── replayExtension() queried iff the manifest declares "replay";
                   │   first plugin wins, a second is logged (qCWarning) and ignored
                   └── Publishes the manifest-derived contributions to QML

2. Runtime
   ├── User toggles a plugin → QGCPluginManager::setPluginEnabled(id, bool)
   │   ├── Enabling: re-inspect the stored path, activate if still valid
   │   └── Disabling: cleanup() + delete the instance; contributions removed;
   │       the library mapping itself stays until process restart
   └── QGCPluginManager::reloadPlugin(id) — same deactivate/re-inspect/activate
       flow, using the stored file path (no directory rescan)

3. Application Shutdown
   └── QGCPluginManager::cleanup() — plugin->cleanup() + delete on every Active record
```

Android has no dynamic load/unload: plugins compile into the APK; the enabled toggle
only controls which ones activate at startup.

## Creating a Plugin

See [plugins/README.md](../../plugins/README.md) for the plugin-author's-eye view
(manifest schema, CMake pattern, search paths, settings behavior). This document is the
architecture reference for the loader/manager internals themselves.

## Examples

See `/plugins/` for complete examples:
- `example/` — minimal plugin with a tool menu item
- `qdrive/` — full-featured plugin with AWS integration
