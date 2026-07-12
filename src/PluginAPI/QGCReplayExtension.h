/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include "qgc_plugin_api_global.h"

/**
 * @class QGCReplayExtension
 * @brief Abstract interface for plugin-provided flight replay functionality.
 *
 * Plugins that support flight replay (tlog playback with optional video sync)
 * should implement this interface and return an instance from
 * QGCPlugin::replayExtension(). The core UI accesses replay functionality
 * exclusively through QGCPluginManager::replayExtension(), which exposes the
 * first registered implementation.
 *
 * This interface intentionally uses QObject* for flight entry parameters so
 * the core has no dependency on plugin-specific model types.
 *
 * @warning This vtable is frozen: it ships across the SDK boundary, so adding,
 * removing, or reordering virtuals breaks every built plugin silently.
 * Additions go to a new "QGCReplayExtension2"-style interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCReplayExtension : public QObject
{
    Q_OBJECT

    // ── Replay state ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool     isActive        READ isActive        NOTIFY isActiveChanged)
    Q_PROPERTY(QObject* logReplayLink   READ logReplayLink   NOTIFY isActiveChanged)
    Q_PROPERTY(bool     isPlaying       READ isPlaying       NOTIFY isPlayingChanged)
    Q_PROPERTY(qreal    playbackSpeed   READ playbackSpeed   NOTIFY playbackSpeedChanged)

    // ── Video state ───────────────────────────────────────────────────────────
    Q_PROPERTY(bool     hasVideo        READ hasVideo        NOTIFY hasVideoChanged)
    Q_PROPERTY(QString  videoUrl        READ videoUrl        NOTIFY videoUrlChanged)
    Q_PROPERTY(qreal    videoOffsetSecs READ videoOffsetSecs NOTIFY videoOffsetSecsChanged)
    Q_PROPERTY(qint64   videoPositionMs  READ videoPositionMs  NOTIFY videoPositionMsChanged)
    Q_PROPERTY(qint64   videoDurationMs  READ videoDurationMs  NOTIFY videoDurationMsChanged)

public:
    explicit QGCReplayExtension(QObject* parent = nullptr);
    ~QGCReplayExtension() override;

    virtual bool     isActive()        const = 0;
    virtual QObject* logReplayLink()   const = 0;
    virtual bool     isPlaying()       const = 0;
    virtual qreal    playbackSpeed()   const = 0;
    virtual bool     hasVideo()        const = 0;
    virtual QString  videoUrl()        const = 0;
    virtual qreal    videoOffsetSecs() const = 0;
    virtual qint64   videoPositionMs()  const = 0;
    virtual qint64   videoDurationMs()  const = 0;

    // ── Primary API ───────────────────────────────────────────────────────────

    /** Open a flight entry for full replay. Accepts QObject* to avoid core dependency on plugin model types. */
    Q_INVOKABLE virtual void openFlight(QObject* entry) = 0;

    /** Stop replay and unload all resources. */
    Q_INVOKABLE virtual void closeFlight() = 0;

    // ── Unified playback controls ─────────────────────────────────────────────

    Q_INVOKABLE virtual void setPlaybackSpeed(qreal speed) = 0;
    Q_INVOKABLE virtual void seekTo(qreal percent) = 0;

    // ── Video offset adjustment ───────────────────────────────────────────────

    Q_INVOKABLE virtual void adjustVideoOffset(qreal deltaSecs) = 0;

signals:
    void isActiveChanged();
    void isPlayingChanged();
    void playbackSpeedChanged();
    void hasVideoChanged();
    void videoUrlChanged();
    void videoOffsetSecsChanged();
    void videoPositionMsChanged();
    void videoDurationMsChanged();
};
