# QGroundControl Plugins

This directory contains QGC runtime plugins that extend core functionality.

## Plugin Architecture

QGC uses a dynamic plugin system managed by `QGCPluginManager`:

- Each plugin is a Qt `MODULE` library declaring a **manifest** (`qgcplugin.json`) that
  states its identity, compatibility range, and its contributions.
- `QGCPluginManager::init()` runs before the QML engine exists
  ([QGCApplication.cc](../src/QGCApplication.cc)), so a plugin's contributions must be
  knowable from data, not from running its code.
- Loading is two-phase: **inspect** reads and validates the manifest without executing
  any plugin code; **activate** instantiates the plugin only if it's valid *and* enabled.
  A disabled or incompatible plugin's code never runs.
- Plugins are built **in-tree** today (see [Tier roadmap](#tier-roadmap) below for where
  this is headed).

See [src/PluginSystem/README.md](../src/PluginSystem/README.md) for the architecture in
detail (manifest schema, loader states, manager internals).

## The Manifest

Every plugin ships a `qgcplugin.json` (usually generated from a `qgcplugin.json.in` via
CMake's `configure_file`, so `hostBuildId` can be stamped with the host's build hash):

```json
{
    "id": "org.qgroundcontrol.example",
    "name": "Example",
    "version": "1.0.0",
    "vendor": "QGroundControl",
    "description": "Demonstrates the QGC plugin system",
    "tier": "sdk",
    "apiVersion": 2,
    "hostVersion": { "min": "5.0", "max": "" },
    "hostBuildId": null,
    "contributes": {
        "toolMenu": {
            "title": "Example",
            "icon": "/qmlimages/plugin.svg",
            "source": "qrc:/qml/ExamplePluginView.qml",
            "toolbarSource": "qrc:/qml/ExampleToolBar.qml"
        },
        "flyViewPanel": {
            "panel": "qrc:/qml/ExampleFlyViewPanel.qml",
            "dock": "qrc:/qml/ExampleFlyViewDockItem.qml",
            "defaultWidth": 35,
            "defaultHeight": 18,
            "defaultPosition": [0.0, 0.0]
        },
        "planViewPanel": { "panel": "qrc:/qml/ExamplePlanViewPanel.qml" },
        "replay": false,
        "telemetryLogging": false
    }
}
```

- **`id`** — reverse-DNS, stable identity. This is the key used for the plugin's
  enabled/disabled setting (`PluginSettings`), *not* its display name.
- **`tier`** — `internal` (full access to QGC internals, gated to a matching
  `hostBuildId`), `sdk` (links only the published `QGCPluginAPI` + Qt, no QGC
  internals), or `qml` (no binary at all — see [Packages](#packages) below) — all three
  work today (see [Tier roadmap](#tier-roadmap)).
- **`apiVersion`** — must equal the host's supported major version
  (`QGCPluginApiVersion` in [QGCPluginInterface.h](../src/PluginAPI/QGCPluginInterface.h));
  required for `sdk`/`internal`, optional and unchecked for `qml` (no binary, no C++ ABI).
- **`qmlApiVersion`** — tier `qml` only; the `QGroundControl` QML singleton tree's API
  level (`QGCPluginQmlApiLevel` in
  [PluginManifest.h](../src/PluginSystem/PluginManifest.h)). Optional — declared values
  are checked against the host's, undeclared is unchecked.
- **`hostVersion.min`/`.max`** — half-open range `[min, max)`; empty `max` means unbounded.
- **`hostBuildId`** — required and checked for `tier: "internal"` only; a mismatch means
  "built for another QGC build."
- **`contributes`** — the plugin's static contributions, declared as data (every key
  optional; schema and defaults documented in
  [PluginContributions.h](../src/PluginSystem/PluginContributions.h)):
  - **`toolMenu`** — an entry in the main tool menu; `title` and `source` (the
    full-screen view's QML URL) are required, `icon` and `toolbarSource` (custom toolbar
    QML) optional.
  - **`flyViewPanel`** / **`planViewPanel`** — a floating panel; `panel` (QML URL) is
    required, `dock` (collapsed dock-row QML), `defaultWidth`/`defaultHeight`
    (font-size units, 0 = framework default) and `defaultPosition` (`[x, y]` fractions
    of the view, `[-1, -1]` = framework default) optional.
  - **`replay`** — the plugin provides a flight replay extension; its
    `replayExtension()` override is only queried when declared.
  - **`telemetryLogging`** — the plugin claims exclusive control of tlog logging
    (disables MAVLinkProtocol's built-in auto-start/auto-save).

  Contributions are synthesized from the manifest at inspection time and shown only
  while the plugin is enabled — plugin code never runs to produce them. URLs starting
  with `qrc:/` (or a bare resource path, e.g. `/qmlimages/...`) name compiled-in/host
  resources and pass through verbatim; any other (relative) URL is resolved
  package-relative to `file://<package dir>/<url>` for a [package](#packages) plugin,
  or left as declared for a dev-loop bare-dylib plugin (no package directory to resolve
  against).

## Creating a New Plugin

`qgc_add_plugin()` ([cmake/modules/PluginHelpers.cmake](../cmake/modules/PluginHelpers.cmake))
is the single entry point for building a plugin — it configures the MODULE library, wires up
the manifest, applies common compile definitions/include dirs/Qt linkage, sets the
undefined-symbol link options, and auto-deploys the built library to the host's runtime plugin
directory for the dev loop:

```cmake
qgc_add_plugin(MyPlugin
    TIER SDK                   # or INTERNAL for full QGC-internals access; QML is Stage 3
    MANIFEST qgcplugin.json.in
    SOURCES
        MyPlugin.h
        MyPlugin.cc
    QRC_FILES
        MyPlugin.qrc
)
```

Prefer `TIER SDK` unless the plugin genuinely needs a QGC internal not exposed by a
host service (§ below) — it's the tier a real out-of-tree SDK consumer will use, links
nothing but `QGCPluginAPI` + Qt, and the include boundary is enforced by the build
itself (no `src/` path means a violation fails to compile, not just to link). See
`plugins/example/` for a working `TIER SDK` plugin and `plugins/qdrive/` for `TIER
INTERNAL`.

Extras beyond the common set (extra Qt modules, a test subdirectory, ...) are added with
normal CMake commands after the call — see
[`plugins/qdrive/CMakeLists.txt`](qdrive/CMakeLists.txt) for an example with extra Qt modules.

1. Create `plugins/yourplugin/` with a `.h`/`.cc`/`.qrc`, a `qgcplugin.json.in`, and a
   `CMakeLists.txt` modeled on [`plugins/example/CMakeLists.txt`](example/CMakeLists.txt).
2. Implement `QGCPluginInterface` (factory) and a `QGCPlugin` subclass:

```cpp
#include <QGCPluginAPI/QGCPlugin.h>
#include <QGCPluginAPI/QGCPluginInterface.h>

class MyPlugin : public QObject, public QGCPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")
    Q_INTERFACES(QGCPluginInterface)

public:
    int pluginInterfaceVersion() const override { return QGCPluginApiVersion; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

class MyRuntimePlugin : public QGCPlugin {
    Q_OBJECT

    // Contributions (tool menu, panels, flags) are declared in the manifest's
    // "contributes" object, not in code. Override init(host)/cleanup() for
    // lifecycle work and replayExtension() to provide flight replay.
};
```

Note the `FILE "qgcplugin.json"` reference — Qt embeds that file's contents as the
plugin's metadata, which is what `QGCPluginLoader::inspect()` reads without running any
code. The manifest's own `id`/`apiVersion`/etc. are what's actually validated; the
`Q_PLUGIN_METADATA` IID only has to match `QGCPluginInterface_iid`.

`init(QGCHostServices* host)` receives the host's service registry (valid for the
plugin's lifetime). Services are acquired by their versioned id and cast to the
matching SDK interface; an unknown id returns `nullptr`, and plugins must tolerate
absent services:

```cpp
void MyRuntimePlugin::init(QGCHostServices* host)
{
    auto* replay = host ? qobject_cast<QGCReplayService*>(host->service(QGCReplayServiceId)) : nullptr;
    if (replay) {
        // start/control tlog replay sessions, register param/plan sidecar files
    }
}
```

Services provided today (ids are append-only — a breaking change ships as a new id,
never as a change to an existing interface):

| Id | SDK interface | Purpose |
|---|---|---|
| `qgc.replay/1` | `QGCReplayService` | Tlog flight-replay sessions: start/stop, playback control, param/plan sidecar registration |
| `qgc.telemetryLogging/1` | `QGCTelemetryLoggingService` | Tlog recording control: start/stop, pending-log save/discard |
| `qgc.vehicles/1` | `QGCVehicleService` | Connected vehicles as `QObject*` (meta-object surface): active vehicle, list, add/remove signals |
| `qgc.missions/1` | `QGCMissionService` | Per-vehicle mission readiness + snapshot of a vehicle's current mission to a `.plan` file |
| `qgc.app/1` | `QGCAppService` | Host identity (app/org name, version) and storage paths (save root, telemetry directory) |

3. **Real linkage** — tier-dependent, never the `QGroundControl` target itself:
   - `TIER SDK` links only the published `QGCPluginAPI` shared library + Qt (`@rpath`);
     every symbol it needs resolves from that dylib, the same shape an out-of-tree
     author gets from the SDK zip (Stage 2, §7 of the macOS implementation plan). See
     `plugins/example/CMakeLists.txt`.
   - `TIER INTERNAL` links Qt only and resolves QGC-internal symbols at `dlopen` time
     from the running executable, via `-undefined dynamic_lookup` (macOS) or
     `-Wl,--allow-shlib-undefined` (Linux) — see `plugins/qdrive/CMakeLists.txt`. This
     only works because the app is built with `-Wl,-export_dynamic` today; that's
     scaffolding for Tier C plugins, not something a real SDK consumer should rely on.
4. Build: `cmake --build build --target MyPlugin`. The plugin auto-deploys (via a
   `POST_BUILD` copy step) to the platform's user plugins directory for local iteration.

## Plugin Discovery

`QGCPluginLoader::defaultPluginPaths()` returns, per platform:

**macOS**:
- `QGroundControl.app/Contents/PlugIns/`
- `<app bundle>/plugins/`
- `~/Library/Application Support/QGroundControl/plugins/`

**Linux**:
- `<app dir>/plugins/`
- `<app dir>/../lib/qgroundcontrol/plugins/`
- `~/.local/share/QGroundControl/plugins/`

**Windows**:
- `<app dir>/plugins/`
- `%APPDATA%/QGroundControl/plugins/`

Every plugin file found in these directories is **inspected** (manifest read,
validated) at startup; only valid **and** enabled ones are **activated**. A child
*directory* containing `qgcplugin.json` at its root is a **package** (below) — both
forms are discovered side by side in the same search paths.

## Packages

A package is a directory (or, once installed, a `.qgcplugin` zip's extracted contents)
laid out like:

```
org.example.qgc.mypackage/
├── qgcplugin.json              # manifest — read directly, not via QPluginLoader metadata
├── qml/ …                      # tier qml content and/or panel QML
├── assets/ …
└── bin/                        # absent for tier qml
    └── macos-universal/
        └── MyPlugin.dylib
```

- **Tier `qml` (no `bin/` at all)** — no binary; contributions synthesize from the
  manifest alone (`QGCPluginLoader::activate()` is a no-op — nothing to instantiate),
  with relative URLs resolved package-relative. A `qml`-tier manifest that declares a
  binary, or declares `replay`/`telemetryLogging` (nothing exists to implement them),
  fails inspection.
- **Tier `sdk`/`internal`** — the binary is looked up under `bin/macos-universal/` (the
  documented key on macOS); if that's absent, the loader falls back to any single binary
  under a `bin/macos-*/` directory. Anything else (missing, or more than one candidate)
  fails inspection with a legible reason rather than guessing.
- Package identity/contributions always come from the sidecar `qgcplugin.json`, never
  from a binary's own embedded `Q_PLUGIN_METADATA` (even for tier `sdk`/`internal`,
  which happen to carry one too, built the same way as any other plugin).

Package **installation** (`.qgcplugin` → `<plugins dir>/<id>/`, consent, removal) is a
later stage — today, package directories are discovered exactly like bare dylibs: drop
one in a search path above for the dev loop.

## Plugin Settings

- Plugins are registered with `PluginSettings` **by manifest `id`**, not display name —
  renaming a plugin's `name` doesn't lose its enabled/disabled state.
- Users toggle plugins in Application Settings → Plugins; the page shows each plugin's
  name/version/vendor and a status line ("Active", "Disabled", "Incompatible: <reason>",
  "Failed to load: <reason>", "Quarantined: <reason>").
- Toggling calls `QGCPluginManager::setPluginEnabled(id, bool)`, which activates or
  deactivates the plugin **immediately**, in memory — no restart.
- **Default state**: interim rule until the trust model (manifest-driven, source-dir
  based) lands — the Example plugin defaults off, every other plugin defaults on.
- Plugin **code** changes (C++ or QML compiled into the binary) still require rebuilding
  the plugin; the enable/disable toggle only controls whether the already-built library
  is loaded.

## Tier Roadmap

`tier` in the manifest — all three work today, `qml` (Tier A) is dev-loop only until the
installer UX (`.qgcplugin` → "Install from file…") lands:

| Tier | Status | Description |
|---|---|---|
| `internal` | **Working today** | Full access to QGC internals, gated by matching `hostBuildId` (rebuilds together with the host). `qdrive` is this tier. |
| `sdk` | **Working today** | Links only a stable `QGCPluginAPI` shared library + Qt; loads into any host build within its declared version range. `example` is this tier — see [Example Plugin](#example-plugin) and `plugins/.architecture/04-macos-implementation-plan.md` §5 for the full SDK boundary story. |
| `qml` | **Working (dev-loop)** | No compiled binary at all — pure manifest + QML, discovered from a [package](#packages) directory. Installing a `.qgcplugin` from the settings page is a later stage; today, drop a package directory into a search path to test one. |

## SDK Package (out-of-tree Tier B)

`plugins/template/` is the source of the standalone, copy-and-build plugin project
shipped inside the packaged SDK zip (`qgc-plugin-sdk-macos-<version>.zip`, built by
`cmake --install <build-dir> --component QGCPluginSDK`, packed in `macos.yml`). Unlike
`plugins/example/`, it builds with **`find_package(QGCPluginAPI)`**, not
`qgc_add_plugin()` — that helper is an in-tree build convenience, not part of the
published package, so the template proves the actual out-of-tree path a real SDK
consumer uses. See `plugins/template/SDK-README.md` (installed at the package root) for
the full compatibility contract and ABI rules.

## Example Plugin

See `example/` for a minimal working `TIER SDK` plugin: adds "Example Plugin" to the
Tools menu, shows a custom QML view, demonstrates resource bundling and a host-service
lookup shape (`ExampleRuntimePlugin::init()`).

## Troubleshooting

**Plugin not loading:**
- Check logs: `qCDebug(QGCPluginLoaderLog)` / `qCDebug(QGCPluginManagerLog)`.
- Look for "Validated `<name>` (`<tier>`, build `<hash>`) before load" — if it's missing,
  inspection rejected the manifest (check `errorString` in the log). `build` is empty for
  non-`internal` tiers.
- Confirm the plugin is in one of the search paths above and has the right extension
  (`.dylib`/`.so`/`.dll`).

**Plugin menu item / panel not visible:**
- Application Settings → Plugins — confirm the plugin's status is "Active", not
  "Disabled"/"Incompatible"/"Failed".

**Runtime crashes:**
- Don't access `activeVehicle()` without a null-check.
- Wait for `parametersReady` before accessing Facts.

## Resources

- [src/PluginSystem/README.md](../src/PluginSystem/README.md) — architecture detail
- [Example Plugin](example/)
- [QGC Coding Standards](../CODING_STYLE.md)
- [Qt Plugin Documentation](https://doc.qt.io/qt-6/plugins-howto.html)
