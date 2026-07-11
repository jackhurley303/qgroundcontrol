# QGC Core Plugin API

This directory contains the **Core Plugin** — the singleton managing core application
behavior. It is distinct from the **runtime plugin system** (dynamically loaded plugins
managed by `QGCPluginManager`), which is documented in
[src/PluginSystem/README.md](../PluginSystem/README.md) (architecture: loader, manager,
manifest, contributions) and [plugins/README.md](../../plugins/README.md) (plugin
author's guide: manifest schema, CMake pattern, settings behavior).

## `QGCCorePlugin` — Core Application Manager (Singleton)

- **Purpose**: Singleton that manages core QGC functionality
- **Responsibilities**:
  - Manages application-wide settings and options (`QGCOptions`)
  - Provides default analyze pages and toolbar indicators
  - Handles MAVLink message routing
  - Manages custom map items and video receivers
- **Key Properties**:
  - `analyzePages` - Application analyze pages
  - `options` - Global QGC options
  - `toolbarIndicators` - Custom toolbar indicator components

`QmlComponentInfo` describes a QML component (title + URL + icon) exposed to the UI,
used for analyze pages and similar core-plugin-provided views.

Custom builds override `QGCCorePlugin` to customize the application; see
[custom-example/](../../custom-example/) for the pattern.
