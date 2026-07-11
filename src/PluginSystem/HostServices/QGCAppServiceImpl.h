/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCAppService.h"

/**
 * @class QGCAppServiceImpl
 * @brief Host implementation of "qgc.app/1" over QCoreApplication and AppSettings.
 */
class QGCAppServiceImpl : public QGCAppService
{
    Q_OBJECT

public:
    explicit QGCAppServiceImpl(QObject* parent = nullptr);

    QString applicationName() const override;
    QString organizationName() const override;
    QString versionString() const override;
    QString savePath() const override;
    QString telemetrySavePath() const override;
};
