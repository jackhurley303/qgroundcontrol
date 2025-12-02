# Example Plugin

Demonstrates the QGC plugin architecture with a minimal working example.

## Building

**The plugin builds automatically as part of QGC** when the `plugins/` directory exists.

From the QGC root directory:

```bash
# Build QGC (includes plugins)
cmake --build build --config Debug

# Or build just the plugin
cmake --build build --config Debug --target ExamplePlugin
```

### Quick rebuild script

Use the provided build script for rapid iteration:

```bash
# macOS/Linux
./plugins/example/build.sh

# Windows
plugins\example\build.bat
```

The script automatically:
- Forces a rebuild by touching source files
- Builds only the plugin target
- Copies the plugin to the correct user directory
- Works on macOS, Linux, and Windows

## Installation

The build script automatically deploys to:
- **macOS**: `~/Library/Application Support/QGroundControl/QGroundControl Daily/plugins/`
- **Linux**: `~/.local/share/QGroundControl/QGroundControl Daily/plugins/`
- **Windows**: `%LOCALAPPDATA%\QGroundControl\QGroundControl Daily\plugins\`

Or manually copy from:
- `build/Debug/plugins/libExamplePlugin.dylib` (macOS)
- `build/Debug/plugins/libExamplePlugin.so` (Linux)
- `build\Debug\plugins\ExamplePlugin.dll` (Windows)

## What it does

- Adds "Example Plugin" menu item to the Tools menu
- Shows a simple info page when clicked
- Demonstrates plugin interface implementation
- Shows how to use Qt resources in plugins
- **Demonstrates custom toolbar support for plugin pages**

## Custom Toolbar Feature

This plugin demonstrates the custom toolbar capability for plugins. Plugins can provide their own toolbar UI by specifying a `toolbarSource` in their `toolMenuItems`:

```cpp
// In ExampleCorePlugin constructor
QVariantMap item;
item["title"] = "Example Plugin";
item["icon"] = "/res/QGCLogoFull.svg";
item["source"] = "qrc:/qml/ExamplePluginView.qml";
item["toolbarSource"] = "qrc:/qml/ExampleToolBar.qml";  // Custom toolbar
item["visible"] = true;
```

### Custom Toolbar Requirements

Your custom toolbar QML must:
1. Have a `property string toolTitle` to receive the tool name from MainWindow
2. Have a `signal exitRequested()` that will be called when the user wants to exit

Example toolbar structure:
```qml
Rectangle {
    property string toolTitle: ""
    signal exitRequested()
    
    // Your custom toolbar UI here
    // Call exitRequested() when user clicks back/exit button
}
```

See `ExampleToolBar.qml` for a complete example with custom buttons and layout.

## Architecture

- `ExamplePlugin.h` - Plugin factory implementing `QGCCorePluginInterface`
- `ExamplePlugin.cc` - Factory implementation that creates plugin instance
- `ExamplePluginView.qml` - Plugin UI (loaded from Qt resources)
- `ExamplePlugin.qrc` - Qt resource file bundling QML
- `CMakeLists.txt` - Build configuration with platform-specific linker flags
- `build.sh` / `build.bat` - Cross-platform build scripts
