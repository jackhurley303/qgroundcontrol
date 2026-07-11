/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCAppServiceImpl.h"
#include "AppSettings.h"
#include "SettingsManager.h"

#include <QtCore/QCoreApplication>

QGCAppServiceImpl::QGCAppServiceImpl(QObject* parent)
    : QGCAppService(parent)
{
    connect(SettingsManager::instance()->appSettings(), &AppSettings::savePathsChanged,
            this, &QGCAppService::savePathsChanged);
}

QString QGCAppServiceImpl::applicationName() const
{
    return QCoreApplication::applicationName();
}

QString QGCAppServiceImpl::organizationName() const
{
    return QCoreApplication::organizationName();
}

QString QGCAppServiceImpl::versionString() const
{
    return QCoreApplication::applicationVersion();
}

QString QGCAppServiceImpl::savePath() const
{
    return SettingsManager::instance()->appSettings()->savePath()->rawValue().toString();
}

QString QGCAppServiceImpl::telemetrySavePath() const
{
    return SettingsManager::instance()->appSettings()->telemetrySavePath();
}
