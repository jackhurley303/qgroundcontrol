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

/**
 * @class QGCHostServices
 * @brief Plugin-side handle to the services the host application provides.
 *
 * The host passes an instance to QGCPlugin::init(). Plugins acquire individual
 * services by their versioned id (e.g. "qgc.replay/1") and cast the returned
 * QObject to the matching service interface from this SDK. An unknown id
 * returns nullptr; plugins must tolerate absent services.
 *
 * Service ids are append-only: a breaking change to a service interface ships
 * as a new id ("qgc.replay/2"), never as a change to an existing one.
 */
class QGCPLUGINAPI_EXPORT QGCHostServices : public QObject
{
    Q_OBJECT

public:
    explicit QGCHostServices(QObject* parent = nullptr);
    ~QGCHostServices() override;

    /// Look up a host service by its versioned id.
    /// @return The service object, or nullptr if the host does not provide it.
    virtual QObject* service(const QString& id) = 0;
};
