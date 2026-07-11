/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCTelemetryLoggingService.h"

/**
 * @class QGCTelemetryLoggingServiceImpl
 * @brief Host implementation of "qgc.telemetryLogging/1" over MAVLinkProtocol.
 *
 * Every call delegates to the MAVLinkProtocol singleton; the singleton's
 * change signals are relayed to the service's own.
 */
class QGCTelemetryLoggingServiceImpl : public QGCTelemetryLoggingService
{
    Q_OBJECT

public:
    explicit QGCTelemetryLoggingServiceImpl(QObject* parent = nullptr);

    bool tlogLogging() const override;
    bool hasPendingLog() const override;
    QString pendingLogName() const override;

    void startTlogLogging() override;
    void stopTlogLogging() override;
    void savePendingLog() override;
    void discardPendingLog() override;
};
