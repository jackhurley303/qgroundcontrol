/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include "qgc_plugin_api_global.h"

/// Versioned service id for QGCReplayService. Breaking changes ship as
/// "qgc.replay/2", never as changes to this interface.
inline constexpr const char* QGCReplayServiceId = "qgc.replay/1";

/**
 * @class QGCReplayService
 * @brief Host service for telemetry-log (tlog) flight replay.
 *
 * Acquired via QGCHostServices::service(QGCReplayServiceId) and cast with
 * qobject_cast. Replay is a single session: one tlog file plays back at a
 * time (the stream inside it may carry multiple vehicles). startReplay()
 * while a session is active fails; the playback controls are safe no-ops
 * when no session is active.
 *
 * The sidecar registries associate a parameter or plan file with a vehicle
 * system id before that vehicle is created by replay, so the host loads them
 * during vehicle initialisation. Passing an empty path clears the entry.
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.replay/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCReplayService : public QObject
{
    Q_OBJECT

public:
    explicit QGCReplayService(QObject* parent = nullptr);
    ~QGCReplayService() override;

    /// Start a replay session for the given tlog file. A true return means the
    /// session was created, not that playback will succeed: failures after
    /// creation (a live vehicle connection, a corrupt or empty tlog) are
    /// reported asynchronously via replayError(), and a session the host tears
    /// down ends with replayEnded().
    /// @return false if the path is empty, a session is already active or
    /// still tearing down after stopReplay()/replayEnded(), a vehicle
    /// connection is active, or the host failed to create the replay link.
    virtual bool startReplay(const QString& tlogPath) = 0;

    /// End the active replay session (pauses playback and disconnects the
    /// replay link), emitting replayEnded(). No-op when no session is active.
    virtual void stopReplay() = 0;

    /// Returns true while a replay session is active.
    virtual bool replayActive() const = 0;

    /// Resume playback.
    virtual void play() = 0;

    /// Pause playback.
    virtual void pause() = 0;

    /// Begin streaming the log from the current playhead position.
    virtual void beginStream() = 0;

    /// Set the playback speed multiplier (1.0 = realtime). Must be > 0;
    /// non-positive values are ignored.
    virtual void setPlaybackSpeed(qreal playbackSpeed) = 0;

    /// Seek to a position in the log.
    /// @param percentComplete Position as a percentage of the log [0, 100].
    virtual void movePlayhead(qreal percentComplete) = 0;

    /// Ask the host to reload registered plan files for the replay vehicles.
    virtual void requestPlanReload() = 0;

    /// Register a parameter file to load when replay creates the vehicle with
    /// this system id. An empty path clears the entry.
    virtual void registerReplayParamFile(int vehicleId, const QString& filePath) = 0;

    /// Register a plan file to load when replay creates the vehicle with this
    /// system id. An empty path clears the entry.
    virtual void registerReplayPlanFile(int vehicleId, const QString& planFilePath) = 0;

signals:
    void playbackStarted();
    void playbackPaused();
    void playbackAtEnd();
    /// @param percentComplete Position as a percentage of the log [0, 100].
    void playbackPercentCompleteChanged(qreal percentComplete);
    void currentLogTimeSecs(quint32 secs);
    void logFileStats(quint32 logDurationSecs);
    /// A session error reported by the host (e.g. replay refused while a
    /// vehicle is connected, corrupt tlog, seek failure). The session may or
    /// may not survive the error; a terminal one is followed by replayEnded().
    void replayError(const QString& message);
    /// The session ended — either stopReplay() or a host-side teardown.
    /// replayActive() is already false when this is emitted.
    void replayEnded();
};
