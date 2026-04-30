import QtQuick
import QtQuick.Controls
import QtMultimedia

import QGroundControl
import QGroundControl.Controls

Item {
    anchors.fill: parent
    clip:         true

    readonly property var  _replay:    QGroundControl.pluginManager.replayExtension
    readonly property bool _isLoading: videoPlayer.mediaStatus === MediaPlayer.NoMedia
                                    || videoPlayer.mediaStatus === MediaPlayer.LoadingMedia
    readonly property bool _isPreStart: _replay !== null && _replay.videoPositionMs < 0

    property bool _videoEnded:    false
    property real _prevPositionMs: 0

    readonly property bool _durationKnown: _replay !== null && _replay.videoDurationMs > 0
    readonly property bool _isPostEnd: !_isPreStart && _replay !== null && (
        _durationKnown ? _replay.videoPositionMs > _replay.videoDurationMs : _videoEnded
    )

    MediaPlayer {
        id:           videoPlayer
        source:       _replay ? _replay.videoUrl : ""
        videoOutput:  videoOutput
        playbackRate: _replay ? _replay.playbackSpeed : 1.0
        onSourceChanged:      { _videoEnded = false; _prevPositionMs = 0 }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) _videoEnded = true
    }

    Rectangle {
        anchors.fill: parent
        color:        "black"
    }

    Image {
        anchors.fill: parent
        source:       "/res/NoVideoBackground.jpg"
        fillMode:     Image.PreserveAspectCrop
        visible:      _isLoading

        Rectangle {
            anchors.centerIn: parent
            width:            noVideoLabel.contentWidth + ScreenTools.defaultFontPixelHeight
            height:           noVideoLabel.contentHeight + ScreenTools.defaultFontPixelHeight
            radius:           ScreenTools.defaultFontPixelWidth / 2
            color:            "black"
            opacity:          0.5
        }

        QGCLabel {
            id:              noVideoLabel
            anchors.centerIn: parent
            text:            qsTr("LOADING VIDEO")
            font.bold:       true
            color:           "white"
        }
    }

    VideoOutput {
        id:           videoOutput
        anchors.fill: parent
        fillMode:     VideoOutput.PreserveAspectFit
        visible:      !_isPreStart && !_isPostEnd
    }

    QGCLabel {
        anchors.centerIn: parent
        visible:          _isPreStart
        text:             _durationKnown
                            ? qsTr("Video starts in %1s").arg(Math.ceil(-_replay.videoPositionMs / 1000))
                            : qsTr("Waiting for video...")
        font.bold:        true
        color:            "white"
    }

    QGCLabel {
        anchors.centerIn: parent
        visible:          _isPostEnd
        text:             _durationKnown
                            ? qsTr("%1s since video ended").arg(Math.floor((_replay.videoPositionMs - _replay.videoDurationMs) / 1000))
                            : qsTr("Video ended")
        font.bold:        true
        color:            "white"
    }

    function _syncVideoState(posMs, forceSeek) {
        if (_isPreStart || _isPostEnd) {
            if (videoPlayer.playbackState === MediaPlayer.PlayingState) videoPlayer.pause()
            return
        }

        // After EndOfMedia the player enters StoppedState and can no longer render frames.
        // play() restarts the decoder so that subsequent position changes decode and display frames.
        if (videoPlayer.playbackState === MediaPlayer.StoppedState) videoPlayer.play()

        if (_replay.isPlaying) videoPlayer.play()
        else                   videoPlayer.pause()

        if (forceSeek || Math.abs(videoPlayer.position - posMs) > 1000)
            videoPlayer.position = posMs
    }

    Connections {
        target:  _replay
        enabled: _replay !== null
        function onIsActiveChanged() {
            if (!_replay.isActive) videoPlayer.stop()
        }
        function onIsPlayingChanged() {
            if (_replay.isPlaying) videoPlayer.play()
            else videoPlayer.pause()
        }
        function onVideoOffsetSecsChanged() {
            _syncVideoState(_replay.videoPositionMs, true)
        }
        function onVideoPositionMsChanged() {
            const posMs = _replay.videoPositionMs
            if (_videoEnded && posMs < _prevPositionMs) _videoEnded = false
            _prevPositionMs = posMs
            _syncVideoState(posMs, false)
        }
    }
}
