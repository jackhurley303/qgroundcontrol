/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCHostServicesImpl.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(QGCHostServicesLog, "PluginSystem.QGCHostServices");

QGCHostServicesImpl::QGCHostServicesImpl(QObject* parent)
    : QGCHostServices(parent)
{
}

void QGCHostServicesImpl::registerService(const QString& id, QObject* service)
{
    if (_services.contains(id)) {
        qCWarning(QGCHostServicesLog) << "Service id already registered, ignoring duplicate:" << id;
        return;
    }
    _services.insert(id, service);
    qCDebug(QGCHostServicesLog) << "Registered service:" << id;
}

QObject* QGCHostServicesImpl::service(const QString& id)
{
    return _services.value(id, nullptr);
}
