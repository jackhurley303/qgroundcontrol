/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls

import QGroundControl.PluginUI

/**
 * @brief Custom toolbar for the Example plugin
 * 
 * This demonstrates how plugins can provide their own toolbar UI.
 * The toolbar must provide:
 * - A property called 'toolTitle' to receive the tool name
 * - A signal called 'exitRequested()' that will be called to exit the tool
 */
Rectangle {
    id:     _root
    width:  parent.width
    height: ScreenTools.toolbarHeight
    color:  qgcPal.toolbarBackground

    // Properties that MainWindow will bind to
    property string toolTitle: ""
    signal exitRequested()

    QGCPalette { id: qgcPal }

    QGCLabel {
        anchors.left:           parent.left
        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth
        anchors.verticalCenter: parent.verticalCenter
        font.pointSize:         ScreenTools.largeFontPointSize
        text:                   "< " + _root.toolTitle

        QGCMouseArea {
            fillItem:   parent
            onClicked:  _root.exitRequested()
        }
    }
}
