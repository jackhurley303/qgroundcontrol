/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>

#include "QGCHostServices.h"

Q_DECLARE_LOGGING_CATEGORY(QGCHostServicesLog)

/**
 * @class QGCHostServicesImpl
 * @brief Host-side service registry handed to plugins via QGCPlugin::init().
 *
 * A plain id -> QObject map. The plugin manager constructs one instance,
 * registers the host's service implementations on it, and passes it to every
 * plugin. Registered services are owned by their parents, not the registry.
 */
class QGCHostServicesImpl : public QGCHostServices
{
    Q_OBJECT

public:
    explicit QGCHostServicesImpl(QObject* parent = nullptr);

    /// Register a service under its versioned id. First registration wins;
    /// a duplicate id is ignored with a warning.
    void registerService(const QString& id, QObject* service);

    QObject* service(const QString& id) override;

private:
    QHash<QString, QObject*> _services;
};
