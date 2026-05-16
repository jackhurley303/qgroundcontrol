/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>

/**
 * @brief Plugin-agnostic hook for plan view toolbar actions.
 *
 * Plugins that need to override or extend toolbar save behavior register
 * via registerSaveOverride() and listen to planSaveRequested(). The toolbar
 * checks hasSaveOverride before invoking its default save logic.
 *
 * Multiple plugins may register simultaneously — hasSaveOverride remains
 * true until all have unregistered.
 */
class PlanViewActionContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSaveOverride READ hasSaveOverride NOTIFY saveOverrideChanged)

public:
    static PlanViewActionContext* instance();

    bool hasSaveOverride() const { return _overrideCount > 0; }

    Q_INVOKABLE void registerSaveOverride();
    Q_INVOKABLE void unregisterSaveOverride();

    /** Called by the toolbar when hasSaveOverride is true. Emits planSaveRequested. */
    Q_INVOKABLE void triggerSave(QObject* planMasterController);

signals:
    void saveOverrideChanged();
    void planSaveRequested(QObject* planMasterController);

private:
    explicit PlanViewActionContext(QObject* parent = nullptr);

    int _overrideCount = 0;
    static PlanViewActionContext* _instance;
};
