# Current State — What Is Implemented Today

This is an inventory of the plugin infrastructure as it exists on the `plugin-infrastructure-with-qdrive` branch, followed by the defects and gaps found during review.

## 1. Components

### Core plugin system (`src/PluginSystem/`)

| File | Role |
|------|------|
| `QGCPluginInterface.h` | The Qt plugin factory interface. Two methods: `pluginInterfaceVersion()` (must return `1`) and `createPlugin(QObject*)`. Declared with IID `org.qgroundcontrol.QGCPluginInterface/1.0`. |
| `QGCPlugin.h/.cc` | Base class for a loaded plugin instance. All extension points are virtuals on this class (see §2). |
| `QGCPluginLoader.h/.cc` | Discovery + loading. Scans per-platform directories for `.dll`/`.dylib`/`.so`, loads via `QPluginLoader`, `qobject_cast`s to `QGCPluginInterface`, checks `pluginInterfaceVersion() == 1`, calls `createPlugin()`. |
| `QGCPluginManager.h/.cc` | Application-static singleton. Orchestrates load at startup (`QGCApplication.cc:307`), registers each plugin with `PluginSettings`, skips disabled plugins, aggregates per-plugin contributions into QML-consumable `QVariantList`s, supports runtime `unloadPlugin()`/`reloadPlugin()`. |
| `QGCReplayExtension.h/.cc` | Abstract QObject interface for plugin-provided flight replay (tlog playback + video sync). Exposed to core UI as `QGCPluginManager::replayExtension` (first registered wins). Uses `QObject*` for flight entries so core has no dependency on plugin model types. |

The core plugin (`QGCCorePlugin`, `src/API/`) was cleanly separated from the runtime-plugin system — `QGCCorePlugin` remains the compile-time customization singleton; `QGCPluginManager` owns runtime plugins. This separation is good and should be preserved in any upstream PR.

### Extension points (host-side consumers)

| Extension point | Plugin-side hook | Host-side consumer |
|---|---|---|
| Tool menu item | `QGCPlugin::toolMenuItem()` → `QVariantMap` (title, icon, source QML) | Main window Tools menu via `QGCPluginManager::toolMenuItems` |
| Fly-view floating panel | `flyViewPanelUrl()`, `flyViewPanelDockUrl()`, default width/height/position | `src/FlyView/FlyViewPluginButtonStrip.qml`, `FlyViewWidgetLayer.qml` (panel state preserved on pop-out) |
| Plan-view floating panel | `planViewPanelUrl()` + same family | `src/PlanView/PlanView.qml`, `PlanViewPluginButtonStrip.qml` |
| Flight replay (video panel + replay bar) | `replayExtension()` → `QGCReplayExtension*` | Replay/video UI reads `QGCPluginManager::replayExtension`; log-replay infrastructure gained plugin-agnostic hooks (plan loading via registry, param seek resolution, open tlog/params from main window) |
| Telemetry logging takeover | `controlsTelemetryLogging()` | `src/Comms/MAVLinkProtocol.cc` disables built-in tlog auto-start/save and exposes start/stop/save/discard for the plugin to drive |
| Lifecycle | `init()` / `cleanup()` | Called by the manager on load/unload |

### Settings integration

- `src/Settings/PluginSettings.h/.cc` — a `SettingsGroup`; each plugin gets an enabled/disabled `Fact` keyed by its `name()`, persisted across restarts.
- `src/UI/AppSettings/PluginSettings.qml` — the Application Settings → Plugins page with per-plugin toggles.
- Toggling on desktop unloads/reloads the plugin from memory immediately; on Android the toggle only controls which compiled-in plugins load at startup.

### Build system

- `plugins/CMakeLists.txt` auto-discovers any subdirectory with a `CMakeLists.txt` (`CONFIGURE_DEPENDS` glob) and builds it as part of the QGC build.
- `cmake/modules/PluginHelpers.cmake` provides `qgc_add_plugin()`: MODULE library, AUTOMOC/AUTORCC, C++20, output to `build/$<CONFIG>/plugins/`, links Qt only, inherits `QGC_PLUGIN_COMPILE_DEFINITIONS` / `QGC_PLUGIN_INCLUDE_DIRECTORIES` from the parent build.
- The example plugin's own `CMakeLists.txt` shows the linkage model explicitly: include the whole QGC source tree (`src/`, `src/Vehicle`, `src/FactSystem`, …), link **only** Qt, and leave every QGC symbol undefined — `-undefined dynamic_lookup` (macOS) / `-Wl,--allow-shlib-undefined` (Linux) — to be resolved from the executable at load time. The executable exports its symbols for this purpose (`-Wl,-export_dynamic`, root `CMakeLists.txt:302-306`).
- Auto-deploy: a post-build step copies the built plugin into the user's app-data plugins directory for the dev loop.

### Reference plugins

- `plugins/example/` — template plugin exercising every extension point, with commented header showing what is optional.
- `plugins/qdrive/` — a real, large plugin (own git repo, submodule). It includes QGC internals extensively: `Comms/LinkManager.h`, `Comms/LogReplayLink.h`, `Comms/MAVLinkProtocol.h`, `FactSystem/ParameterManager.h`, `MissionManager/PlanMasterController.h`, `MissionManager/MissionManager.h`, and more — all resolved via undefined-symbol lookup against the running executable.

### Documentation

- `plugins/README.md` (developer how-to: submodule workflow, discovery paths, troubleshooting) and `src/PluginSystem/README.md` (architecture + lifecycle). Both are reasonably current; both document the in-tree build as the model.

## 2. The load path, end to end

```
QGCApplication init
  └─ QGCPluginManager::instance()->init()
      └─ QGCPluginLoader::loadPlugins(defaultPluginPaths())
          ├─ scan dirs for *.dll / *.dylib|*.bundle / *.so
          ├─ QPluginLoader::instance()            ← plugin code EXECUTES here
          ├─ qobject_cast<QGCPluginInterface*>    ← interface check
          ├─ pluginInterfaceVersion() == 1        ← version check (after code ran)
          └─ createPlugin(nullptr) → QGCPlugin*
      ├─ PluginSettings::registerPlugin(name())   ← name known only after instantiation
      ├─ disabled? → delete plugin (stays loaded in loader, instance discarded)
      └─ enabled? → init(); collect toolMenuItem / panels / replayExtension /
                    controlsTelemetryLogging into QVariantLists → QML
```

Search paths (`QGCPluginLoader::defaultPluginPaths()`): the executable-adjacent `plugins/` dir (plus `Contents/PlugIns` inside a macOS bundle, plus `../lib/qgroundcontrol/plugins` on Linux) and the per-user `AppDataLocation/plugins` dir.

## 3. Why the current system cannot meet the goal

The goal is: distribute a plugin binary; have it load into multiple QGC versions; never build the app and plugin together. Each part fails today for a structural reason:

### 3.1 Total build coupling via undefined-symbol resolution (the core problem)

Plugins compile against QGC's internal headers and resolve *any* internal symbol from the executable at `dlopen` time. This means a plugin binary depends on:

- the **exact class layouts** of every QGC type it touches (one added data member or virtual in `Vehicle`, `LinkManager`, `QGCPlugin` itself, etc. silently corrupts the plugin — undefined behavior, not a load error);
- the **exact mangled symbol set** of that build (a renamed method or changed signature → unresolved symbol at first call);
- the same compiler family, standard-library ABI, and Qt build.

There is no versioned surface: the "API" is the entire program. A plugin built against commit A has no defined behavior on commit B. This is precisely the compile-time half of the hybrid.

### 3.2 Windows does not work

An `.exe` does not export symbols, and `-export_dynamic` is only applied on `APPLE OR LINUX` (root `CMakeLists.txt`). The example plugin's CMake prints "Windows plugin loading may require additional configuration" — i.e., a plugin DLL with unresolved QGC symbols cannot even be linked on Windows, because PE requires all imports resolved at link time against an import library. **Of the goal's named distribution targets (Windows, macOS, Android), Windows is the one where the current model is not implementable even in matched-build form** without `ENABLE_EXPORTS` on the executable (which then binds the DLL to that exe's symbol set — matched-build again).

### 3.3 Android cannot load external binaries

Android (targetSdk ≥ 29) forbids executing native code from writable app storage, so scanning a plugins directory for downloaded `.so` files is a dead end by OS policy. The current answer (compile plugins into the APK; toggle controls which ones load) is correct as far as it goes, but it means Android "distribution" today = rebuilding the app — the opposite of the goal. ATAK's answer (plugins as separately installed, signature-verified APKs) is the model to adopt; see 02-target-architecture §6.

### 3.4 The interface version is a fiction

`pluginInterfaceVersion()` has returned `1` since the system was created, while the branch history added ABI-breaking virtuals to `QGCPlugin` repeatedly (fly-view panels, then telemetry logging, then plan-view panels — each addition changes the vtable layout every already-built plugin baked in). The check passes while the binary contract it is supposed to guard has changed several times. Nothing records *which* extension points a host build has, and nothing lets a plugin say "I need at least host X."

### 3.5 Metadata requires executing the plugin

The plugin's name — the identity key for the settings system — is only obtainable by loading the code and instantiating the plugin. Consequences: disabled plugins still get their code loaded and their factory run every startup; the settings page cannot show anything about a plugin that failed to load; there is no way to inspect version/vendor/compatibility before running foreign code. Qt already solves this: `Q_PLUGIN_METADATA(... FILE "plugin.json")` embeds JSON readable via `QPluginLoader::metaData()` **without** loading the library. The infrastructure doesn't use it yet.

## 4. Smaller defects found during review

These are worth fixing regardless of the bigger redesign, and several would be flagged in upstream review:

1. **IID mismatch.** `Q_DECLARE_INTERFACE` uses `org.qgroundcontrol.QGCPluginInterface/1.0`, but the example/README/QDrive use `Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCPluginInterface")` — different string, no version suffix. It works only because `QPluginLoader` doesn't check the metadata IID (`QFactoryLoader` would). The IIDs should be one string, version-suffixed, and the loader should check the metadata IID before instantiating.
2. **`-Wl,-export_dynamic` on the executable** exports *every* symbol: binary-size and link-time cost, symbol-collision risk with Qt plugins/GStreamer, and upstream reviewers will question it. It disappears entirely once plugins link against an SDK library instead (02-target-architecture §5).
3. **Docs contradict the code.** `src/PluginSystem/README.md` shows `target_link_libraries(MyPlugin ... QGroundControl)` and says init is called "always, regardless of enabled state"; the real model is link-nothing + dynamic lookup, and disabled plugins are deleted without `init()`. `plugins/README.md`'s macOS user path includes a hardcoded "QGroundControl Daily" while the loader uses `AppDataLocation`. Small, but upstream reviewers read the docs first.
4. **Replay extension is single-slot, first-wins** with no arbitration or diagnostics when two plugins provide one. Acceptable for now; should at least log a warning on the second registration.
5. **`reloadPlugin()` fallback loads every plugin in every directory** to find one, then deletes the others — instances whose libraries stay loaded. Rare path, but it also re-runs foreign code unnecessarily.
6. **Unload does not actually unload the library.** `unloadPlugin()` deletes the `QGCPlugin` instance but never calls `QPluginLoader::unload()`, so "reduces memory footprint" (README) is only true for the instance's allocations, not the mapped library. Also, QML objects created from a plugin's `qrc` resources would dangle if the library ever were truly unloaded — true unload should probably be dropped as a feature (VS Code requires a restart to deactivate an extension for the same reason) rather than made to work.
7. **Security posture is implicit.** The app auto-executes any shared library found in a user-writable directory at startup, with no consent step, no signature hook, and no crash attribution. For a hobbyist fork that's fine; for upstream QGC (used operationally) this needs an explicit story (02-target-architecture §7).
8. **`qgc_add_plugin()` isn't used by the plugins.** Both `example` and `qdrive` hand-roll their CMake instead of calling the helper, so the helper and reality have already drifted (the helper doesn't set the undefined-symbol link options at all). One canonical path should exist.

## 5. What is genuinely good and should be kept

- **The contribution-point pattern.** Contributions flow to the host as `QVariantMap`/`QVariantList` (string-keyed, evolvable without ABI breaks) and QML component URLs. This is exactly the right shape — it's the same late-bound style VS Code uses for `contributes` — and it survives every redesign below.
- **The `QObject*`-based replay interface** deliberately avoids host dependencies on plugin types. Right instinct; generalize it.
- **Core-plugin vs runtime-plugin separation**, the settings/Fact integration, the per-view button-strip + floating-panel UI framework, and the plugin-agnostic replay hooks in the log-replay stack are all real, reusable assets.
- **The auto-discovery build + auto-deploy dev loop** is a good developer experience for the in-tree tier and should remain the way plugin authors iterate.
