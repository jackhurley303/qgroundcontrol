/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCVehicleService.h"

/**
 * @class QGCVehicleServiceImpl
 * @brief Host implementation of "qgc.vehicles/1" over MultiVehicleManager.
 *
 * Hands out the manager's Vehicle instances as QObject* and relays its
 * add/remove/active-changed signals.
 */
class QGCVehicleServiceImpl : public QGCVehicleService
{
    Q_OBJECT

public:
    explicit QGCVehicleServiceImpl(QObject* parent = nullptr);

    QObject* activeVehicle() const override;
    QList<QObject*> vehicles() const override;
};
