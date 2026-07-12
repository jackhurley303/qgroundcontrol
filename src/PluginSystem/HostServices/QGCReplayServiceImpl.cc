/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCReplayServiceImpl.h"
#include "QGCHostServicesImpl.h"
#include "LinkManager.h"
#include "LogReplayLink.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "Vehicle.h"

QGCReplayServiceImpl::QGCReplayServiceImpl(QObject* parent)
    : QGCReplayService(parent)
{
}

bool QGCReplayServiceImpl::startReplay(const QString& tlogPath)
{
    if (tlogPath.isEmpty() || _link || _teardownLink) {
        return false;
    }

    // Mirror the replay worker's own rule synchronously: it refuses to
    // connect while any vehicle is active (including one created by another
    // replay), and the rejected link would be left as dead weight whose
    // pause() still flips LinkManager's global connection-suspend state
    if (MultiVehicleManager::instance()->activeVehicle()) {
        return false;
    }

    LogReplayLink* link = LinkManager::instance()->startLogReplay(tlogPath);
    if (!link) {
        return false;
    }
    _link = link;

    connect(link, &LogReplayLink::playbackStarted, this, &QGCReplayService::playbackStarted);
    connect(link, &LogReplayLink::playbackPaused, this, &QGCReplayService::playbackPaused);
    connect(link, &LogReplayLink::playbackAtEnd, this, &QGCReplayService::playbackAtEnd);
    connect(link, &LogReplayLink::playbackPercentCompleteChanged, this, &QGCReplayService::playbackPercentCompleteChanged);
    connect(link, &LogReplayLink::currentLogTimeSecs, this, &QGCReplayService::logTimeChanged);
    connect(link, &LogReplayLink::logFileStats, this, &QGCReplayService::logDurationChanged);
    connect(link, &LogReplayLink::communicationError, this,
            [this](const QString& title, const QString& error) {
                Q_UNUSED(title);
                emit replayError(error);
            });
    // Host-side teardown (e.g. corrupt tlog): QPointer only nulls on link
    // destruction, so track disconnect explicitly to keep replayActive() honest
    connect(link, &LogReplayLink::disconnected, this, &QGCReplayServiceImpl::_onLinkDisconnected);

    return true;
}

void QGCReplayServiceImpl::stopReplay()
{
    if (!_link) {
        return;
    }
    LogReplayLink* link = _link;
    _link.clear();
    _teardownLink = link;
    // Tear down the relays first so queued late emissions (the pause below
    // lands on the worker asynchronously) don't reach plugins after the
    // session is already reported ended
    QObject::disconnect(link, nullptr, this, nullptr);
    link->pause();
    link->disconnect();
    emit replayEnded();
}

void QGCReplayServiceImpl::_onLinkDisconnected()
{
    if (!_link) {
        return; // already ended via stopReplay()
    }
    LogReplayLink* link = _link;
    _link.clear();
    _teardownLink = link;
    QObject::disconnect(link, nullptr, this, nullptr);
    emit replayEnded();
}

bool QGCReplayServiceImpl::replayActive() const
{
    return !_link.isNull();
}

void QGCReplayServiceImpl::play()
{
    if (_link) {
        _link->play();
    }
}

void QGCReplayServiceImpl::pause()
{
    if (_link) {
        _link->pause();
    }
}

void QGCReplayServiceImpl::beginStream()
{
    if (_link) {
        _link->beginStream();
    }
}

void QGCReplayServiceImpl::setPlaybackSpeed(qreal playbackSpeed)
{
    // The worker divides by the speed; a non-positive value would hang or
    // corrupt the playback timing loop
    if (playbackSpeed <= 0.0) {
        qCWarning(QGCHostServicesLog) << "Ignoring non-positive playback speed:" << playbackSpeed;
        return;
    }
    if (_link) {
        _link->setPlaybackSpeed(playbackSpeed);
    }
}

void QGCReplayServiceImpl::movePlayhead(qreal percentComplete)
{
    if (_link) {
        _link->movePlayhead(percentComplete);
    }
}

void QGCReplayServiceImpl::requestPlanReload()
{
    if (_link) {
        _link->requestPlanReload();
    }
}

void QGCReplayServiceImpl::registerReplayParamFile(int vehicleId, const QString& filePath)
{
    ParameterManager::registerReplayParamFile(vehicleId, filePath);
}

void QGCReplayServiceImpl::registerReplayPlanFile(int vehicleId, const QString& planFilePath)
{
    Vehicle::registerReplayPlanFile(vehicleId, planFilePath);
}
