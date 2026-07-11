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
    "id": "org.example.qgc.example",
    "name": "Example",
    "version": "1.0.0",
    "vendor": "Example Org",
    "description": "Demonstrates the QGC plugin system",
    "tier": "internal",
    "apiVersion": 2,
    "hostVersion": { "min": "5.0", "max": "" },
    "hostBuildId": "@QGC_GIT_HASH@",
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
- **`tier`** — `internal` (today's only working tier: full access to QGC internals,
  gated to a matching `hostBuildId`), `sdk`, or `qml` (see [Tier roadmap](#tier-roadmap)).
- **`apiVersion`** — must equal the host's supported major version
  (`QGCPluginApiVersion` in [QGCPluginInterface.h](../src/PluginAPI/QGCPluginInterface.h)).
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
  with `qrc:/` (or a bare resource path) name compiled-in resources; package-relative
  URLs are a planned Stage 3 feature.

## Creating a New Plugin

`qgc_add_plugin()` ([cmake/modules/PluginHelpers.cmake](../cmake/modules/PluginHelpers.cmake))
is the single entry point for building a plugin — it configures the MODULE library, wires up
the manifest, applies common compile definitions/include dirs/Qt linkage, sets the
undefined-symbol link options, and auto-deploys the built library to the host's runtime plugin
directory for the dev loop:

```cmake
qgc_add_plugin(MyPlugin
    TIER INTERNAL              # only tier that works today; SDK/QML are Stage 2/3
    MANIFEST qgcplugin.json.in
    SOURCES
        MyPlugin.h
        MyPlugin.cc
    QRC_FILES
        MyPlugin.qrc
)
```

Extras beyond the common set (extra Qt modules, a test subdirectory, ...) are added with
normal CMake commands after the call — see
[`plugins/qdrive/CMakeLists.txt`](qdrive/CMakeLists.txt) for an example with extra Qt modules.

1. Create `plugins/yourplugin/` with a `.h`/`.cc`/`.qrc`, a `qgcplugin.json.in`, and a
   `CMakeLists.txt` modeled on [`plugins/example/CMakeLists.txt`](example/CMakeLists.txt).
2. Implement `QGCPluginInterface` (factory) and a `QGCPlugin` subclass:

```cpp
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

3. **Real linkage** (macOS/Linux today): the plugin links Qt only, *not* the
   `QGroundControl` target. Undefined symbols (QGC internals) are resolved at `dlopen`
   time from the running executable, via `-undefined dynamic_lookup` (macOS) or
   `-Wl,--allow-shlib-undefined` (Linux) — see `plugins/example/CMakeLists.txt` for the
   exact flags. This only works because the app is built with `-Wl,-export_dynamic`
   today; that's temporary scaffolding for Tier C plugins, not something a real SDK
   consumer should rely on.
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
validated) at startup; only valid **and** enabled ones are **activated**.

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

`tier` in the manifest is forward-looking; only `internal` actually works today:

| Tier | Status | Description |
|---|---|---|
| `internal` | **Working today** | Full access to QGC internals, gated by matching `hostBuildId` (rebuilds together with the host). Both `example` and `qdrive` are this tier. |
| `sdk` | Not yet built | Links only a stable `QGCPluginAPI` shared library + Qt; loads into any host build within its declared version range. |
| `qml` | Not yet built | No compiled binary at all — pure manifest + QML, installed at runtime. |

## Example Plugin

See `example/` for a minimal working plugin: adds "Example Plugin" to the Tools menu,
shows a custom QML view, demonstrates resource bundling.

## Troubleshooting

**Plugin not loading:**
- Check logs: `qCDebug(QGCPluginLoaderLog)` / `qCDebug(QGCPluginManagerLog)`.
- Look for "Validated `<name>` (internal, build `<hash>`) before load" — if it's missing,
  inspection rejected the manifest (check `errorString` in the log).
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
