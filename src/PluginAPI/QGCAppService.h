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

/// Versioned service id for QGCAppService. Breaking changes ship as
/// "qgc.app/2", never as changes to this interface.
inline constexpr const char* QGCAppServiceId = "qgc.app/1";

/**
 * @class QGCAppService
 * @brief Host service exposing application identity and storage paths.
 *
 * Acquired via QGCHostServices::service(QGCAppServiceId) and cast with
 * qobject_cast.
 *
 * savePath() is the user-configurable root under which the host stores its
 * file types; telemetrySavePath() is the directory the host writes telemetry
 * logs (tlogs) into — plugins locating a host-written tlog must use it rather
 * than deriving the directory themselves. Both can change at runtime when the
 * user reconfigures the save location (savePathsChanged()).
 *
 * ABI: this vtable is frozen once the SDK ships — additions go to a
 * "qgc.app/2" interface, never here.
 */
class QGCPLUGINAPI_EXPORT QGCAppService : public QObject
{
    Q_OBJECT

public:
    explicit QGCAppService(QObject* parent = nullptr);
    ~QGCAppService() override;

    /// The application display name (e.g. "QGroundControl Daily").
    virtual QString applicationName() const = 0;

    /// The organization name the host stores settings under.
    virtual QString organizationName() const = 0;

    /// The host version string (git describe, e.g. "v5.0.7-...").
    virtual QString versionString() const = 0;

    /// The user-configurable root save location.
    virtual QString savePath() const = 0;

    /// The directory the host writes telemetry logs into.
    virtual QString telemetrySavePath() const = 0;

signals:
    void savePathsChanged();
};
