#!/bin/bash
# Build and deploy the Example Plugin (macOS/Linux)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QGC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macOS"
    BUILD_DIR="$QGC_ROOT/build/Qt_6_10_0_for_macOS-Debug"
    PLUGIN_DEST="$HOME/Library/Application Support/QGroundControl/QGroundControl Daily/plugins"
    PLUGIN_EXT=".dylib"
    CPU_COUNT=$(sysctl -n hw.ncpu)
else
    PLATFORM="Linux"
    BUILD_DIR="$QGC_ROOT/build"
    PLUGIN_DEST="$HOME/.local/share/QGroundControl/QGroundControl Daily/plugins"
    PLUGIN_EXT=".so"
    CPU_COUNT=$(nproc)
fi

echo "Building Example Plugin for $PLATFORM..."

# Touch QML file to force rebuild
touch "$SCRIPT_DIR/ExamplePluginView.qml"

# Build the plugin
cd "$QGC_ROOT"
cmake --build "$BUILD_DIR" --config Debug --target ExamplePlugin -j$CPU_COUNT

# Copy to plugin directory
echo "Deploying plugin..."
mkdir -p "$PLUGIN_DEST"
cp "$BUILD_DIR/Debug/plugins/libExamplePlugin${PLUGIN_EXT}" "$PLUGIN_DEST/"

echo "✓ Plugin built and deployed successfully!"
echo "  Plugin location: $PLUGIN_DEST/libExamplePlugin${PLUGIN_EXT}"
echo "  Restart QGroundControl to load the updated plugin."
