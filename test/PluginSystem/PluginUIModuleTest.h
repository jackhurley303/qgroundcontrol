/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtQml/QQmlEngine>

#include "UnitTest.h"

class QObject;

/// Verifies the QGroundControl.PluginUI contract inside a real QQmlEngine.
///
/// The identity checks are the point. A module built the obvious way — a second
/// qt_add_qml_module over the same sources — renders perfectly and yet gives the
/// plugin a *different* type and a *second* singleton instance than the host has.
/// Appearance cannot catch that, so it is asserted here instead.
class PluginUIModuleTest : public UnitTest
{
    Q_OBJECT

private slots:
    void initTestCase() override;
    void cleanupTestCase() override;
    void init() override;

    void _compositeTypeIsSharedWithHost_test();
    void _singletonIsSharedWithHost_test();
    void _cppSingletonIsSharedWithHost_test();
    void _cppTypesResolve_test();
    void _everyRosterTypeResolves_test();
    void _unpublishedHostTypeIsNotReachable_test();
    void _unknownTypeIsNotReachable_test();
    void _facadeMembersResolve_test();
    void _facadeDelegatesToHostGlobal_test();
    void _facadeShowMessageDialogForwardsToHostGlobal_test();

private:
    /// Builds \a qml against the shared engine. Returns the root object, or null
    /// with _lastError set to the component's error string.
    QObject* _create(const QByteArray& qml);

    QQmlEngine* _engine = nullptr;
    QString _lastError;
};
