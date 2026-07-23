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

/// Versioned service id for QGCTelemetryLoggingService. Breaking changes ship
/// as "qgc.telemetryLogging/2", never as changes to this interface.
inline constexpr const char* QGCTelemetryLoggingServiceId = "qgc.telemetryLogging/1";

/**
 * @class QGCTelemetryLoggingService
 * @ingroup PluginAPI
 * @brief Host service for telemetry-log (tlog) recording control.
 *
 * Acquired via QGCHostServices::service(QGCTelemetryLoggingServiceId) and
 * cast with qobject_cast. Intended for plugins that declare
 * "telemetryLogging": true in their manifest and take over the tlog
 * save/discard decision from the host.
 *
 * A recording that stops becomes a *pending log*: it is complete on disk but
 * not yet saved to the telemetry directory. Exactly one pending log exists at
 * a time; savePendingLog()/discardPendingLog() resolve it.
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.telemetryLogging/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCTelemetryLoggingService : public QObject
{
    Q_OBJECT

public:
    explicit QGCTelemetryLoggingService(QObject* parent = nullptr);
    ~QGCTelemetryLoggingService() override;

    /// Returns true while a tlog is actively being written.
    virtual bool tlogLogging() const = 0;

    /// Returns true when a completed tlog is waiting for a save/discard decision.
    virtual bool hasPendingLog() const = 0;

    /// The filename (no directory) the pending log will be saved as.
    /// Empty when hasPendingLog() is false.
    virtual QString pendingLogName() const = 0;

    /// Start tlog recording. No-op if already logging or a pending log exists.
    virtual void startTlogLogging() = 0;

    /// Stop tlog recording. The completed log becomes the pending log.
    virtual void stopTlogLogging() = 0;

    /// Save the pending tlog to the host's telemetry directory.
    virtual void savePendingLog() = 0;

    /// Discard (delete) the pending tlog without saving.
    virtual void discardPendingLog() = 0;

signals:
    void tlogLoggingChanged();
    void hasPendingLogChanged();
};
