/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

/// The C++ half of the QGroundControl.PluginUI module.
///
/// The composite (.qml) half is declared entirely in PluginUI/qmldir, which the
/// engine picks up as a resource. C++ types can't be declared in a qmldir, so the
/// handful this module publishes are re-registered here under the second URI.
///
/// Re-registration is additive: it yields the *same* C++ type under both URIs, so
/// nothing has to move out of the executable and a host type is never doubled.
namespace PluginUIModule {

/// Registers the module's C++ types. Idempotent, and must run before any
/// QQmlEngine resolves an `import QGroundControl.PluginUI`.
void registerTypes();

}  // namespace PluginUIModule
