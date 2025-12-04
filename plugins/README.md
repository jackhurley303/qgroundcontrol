# QGroundControl Plugins

This directory contains QGC plugins that extend core functionality.

## Plugin Architecture

QGC uses a dynamic plugin system that allows loading custom functionality at runtime:

- **Plugins are built in-tree** with QGC (access to full API)
- **Maintained as separate repos** (vendor independence)
- **Integrated via git submodules** (guaranteed compatibility)
- **Loaded at runtime** via Qt's plugin system
- **Extend core via virtual methods** (tool menus, analyze pages, etc.)
- **Cross-platform** support (macOS, Linux, Windows)

See [plugin architecture documentation](../AGENTS.md) for details.

## Creating a New Plugin

### Recommended: Separate Repository + Submodule

This is the preferred approach for vendor/third-party plugins:

```bash
# 1. Create your plugin in its own repository
mkdir qgc-myplugin
cd qgc-myplugin
git init

# 2. Copy example plugin as template
cp -r /path/to/qgroundcontrol/plugins/example/* .
# Rename files: ExamplePlugin* → MyPlugin*

# 3. Commit to your repo
git add .
git commit -m "Initial plugin structure"
git remote add origin https://github.com/yourorg/qgc-myplugin.git
git push -u origin main

# 4. Add as submodule to QGC fork
cd /path/to/your-qgc-fork
git submodule add https://github.com/yourorg/qgc-myplugin.git plugins/myplugin
git commit -m "Add myplugin submodule"

# 5. Build with QGC
cmake --build build --target MyPlugin
```

**Benefits:**
- ✅ Your plugin lives in your own repository
- ✅ Independent versioning and releases
- ✅ Still builds with QGC for compatibility
- ✅ Full access to QGC internals
- ✅ No SDK maintenance needed

### Alternative: In-Tree Development

For rapid prototyping or internal plugins:

1. Create directory: `plugins/yourplugin/`
2. Copy structure from `example/`
3. Modify files for your use case
4. Build: `cmake --build build --target YourPlugin`

The build system auto-discovers plugins in subdirectories.

## Building Plugins

### Build All Plugins

```bash
# From QGC root
cmake --build build --config Debug
```

Plugins are automatically discovered and built.

### Build Specific Plugin

```bash
cmake --build build --config Debug --target ExamplePlugin
```

### Plugin Locations After Build

- **macOS**: `build/Debug/plugins/` (or `build/Qt_*_for_macOS-Debug/Debug/plugins/`)
- **Linux**: `build/Debug/plugins/`
- **Windows**: `build\Debug\plugins\`

## Plugin Discovery

QGC searches for plugins in:

**macOS**:
- `QGroundControl.app/Contents/PlugIns/`
- `~/Library/Application Support/QGroundControl/QGroundControl Daily/plugins/`

**Linux**:
- `/path/to/qgroundcontrol/plugins/`
- `~/.local/share/QGroundControl/plugins/`

**Windows**:
- `C:\Program Files\QGroundControl\plugins\`
- `%LOCALAPPDATA%\QGroundControl\plugins\`

## Plugin Interface

All plugins must implement:

```cpp
class MyPlugin : public QObject, public QGCPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.mavlink.qgroundcontrol.QGCPluginInterface")
    Q_INTERFACES(QGCPluginInterface)
    
public:
    int pluginInterfaceVersion() const override { return 1; }
    QGCPlugin* createPlugin(QObject* parent) override;
};

class MyRuntimePlugin : public QGCPlugin {
    Q_OBJECT
    
public:
    QString name() const override { return "MyPlugin"; }
    QVariantMap toolMenuItem() const override;
};
```

**Note**: Current interface version is 1.

## Plugin Settings

Plugins are automatically registered with the `PluginSettings` system, which provides:

- **Enable/Disable Control**: Users can toggle plugins on/off in Application Settings → Plugins
- **Persistent State**: Settings are stored as Facts (type-safe, validated)
- **Dynamic Visibility**: Changes take effect immediately without restart
- **Default State**: Example plugin is disabled by default; all others enabled

Plugins simply provide their menu items - visibility is controlled automatically:

```cpp
MyRuntimePlugin::MyRuntimePlugin(QObject* parent)
    : QGCPlugin(parent)
{
    _toolMenuItem["title"] = "My Plugin";
    _toolMenuItem["icon"] = "/res/icon.svg";
    _toolMenuItem["source"] = "qrc:/qml/MyPluginView.qml";
    // Visibility is automatically controlled by PluginSettings
}
```

**No manual settings code needed** - the plugin system handles it automatically.

## Example Plugin

See `example/` directory for a minimal working plugin that:
- Adds "Example Plugin" to Tools menu
- Shows custom QML view
- Demonstrates resource bundling
- Includes build scripts for rapid iteration

## Development Workflow

### For Submodule Plugins

1. **Clone QGC with submodules**:
   ```bash
   git clone --recurse-submodules https://github.com/yourorg/qgroundcontrol.git
   # Or if already cloned:
   git submodule update --init --recursive
   ```

2. **Make changes in plugin submodule**:
   ```bash
   cd plugins/yourplugin
   # Make changes, commit to plugin repo
   git add .
   git commit -m "Add feature"
   git push
   ```

3. **Update submodule reference in QGC**:
   ```bash
   cd ../..  # Back to QGC root
   git add plugins/yourplugin
   git commit -m "Update yourplugin to latest"
   ```

4. **Build and test**:
   ```bash
   cmake --build build --target YourPlugin
   # Or rebuild all
   cmake --build build
   ```

### For In-Tree Plugins

1. **Make changes** directly in `plugins/yourplugin/`
2. **Build** with CMake target
3. **Deploy** to user plugin directory (or use build scripts)
4. **Restart QGC** to load changes
5. **Iterate** - build scripts automate rebuild/deploy

## Tips

- **Submodules**: Use separate repos for vendor plugins (independence + compatibility)
- **Build scripts**: Use `build.sh`/`build.bat` for rapid iteration during development
- **Full API access**: Plugins have complete access to QGC internals
- **Debugging**: Set `QGC_LOG_VERBOSE=1` to see plugin loading messages
- **Interface versioning**: Keep interface version stable, breaking changes affect all plugins
- **Cross-platform**: Test on macOS, Linux, and Windows (symbol resolution differs)
- **Submodule updates**: Remember to commit submodule reference changes in main repo

## Troubleshooting

**Plugin not loading:**
- Check logs for errors: `qCDebug(QGCPluginLoaderLog)`
- Verify interface version is `1`
- Ensure plugin file has correct extension (.dylib/.so/.dll)
- Check plugin is in search paths

**Plugin menu item not visible:**
- Go to Application Settings → Plugins
- Check if plugin is enabled (toggle to enable)
- Changes take effect immediately - no restart needed

**Build errors:**
- Verify CMakeLists.txt includes all source files
- Check include paths point to `${CMAKE_SOURCE_DIR}/src/API`
- Ensure `AUTOMOC` and `AUTORCC` are enabled

**Runtime crashes:**
- Don't access `activeVehicle()` without null-check
- Wait for `parametersReady` before accessing Facts
- Use defensive coding patterns (see coding guidelines)

## Resources

- [QGC Plugin API](../src/API/)
- [Example Plugin](example/)
- [QGC Coding Standards](../.github/copilot-instructions.md)
- [Qt Plugin Documentation](https://doc.qt.io/qt-6/plugins-howto.html)
