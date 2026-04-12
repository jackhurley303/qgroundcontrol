import QtQuick
import QtMultimedia

import QGroundControl

Item {
    anchors.fill: parent

    readonly property var _replay: QGroundControl.pluginManager.replayExtension

    MediaPlayer {
        id:          videoPlayer
        source:      _replay ? _replay.videoUrl : ""
        videoOutput: videoOutput
    }

    VideoOutput {
        id:           videoOutput
        anchors.fill: parent
        fillMode:     VideoOutput.PreserveAspectFit
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
        function onVideoPositionMsChanged() {
            if (Math.abs(videoPlayer.position - _replay.videoPositionMs) > 1000)
                videoPlayer.position = _replay.videoPositionMs
        }
    }
}
