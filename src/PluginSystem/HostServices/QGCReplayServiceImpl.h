/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QPointer>

#include "QGCReplayService.h"

class LogReplayLink;

/**
 * @class QGCReplayServiceImpl
 * @brief Host implementation of "qgc.replay/1" over LinkManager's log replay.
 *
 * Wraps the single active LogReplayLink: startReplay() creates it through
 * LinkManager and relays its playback signals to the service's own; the
 * control methods forward to it and no-op when no session is active. The
 * link itself is owned by LinkManager (QPointer-tracked here).
 */
class QGCReplayServiceImpl : public QGCReplayService
{
    Q_OBJECT

public:
    explicit QGCReplayServiceImpl(QObject* parent = nullptr);

    bool startReplay(const QString& tlogPath) override;
    void stopReplay() override;
    bool replayActive() const override;

    void play() override;
    void pause() override;
    void beginStream() override;
    void setPlaybackSpeed(qreal playbackSpeed) override;
    void movePlayhead(qreal percentComplete) override;
    void requestPlanReload() override;

    void registerReplayParamFile(int vehicleId, const QString& filePath) override;
    void registerReplayPlanFile(int vehicleId, const QString& planFilePath) override;

private slots:
    void _onLinkDisconnected();

private:
    QPointer<LogReplayLink> _link;
    // Link disconnect is queued to the worker thread; refuse a new session
    // until the old link is actually destroyed so two replay links never
    // fight over LinkManager's global connection-suspend state
    QPointer<LogReplayLink> _teardownLink;
};
