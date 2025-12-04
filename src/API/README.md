# QGroundControl Plugin Architecture

This directory contains the core plugin system architecture for QGroundControl.

## Architecture Overview

QGroundControl uses a clear separation between the **Core Plugin** (singleton managing the application) and **Runtime Plugins** (dynamically loaded extensions).

### Core Components

#### `QGCCorePlugin` - Core Application Manager (Singleton)
- **Purpose**: Singleton that manages core QGC functionality and aggregates runtime plugin contributions
- **Responsibilities**:
  - Manages application-wide settings and options
  - Aggregates tool menu items from all loaded plugins
  - Provides default analyze pages and toolbar indicators
  - Handles MAVLink message routing
  - Manages custom map items and video receivers
- **Key Properties**:
  - `toolMenuItems` - Aggregated list of all tool menu items (from core + plugins)
  - `loadedPlugins` - List of currently loaded runtime plugins
  - `analyzePages` - Application analyze pages
  - `options` - Global QGC options

#### `QGCPlugin` - Runtime Plugin Base Class
- **Purpose**: Base class for dynamically loaded runtime plugins
- **Responsibilities**:
  - Provide tool menu items to extend the Tools menu
  - Initialize plugin-specific functionality
  - Clean up on plugin unload
- **Key Methods**:
  - `name()` - Human-readable plugin name
  - `toolMenuItem()` - Single menu item this plugin contributes
  - `init()` / `cleanup()` - Lifecycle management

#### `QGCPluginInterface` - Plugin Loading Interface
- **Purpose**: Qt Plugin Interface for loading runtime plugins
- **Current Version**: 1.0
- **Methods**:
  - `pluginInterfaceVersion()` - Must return 1
  - `createPlugin()` - Factory method returning `QGCPlugin*`

#### `QGCPluginLoader` - Plugin Discovery & Loading
- **Purpose**: Discovers and loads plugins from filesystem
- **Features**:
  - Scans multiple plugin directories
  - Platform-specific library loading (.dll, .dylib, .so)
  - Version compatibility checking
  - Plugin validation
- **Default Search Paths**:
  - Application directory: `<app>/plugins/`
  - macOS: `<app.app>/Contents/PlugIns/`
  - User data: `~/.local/share/QGroundControl/plugins/` (Linux)

## Plugin Lifecycle

```
1. Application Startup
   └── QGCCorePlugin::init()
       └── QGCCorePlugin::_loadPlugins()
           ├── QGCPluginLoader scans plugin directories
           ├── Loads .so/.dylib/.dll files
           ├── Validates QGCPluginInterface version 1
           └── Creates QGCPlugin instances

2. Plugin Initialization
   └── QGCCorePlugin::_loadPlugins() (continued)
       ├── Registers each plugin with PluginSettings
       ├── Calls plugin->init() on all plugins (always, regardless of enabled state)
       ├── Collects plugin->toolMenuItem()
       ├── Adds pluginName property to menu item
       └── Aggregates items to QGCCorePlugin::_toolMenuItems

3. Runtime
   └── Tool menu items visibility:
       ├── Bound to PluginSettings Facts
       ├── Changes take effect immediately
       └── No restart required

4. Application Shutdown
   └── QGCCorePlugin::cleanup()
       ├── Calls plugin->cleanup() on each plugin
       └── Deletes plugin instances
```

## Creating a Plugin

### 1. Plugin Structure

```cpp
// MyPlugin.h
#include "API/QGCPlugin.h"
#include "API/QGCCorePluginInterface.h"

// Factory (implements Qt Plugin Interface)
class MyPlugin : public QObject, public QGCPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCPluginInterface")
    Q_INTERFACES(QGCPluginInterface)

public:
    int pluginInterfaceVersion() const override { return 1; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

// Runtime Plugin (your actual plugin logic)
class MyRuntimePlugin : public QGCPlugin
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit MyRuntimePlugin(QObject* parent = nullptr);
    
    // QGCPlugin interface
    QString name() const override { return "MyPlugin"; }
    QVariantMap toolMenuItem() const override;

private:
    QVariantMap _toolMenuItem;
};
```

### 2. Implementation

```cpp
// MyPlugin.cc
#include "MyPlugin.h"

QGCPlugin* MyPlugin::createPlugin(QObject* parent) {
    return new MyRuntimePlugin(parent);
}

MyRuntimePlugin::MyRuntimePlugin(QObject* parent)
    : QGCPlugin(parent)
{
    // Create tool menu item
    _toolMenuItem["title"] = "My Plugin";
    _toolMenuItem["icon"] = "/res/icon.svg";
    _toolMenuItem["source"] = "qrc:/qml/MyPluginView.qml";
    _toolMenuItem["visible"] = true;
}

QVariantMap MyRuntimePlugin::toolMenuItem() const {
    return _toolMenuItem;
}
```

### 3. CMakeLists.txt

```cmake
add_library(MyPlugin SHARED
    MyPlugin.h
    MyPlugin.cc
    # QML resources
    MyPlugin.qrc
)

target_link_libraries(MyPlugin
    PRIVATE
        Qt6::Core
        Qt6::Quick
        QGroundControl  # Link to main app for API access
)

install(TARGETS MyPlugin
    LIBRARY DESTINATION plugins
)
```

## Tool Menu Item

Each plugin provides a single tool menu item as a QVariantMap with the following keys:

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `title` | String | Yes | Display name in Tools menu |
| `icon` | String | No | Path to icon resource |
| `source` | String | Yes | QML file path (qrc:/qml/...) |
| `toolbarSource` | String | No | Custom toolbar QML |
| `pluginName` | String | Auto | Plugin name (added automatically by system) |

**Note**: The `pluginName` property is automatically added by the plugin loading system and is used to bind menu item visibility to the plugin's enabled state in PluginSettings.

## Settings Integration

Plugins are automatically integrated with the `PluginSettings` system:

**Automatic Registration:**
- Each plugin is registered using its `name()` as the identifier
- A Fact is created for the plugin's enabled state
- Settings persist across application restarts

**User Control:**
- Users enable/disable plugins in Application Settings → Plugins
- Changes take effect immediately (no restart required)
- Menu items automatically show/hide based on enabled state

**Implementation:**
```cpp
// Plugins don't need to check enabled state
MyRuntimePlugin::MyRuntimePlugin(QObject* parent)
    : QGCPlugin(parent)
{
    // Just provide menu item - visibility is automatic
    _toolMenuItem["title"] = "My Plugin";
    _toolMenuItem["source"] = "qrc:/qml/MyView.qml";
}
```

**Default States:**
- Example plugin: Disabled by default
- All other plugins: Enabled by default

**Technical Details:**
- Settings stored in `Plugins` group with plugin name as key
- Uses Fact system for type-safety and validation
- QML binds menu visibility directly to Fact values

## QML Integration

Plugins can:
- Register QML types: `qmlRegisterType<MyType>("QGroundControl.MyPlugin", 1, 0, "MyType")`
- Register singletons: `qmlRegisterSingletonType<MySingleton>(...)`
- Provide QML views via `source` in tool menu items
- Access QGC globals via `QGroundControl` singleton

## Architecture

**Current Design** (Version 1):
- Core Plugin: `QGCCorePlugin` (singleton, manages core + aggregates plugins)
- Runtime Plugins: `QGCPlugin` (individual loaded plugins)
- Interface: `QGCPluginInterface` (returns `QGCPlugin*`)
- Clear separation: one core, many runtime plugins

## Examples

See `/plugins/` directory for complete examples:
- `example/` - Minimal plugin with tool menu item
- `qdrive/` - Full-featured plugin with AWS integration

