/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCTelemetryLoggingServiceImpl.h"
#include "MAVLinkProtocol.h"

QGCTelemetryLoggingServiceImpl::QGCTelemetryLoggingServiceImpl(QObject* parent)
    : QGCTelemetryLoggingService(parent)
{
    connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::tlogLoggingChanged,
            this, &QGCTelemetryLoggingService::tlogLoggingChanged);
    connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::hasPendingLogChanged,
            this, &QGCTelemetryLoggingService::hasPendingLogChanged);
}

bool QGCTelemetryLoggingServiceImpl::tlogLogging() const
{
    return MAVLinkProtocol::instance()->tlogLogging();
}

bool QGCTelemetryLoggingServiceImpl::hasPendingLog() const
{
    return MAVLinkProtocol::instance()->hasPendingLog();
}

QString QGCTelemetryLoggingServiceImpl::pendingLogName() const
{
    return MAVLinkProtocol::instance()->pendingLogName();
}

void QGCTelemetryLoggingServiceImpl::startTlogLogging()
{
    MAVLinkProtocol::instance()->startTlogLogging();
}

void QGCTelemetryLoggingServiceImpl::stopTlogLogging()
{
    MAVLinkProtocol::instance()->stopTlogLogging();
}

void QGCTelemetryLoggingServiceImpl::savePendingLog()
{
    MAVLinkProtocol::instance()->savePendingLog();
}

void QGCTelemetryLoggingServiceImpl::discardPendingLog()
{
    MAVLinkProtocol::instance()->discardPendingLog();
}
