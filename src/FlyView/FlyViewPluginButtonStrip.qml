import QtQuick

import QGroundControl
import QGroundControl.Controls

// macOS-style dark translucent dock.
// All plugin entries live inside a single rounded glass panel.
// Clicking a name expands it into a floating panel; the row slides away.
// Minimizing the floating panel slides the row back into place.
Item {
    id: _root

    property var  expandedSet: ({})
    signal expandPlugin(int index)

    property var  _panelItems: QGroundControl.pluginManager.flyViewPanelItems

    // Strip is visible when at least one plugin is not in "expanded" or "popped" state
    property bool _anyCollapsed: {
        var items = _panelItems
        for (var i = 0; i < items.length; i++) {
            if (!expandedSet[items[i].name]) return true
        }
        return false
    }

    readonly property real _dockWidth:    ScreenTools.defaultFontPixelWidth  * 9
    readonly property real _rowHeight:    ScreenTools.defaultFontPixelHeight * 2.4
    readonly property real _headerHeight: ScreenTools.defaultFontPixelHeight * 1.4
    readonly property real _radius:       ScreenTools.defaultFontPixelWidth  * 1.2
    readonly property real _vPad:         ScreenTools.defaultFontPixelHeight * 0.35

    width:   _dock.width
    height:  _dock.height
    visible: _panelItems.length > 0 && _anyCollapsed && !QGroundControl.videoManager.fullScreen

    // ── Glass dock panel ──────────────────────────────────────────────────────
    Rectangle {
        id:           _dock
        width:        _root._dockWidth
        height:       _dockColumn.height + _root._vPad * 2
        color:        Qt.rgba(0.06, 0.06, 0.06, 0.78)
        radius:       _root._radius
        border.color: Qt.rgba(1, 1, 1, 0.1)
        border.width: 1
        clip:         true

        Column {
            id:                _dockColumn
            width:             parent.width
            anchors.top:       parent.top
            anchors.topMargin: _root._vPad

            // ── "PLUGINS" section label ────────────────────────────────────
            Item {
                width:  parent.width
                height: _root._headerHeight

                Text {
                    anchors.centerIn: parent
                    text:             qsTr("PLUGINS")
                    color:            "white"
                    opacity:          0.3
                    font.pointSize:   ScreenTools.smallFontPointSize * 0.8
                    font.bold:        true
                    font.letterSpacing: 1.2
                }
            }

            // Separator below label
            Rectangle {
                x:      ScreenTools.defaultFontPixelWidth * 0.75
                width:  parent.width - ScreenTools.defaultFontPixelWidth * 1.5
                height: 1
                color:  Qt.rgba(1, 1, 1, 0.1)
            }

            // ── Plugin rows ────────────────────────────────────────────────
            Repeater {
                model: _panelItems
                delegate: Item {
                    id:    _delegate
                    width: _root._dockWidth
                    clip:  true

                    property bool _isExpanded: !!_root.expandedSet[modelData.name]

                    height: _isExpanded ? 0 : _root._rowHeight
                    Behavior on height {
                        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                    }

                    // Hover glow
                    Rectangle {
                        anchors.fill:    parent
                        anchors.margins: 3
                        radius:          _root._radius * 0.7
                        color:           "white"
                        opacity:         _ma.containsMouse ? 0.1 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }

                    // Custom dock item provided by the plugin, or plain name fallback
                    Loader {
                        anchors.fill: parent
                        source:       (modelData.dockUrl && modelData.dockUrl !== "") ? modelData.dockUrl : ""
                        active:       modelData.dockUrl && modelData.dockUrl !== ""
                        visible:      active && status === Loader.Ready
                    }

                    Text {
                        anchors.centerIn: parent
                        visible:          !modelData.dockUrl || modelData.dockUrl === ""
                        text:             modelData.name
                        color:            "white"
                        opacity:          0.9
                        font.pointSize:   ScreenTools.smallFontPointSize
                    }

                    MouseArea {
                        id:           _ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    _root.expandPlugin(index)
                    }
                }
            }
        }
    }
}
