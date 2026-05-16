/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PlanViewActionContext.h"

PlanViewActionContext* PlanViewActionContext::_instance = nullptr;

PlanViewActionContext* PlanViewActionContext::instance()
{
    if (!_instance) {
        _instance = new PlanViewActionContext();
    }
    return _instance;
}

PlanViewActionContext::PlanViewActionContext(QObject* parent)
    : QObject(parent)
{
}

void PlanViewActionContext::registerSaveOverride()
{
    ++_overrideCount;
    if (_overrideCount == 1) {
        emit saveOverrideChanged();
    }
}

void PlanViewActionContext::unregisterSaveOverride()
{
    if (_overrideCount > 0) {
        --_overrideCount;
    }
    if (_overrideCount == 0) {
        emit saveOverrideChanged();
    }
}

void PlanViewActionContext::triggerSave(QObject* planMasterController)
{
    emit planSaveRequested(planMasterController);
}
