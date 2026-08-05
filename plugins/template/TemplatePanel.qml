import QtQuick
import QtQuick.Layouts

import QGroundControl.PluginUI

// Fly-view panel content for the SDK template plugin.
//
// This component is loaded inside the floating panel managed by the host's
// FlyViewPluginPanel — the panel itself provides the header, drag, and resize, so
// this file only needs the plugin's actual UI. It imports QGroundControl.PluginUI,
// not QGroundControl.Controls / QGroundControl: those host-internal modules are not
// part of the published SDK, so an out-of-tree plugin cannot resolve them at
// lint time (see SDK-README.md's "QML vocabulary" section).
ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight * 0.5

    QGCLabel {
        Layout.fillWidth:    true
        text:                qsTr("Template Plugin")
        font.bold:           true
        horizontalAlignment: Text.AlignHCenter
    }

    Rectangle {
        Layout.fillWidth:       true
        Layout.preferredHeight: 1
        color:                  Qt.rgba(1, 1, 1, 0.15)
    }

    QGCLabel {
        Layout.fillWidth:    true
        text:                qsTr("Replace this content with your plugin's fly-view UI.")
        font.pointSize:      ScreenTools.smallFontPointSize
        wrapMode:            Text.WordWrap
        opacity:             0.7
        horizontalAlignment: Text.AlignHCenter
    }

    Item { Layout.fillHeight: true }
}
