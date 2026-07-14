/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QVariantMap>

class PluginManifest;

/// Value type holding the contributions a plugin declares in its manifest's
/// "contributes" object, synthesized into the exact QVariantMap shapes the QML
/// consumers read. Synthesis is a pure data operation: no plugin code runs.
///
/// URL rule: "qrc:/..." URLs and resource paths ("/...") refer to compiled-in
/// resources and pass through verbatim. Any other (relative) URL is package-relative,
/// resolved against packageDir into a "file://<packageDir>/<url>" URL; with no package
/// context (packageDir empty — the dev-loop bare-dylib path), a relative URL passes
/// through unresolved.
///
/// Schema of the "contributes" object (all keys optional):
///
///     "contributes": {
///         "toolMenu":      { "title": "Example",                        // required
///                            "source": "qrc:/qml/ExampleView.qml",      // required
///                            "icon": "/qmlimages/plugin.svg",
///                            "toolbarSource": "qrc:/qml/ExampleToolBar.qml" },
///         "flyViewPanel":  { "panel": "qrc:/qml/ExamplePanel.qml",      // required
///                            "dock": "qrc:/qml/ExampleDockItem.qml",
///                            "defaultWidth": 35,                        // font-width units, 0 = framework default
///                            "defaultHeight": 18,                       // font-height units, 0 = framework default
///                            "defaultPosition": [0.0, 0.0] },           // [x, y] fractions, [-1, -1] = framework default
///         "planViewPanel": { ... same keys as flyViewPanel ... },
///         "replay": false,           // plugin provides a QGCReplayExtension
///         "telemetryLogging": false  // plugin claims exclusive tlog-logging control
///     }
///
/// Unknown keys are ignored; a present key with the wrong shape is an error.
class PluginContributions
{
public:
    /// Tool menu entry with keys pluginId, title, icon, source, toolbarSource.
    /// Empty when the plugin contributes no tool menu entry.
    QVariantMap toolMenuItem;

    /// Fly-view panel with keys pluginId, name, panelUrl, dockUrl, defaultWidth,
    /// defaultHeight, defaultXFraction, defaultYFraction. Empty when not contributed.
    QVariantMap flyViewPanelItem;

    /// Plan-view panel, same keys as flyViewPanelItem. Empty when not contributed.
    QVariantMap planViewPanelItem;

    /// True if the manifest declares "replay": the plugin's replayExtension()
    /// is only queried when declared.
    bool providesReplayExtension = false;

    /// True if the manifest declares "telemetryLogging": the plugin takes
    /// exclusive control of tlog logging and MAVLinkProtocol disables its
    /// built-in auto-start/auto-save behaviour.
    bool controlsTelemetryLogging = false;

    /// Parses a manifest's "contributes" object, resolving relative URLs against
    /// packageDir (empty for non-package plugins — see the URL rule above). On failure
    /// returns default-constructed contributions and, if errorOut is non-null, a
    /// human-readable reason.
    static PluginContributions fromManifest(const PluginManifest &manifest, const QString &packageDir, QString *errorOut = nullptr);
};
