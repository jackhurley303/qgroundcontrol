import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id: _root

    spacing: ScreenTools.defaultFontPixelHeight * 0.4

    property var replay     // QGroundControl.pluginManager.replayExtension
    property var controller // LogReplayLinkController

    // ── Computed state ────────────────────────────────────────────────────

    readonly property bool _durationKnown: replay !== null && replay.videoDurationMs > 0

    readonly property real _tlogDurationMs: {
        var s = replay ? replay.totalTime : (controller ? controller.totalTime : "")
        var p = s.split(":")
        return (p.length === 3)
            ? (parseInt(p[0]) * 3600 + parseInt(p[1]) * 60 + parseInt(p[2])) * 1000
            : 0
    }

    readonly property real _tlogDurationSecs: _tlogDurationMs / 1000

    readonly property real _tlogStartFraction: _durationKnown
        ? -replay.videoOffsetSecs * 1000 / replay.videoDurationMs : 0
    readonly property real _tlogWidthFraction: (_durationKnown && _tlogDurationMs > 0)
        ? _tlogDurationMs / replay.videoDurationMs : 0
    readonly property real _tlogEndFraction: _tlogStartFraction + _tlogWidthFraction
    // Not clamped — playhead exits the bar when tlog is outside video range
    readonly property real _playheadFraction: _durationKnown
        ? replay.videoPositionMs / replay.videoDurationMs : 0

    // ── Helpers ───────────────────────────────────────────────────────────

    // [H:]M:SS format — hours omitted when zero
    function formatVideoMs(ms) {
        var t  = Math.floor(ms / 1000)
        var h  = Math.floor(t / 3600)
        var m  = Math.floor((t % 3600) / 60)
        var s  = t % 60
        var mm = (h > 0 && m < 10) ? "0" + m : String(m)
        var ss = s < 10 ? "0" + s : String(s)
        return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
    }

    // [H:]M:SS.cc signed format — hours omitted when zero
    function formatOffsetSecs(secs) {
        var sign  = secs < 0 ? "-" : "+"
        var abs   = Math.abs(secs)
        var h     = Math.floor(abs / 3600)
        var m     = Math.floor((abs % 3600) / 60)
        var s     = abs % 60
        var mm    = (h > 0 && m < 10) ? "0" + m : String(m)
        var sFull = s.toFixed(2)
        var ss    = parseFloat(sFull) < 10 ? "0" + sFull : sFull
        return sign + (h > 0 ? h + ":" + mm + ":" + ss : m + ":" + ss)
    }

    QGCPalette { id: qgcPal }

    // ── "Video Offset" title ──────────────────────────────────────────────

    QGCLabel {
        text:      qsTr("Video Offset")
        font.bold: true
    }

    // ── Nudge/Reset + Slider grouped tightly in their own column ─────────
    //
    // Nested ColumnLayout with spacing: 2 keeps the button row and slider
    // visually attached regardless of the outer column's spacing.

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: ScreenTools.defaultFontPixelHeight * 0.4
        spacing:          0

        RowLayout {
            Layout.fillWidth: true

            // Nudge buttons on the left — step offset by ±0.5 s
            Repeater {
                model: [-0.5, 0.5]

                Rectangle {
                    required property real modelData
                    width:        nudgeLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
                    height:       ScreenTools.defaultFontPixelHeight * 1.3
                    radius:       3
                    enabled:      _durationKnown
                    color:        enabled ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    border.color: enabled ? qgcPal.text : qgcPal.colorGrey
                    border.width: 1

                    QGCLabel {
                        id:               nudgeLabel
                        anchors.centerIn: parent
                        text:             modelData < 0 ? "− 0.5s" : "+ 0.5s"
                        font.pointSize:   ScreenTools.smallFontPointSize
                        color:            parent.enabled ? qgcPal.text : qgcPal.colorGrey
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled:      parent.enabled
                        cursorShape:  parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked:    if (replay) replay.adjustVideoOffset(parent.modelData)
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Reset button on the right
            Rectangle {
                id:           resetBtn
                width:        resetBtnLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
                height:       ScreenTools.defaultFontPixelHeight * 1.3
                radius:       3
                enabled:      _durationKnown && replay !== null && replay.videoOffsetSecs !== 0
                color:        enabled ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                border.color: enabled ? qgcPal.text : qgcPal.colorGrey
                border.width: 1

                QGCLabel {
                    id:               resetBtnLabel
                    anchors.centerIn: parent
                    text:             qsTr("Reset")
                    font.pointSize:   ScreenTools.smallFontPointSize
                    color:            parent.enabled ? qgcPal.text : qgcPal.colorGrey
                }

                MouseArea {
                    anchors.fill: parent
                    enabled:      parent.enabled
                    cursorShape:  parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked:    if (replay) replay.adjustVideoOffset(-replay.videoOffsetSecs)
                }
            }
        }

        // Offset slider + floating value label below
        //
        // value is always set via Qt.callLater so it runs after from/to bindings
        // have settled. Triggers cover: initial creation, replay assignment,
        // videoOffsetSecs changes, and duration loads that change from/to.
        Item {
            id:               sliderItem
            Layout.fillWidth: true
            implicitHeight:   offsetSlider.implicitHeight + offsetLabel.implicitHeight

            function _syncValue() {
                if (replay && !offsetSlider.pressed)
                    offsetSlider.value = replay.videoOffsetSecs
            }

            // Initial creation — replay may already be set with its final value
            Component.onCompleted: Qt.callLater(_syncValue)

            // replay property itself replaced (e.g. new log opened)
            Connections {
                target:                    _root
                function onReplayChanged() { Qt.callLater(sliderItem._syncValue) }
            }

            // replay property changes that affect value or from/to
            Connections {
                target:                              replay
                function onVideoOffsetSecsChanged()  { Qt.callLater(sliderItem._syncValue) }
                function onVideoDurationMsChanged()  { Qt.callLater(sliderItem._syncValue) }
                function onTotalTimeChanged()         { Qt.callLater(sliderItem._syncValue) }
            }

            Slider {
                id:            offsetSlider
                anchors.top:   parent.top
                anchors.left:  parent.left
                anchors.right: parent.right
                enabled:       _durationKnown
                from:          _durationKnown ? -(replay.videoDurationMs / 1000) : -1
                to:            _tlogDurationSecs > 0 ? _tlogDurationSecs : 1

                onMoved: {
                    if (replay) replay.adjustVideoOffset(value - replay.videoOffsetSecs)
                }
            }

            // Hand cursor only over the draggable handle circle
            Item {
                x:      offsetSlider.handle.x
                y:      offsetSlider.handle.y
                width:  offsetSlider.handle.width
                height: offsetSlider.handle.height

                HoverHandler {
                    cursorShape: Qt.OpenHandCursor
                }
            }

            QGCLabel {
                id:             offsetLabel
                y:              offsetSlider.height
                text:           formatOffsetSecs(offsetSlider.value)
                font.pointSize: ScreenTools.defaultFontPointSize
                x: {
                    var center = offsetSlider.handle.x + offsetSlider.handle.width / 2
                    return Math.max(0, Math.min(parent.width - implicitWidth, center - implicitWidth / 2))
                }
            }
        }
    }

    // ── "Video Timeline" title row + current/total + Play icon ────────────

    RowLayout {
        Layout.fillWidth: true
        spacing: ScreenTools.defaultFontPixelWidth * 0.5

        QGCLabel {
            text:      qsTr("Video Timeline")
            font.bold: true
        }

        Item { Layout.fillWidth: true }

        QGCLabel {
            text: {
                if (!replay || !_durationKnown) return "— / —"
                var cur = _playheadFraction < 0 ? "0:00:00"
                        : _playheadFraction > 1 ? formatVideoMs(replay.videoDurationMs)
                        : formatVideoMs(replay.videoPositionMs)
                return cur + " / " + formatVideoMs(replay.videoDurationMs)
            }
            font.pointSize: ScreenTools.smallFontPointSize
        }

        // Play/Pause icon button
        Item {
            id:      playBtn
            width:   ScreenTools.defaultFontPixelHeight * 1.4
            height:  width
            enabled: _durationKnown

            Text {
                anchors.centerIn: parent
                text:             (controller && controller.isPlaying) ? "⏸" : "▶"
                font.pixelSize:   Math.round(parent.height * 0.8)
                color:            parent.enabled ? qgcPal.text : qgcPal.colorGrey
            }

            MouseArea {
                anchors.fill: parent
                enabled:      parent.enabled
                cursorShape:  parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked:    if (controller) controller.isPlaying = !controller.isPlaying
            }
        }
    }

    // ── Timeline bar + playhead arrow ──────────────────────────────────────
    //
    // Bar fills the wrapper. Red line lives inside the bar so clip:true hides
    // it when the playhead is out of [0,1]. The ▲ arrow overflows below the
    // bar and may slightly overlap the time labels (z:1 keeps it on top).

    Item {
        id:               timelineWrapper
        Layout.fillWidth: true
        z:                1  // overflowing arrow renders above sibling RowLayout

        readonly property int _arrowPx: Math.round(ScreenTools.defaultFontPixelHeight * 0.9)
        readonly property int _barHeight: Math.round(ScreenTools.defaultFontPixelHeight * 2.8)

        height: _barHeight  // arrow overflows below; labels stay in place

        Rectangle {
            id:     timelineBar
            anchors.fill: parent
            color:  qgcPal.windowShade
            radius: 0
            clip:   true  // clips tlog rect when it extends beyond bar bounds

            // Tlog region — inset so outline sits inside rails; bottom clears rail by 1px
            Rectangle {
                id:           tlogRect
                visible:      _tlogDurationMs > 0
                x:            _tlogStartFraction * timelineBar.width
                y:            2.5
                width:        _tlogWidthFraction  * timelineBar.width
                height:       timelineBar.height - 5
                radius:       8
                color:        Qt.rgba(qgcPal.brandingBlue.r, qgcPal.brandingBlue.g, qgcPal.brandingBlue.b, 0.3)
                border.color: qgcPal.brandingBlue
                border.width: 1.5

                QGCLabel {
                    anchors.top:        parent.top
                    anchors.left:       parent.left
                    anchors.topMargin:  3
                    anchors.leftMargin: 6
                    text:               qsTr("tlog")
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              "white"
                    font.bold:          true
                }
            }

            // Top rail
            Rectangle {
                anchors.top:   parent.top
                anchors.left:  parent.left
                anchors.right: parent.right
                height:        2
                color:         Qt.rgba(1, 1, 1, 0.5)
            }

            // Bottom rail
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left:   parent.left
                anchors.right:  parent.right
                height:         2
                color:          Qt.rgba(1, 1, 1, 0.5)
            }
        }

        // Red line spanning bar + arrow so they appear attached; visibility guards the out-of-range case
        Rectangle {
            visible: _durationKnown && _playheadFraction >= 0 && _playheadFraction <= 1
            x:       _playheadFraction * timelineBar.width - 1
            y:       0
            width:   2
            height:  timelineWrapper._barHeight + timelineWrapper._arrowPx
            color:   qgcPal.colorRed
            opacity: 0.9
        }

        // ▲ arrow below bar — only visible when playhead is within bar range
        Text {
            visible:        _durationKnown && _playheadFraction >= 0 && _playheadFraction <= 1
            text:           "▲"
            color:          "white"
            font.pixelSize: timelineWrapper._arrowPx
            x:              _playheadFraction * timelineBar.width - implicitWidth / 2
            y:              timelineWrapper._barHeight
        }
    }

    // ── Labels below bar ─────────────────────────────────────────────────

    RowLayout {
        Layout.fillWidth: true
        opacity:          0.5

        QGCLabel {
            text:           formatVideoMs(0)
            font.pointSize: ScreenTools.smallFontPointSize
        }

        Item { Layout.fillWidth: true }

        QGCLabel {
            text:           replay ? formatVideoMs(replay.videoDurationMs) : "—"
            font.pointSize: ScreenTools.smallFontPointSize
        }
    }
}
