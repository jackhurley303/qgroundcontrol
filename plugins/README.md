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
- Plugins build **in-tree** (this directory, via `qgc_add_plugin()`) or **out-of-tree**
  against the published SDK — the two arms are indistinguishable at runtime, by design.
  See [Tiers](#tiers) and [SDK Package](#sdk-package-out-of-tree-tier-b).

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
- **`tier`** — `sdk` (links only the published `QGCPluginAPI` + Qt, no QGC internals),
  `qml` (no binary at all — see [Packages](#packages) below), or `internal` (full access
  to QGC internals, gated to a matching `hostBuildId`). See [Tiers](#tiers) for which to
  choose and what each one can actually be built with today.
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
    TIER SDK                   # the only value accepted — see Tiers above
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
| `qgc.parameters/1` | `QGCParameterService` | Per-vehicle parameter readiness + snapshot of a vehicle's current parameters to a `.params` file |
| `qgc.parameterDiff/1` | `QGCParameterDiffService` | Diff a QGC- or Mission Planner-format parameter file against a vehicle, and write the accepted differences back |
| `qgc.app/1` | `QGCAppService` | Host identity (app/org name, version) and storage paths (save root, telemetry directory) |

3. **Real linkage** — `TIER SDK` links only the published `QGCPluginAPI` shared
   library + Qt (`@rpath`), never the `QGroundControl` target itself; every symbol
   it needs resolves from that dylib, the same shape an out-of-tree author gets
   from the SDK zip (Stage 2, §7 of the macOS implementation plan). See
   `plugins/example/CMakeLists.txt` and `plugins/qdrive/CMakeLists.txt`.
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

Package **installation** works today: Application Settings → Plugins → "Install plugin…"
validates a `.qgcplugin`'s manifest before extracting anything, unpacks it in-process to
`<plugins dir>/<id>/` (so Gatekeeper never quarantines the extracted files), and offers
Remove for it afterwards. Build one with
[tools/pack_plugin.py](#building-a-qgcplugin). Tier `internal` is refused here — a
package's binary could otherwise be swapped without the loader re-checking it against the
manifest that granted it trust.

For the dev loop you can skip packaging entirely: a package directory dropped into a
search path is discovered exactly like a bare dylib. That is also the only way to iterate
on a `qml`-tier plugin, which has no binary to deploy.

## Signing Your Plugin

The release build of QGroundControl runs with the hardened runtime and
`com.apple.security.cs.disable-library-validation` ([deploy/macos/qgroundcontrol-release.entitlements](../deploy/macos/qgroundcontrol-release.entitlements)) —
without that entitlement, dyld refuses to load *any* library not signed by
the app's own Team ID, which would block every third-party plugin outright.
With it, a plugin dylib just needs a valid signature of its own (not
necessarily the same team) to load:

- **Local dev loop** — ad-hoc signing (`codesign -s -`) is enough; this is
  also what an unsigned dylib gets away with on Intel (leniency not present
  on arm64, which requires at least ad-hoc).
- **Distribution** — sign with a Developer ID certificate. If you're
  distributing the plugin dylib on its own (outside a `.qgcplugin` package,
  which is unzipped in-process and consent-gated — see
  [Packages](#packages)), also notarize it: anything that downloads through
  a browser or similar picks up the quarantine xattr, which Gatekeeper
  enforces independently of how the host app is signed.
- **Universal builds** — build your plugin `x86_64;arm64` (the CI host is
  built `x86_64h;arm64`; the `h` sub-type is compatible). A single-arch
  plugin only loads on a host running that arch — the loader reports the
  mismatch via `errorString()` rather than failing silently, but a universal
  binary avoids the split entirely.

## Plugin Settings

- Plugins are registered with `PluginSettings` **by manifest `id`**, not display name —
  renaming a plugin's `name` doesn't lose its enabled/disabled state.
- Users toggle plugins in Application Settings → Plugins. Each row shows
  name/version/vendor, the description, and a line reading
  `<tier> · <source> · <status>` — where *source* is **Development build** (a bare binary
  in the user plugins directory, i.e. your build's auto-deploy), **Installed** (a package
  under that directory, removable) or **Bundled** (ships with the app, trusted, not
  removable). Status is "Active", "Disabled", "Incompatible: <reason>", "Failed to load:
  <reason>", "Quarantined: <reason>" or an approval prompt.
- A **Built <date>** line follows for any active plugin with a binary, decoded from the
  plugin's build-marker symbol; hovering it shows the full marker. "Build unknown" means
  the plugin was built without `qgc_plugin_build_marker()` — it runs fine, but cannot tell
  you which build is executing.
- Toggling calls `QGCPluginManager::setPluginEnabled(id, bool)`, which activates or
  deactivates the plugin **immediately**, in memory — no restart.
- **Default state**: every discovered plugin defaults to enabled. What actually gates a
  plugin is the trust model, not the default: a plugin in the **user** plugins directory
  needs explicit approval on first sight and again whenever its bytes change (which is why
  rebuilding one re-prompts), while a bundle-shipped plugin is trusted outright.
- Plugin **code** changes (C++ or QML compiled into the binary) still require rebuilding
  the plugin; the enable/disable toggle only controls whether the already-built library
  is loaded.

## Tiers

`tier` in the manifest. The **loader** implements all three; the **build tooling** does
not, and the difference matters when choosing one:

- **`sdk`** — links only the published `QGCPluginAPI` + Qt, no QGC internals. Gated on two
  axes: `apiVersion` must exactly equal the host's, and the host version must fall inside
  the declared `hostVersion` range — so it survives host rebuilds and point releases.
  Ships as a bare dylib *or* a [package](#packages). **The only tier `qgc_add_plugin()`
  builds**, and the tier every plugin in this tree uses (`example`, `qdrive`, and the test
  fixture). Choose it for anything with logic — network I/O, storage, log parsing, vehicle
  interaction. See [Example Plugin](#example-plugin) for the full SDK boundary story.
- **`qml`** — no compiled binary at all: pure manifest + QML, discovered from a
  [package](#packages) directory. `QGCPluginLoader::activate()` is a no-op for it, and
  relative URLs in `contributes` resolve package-relative, so **no build system is
  involved** — author the directory by hand and drop it in a search path. Gated on
  `qmlApiVersion`, and only when declared. Cannot declare `replay` or `telemetryLogging`
  (nothing exists to implement them), and a compiled file declaring this tier is rejected.
  Choose it when the plugin is purely a view.
- **`internal`** — full access to QGC internals, pinned to an exactly-matching
  `hostBuildId`, so it rebuilds in lockstep with the host. **No supported build path
  today**: `qgc_add_plugin()` hard-fails on any `TIER` but `SDK`, so this tier means
  hand-rolled CMake, and nothing in the tree uses it. It also
  [cannot be packaged](#packages). If you want it because you need a QGC internal, the
  better answer is usually a new extension point in the base app, keeping the plugin on
  `sdk`.

## Building a `.qgcplugin`

[tools/pack_plugin.py](../tools/pack_plugin.py) packs a package directory into the zip the
settings page's "Install plugin…" consumes. It applies `PluginInstaller`'s rules at pack
time — manifest validity, the tier layout rules, no bundled Qt/`QGCPluginAPI`, no symlinks,
the 512 MB extraction ceiling — because every one of those rejections otherwise happens on
a user's machine where the author never sees it:

```bash
# A package directory already laid out with bin/<platform>/
python3 tools/pack_plugin.py path/to/org.example.myplugin

# Or let it place a freshly built binary for you
python3 tools/pack_plugin.py path/to/pkg --binary build/plugins/libMyPlugin.dylib

python3 tools/pack_plugin.py path/to/pkg --dry-run   # list contents, write nothing
```

Output defaults to `<id>-<version>.qgcplugin`. The archive holds the package directory's
*contents* at its root (not the directory itself) — nesting one level down is the classic
`zip -r` mistake, and the installer sees no manifest at all. Entries are sorted and carry
zip's epoch timestamp, so repacking unchanged content is byte-identical.

The tool ships inside the SDK package at `<sdk>/tools/`, alongside
[the out-of-tree gate](#sdk-package-out-of-tree-tier-b), so an out-of-tree author runs the
same rules CI does.

## SDK Package (out-of-tree Tier B)

`plugins/example/` is the one bundled reference plugin, and it's dual-mode
(out-of-tree-verifier.md U1): in-tree it builds via `qgc_add_plugin()` for the dev loop
(below); packaged inside the SDK zip (`qgc-plugin-sdk-macos-<version>.zip`, built by
`cmake --install <build-dir> --component QGCPluginSDK`, packed in `macos.yml`) it
builds standalone with **`find_package(QGCPluginAPI)`**, never `qgc_add_plugin()` —
that helper is an in-tree build convenience, not part of the published package, so this
is the actual out-of-tree path a real SDK consumer uses. See
`plugins/example/SDK-README.md` (installed at the package root) for the full
compatibility contract and ABI rules.

Proving that out-of-tree build (and its QML/test/symbol axes) is
`tools/verify_plugin_out_of_tree.py`, installed into the package at `<sdk>/tools/` — the
same plugin-agnostic tool CI runs against `plugins/example`, driven by its
`plugin-verify.json` manifest. It has no plugin name or per-plugin branch in it, so any
plugin (out-of-tree QDrive included) can adopt it by adding its own manifest.

## Example Plugin

See `example/` for a working `TIER SDK` plugin: adds "Example Plugin" to the Tools
menu, contributes fly-view and plan-view panels, shows a custom QML view, demonstrates
resource bundling and a host-service lookup shape (`ExampleRuntimePlugin::init()`). It
is the SDK's single bundled reference plugin and doubles as the out-of-tree copy-and-build
starting point above.

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
