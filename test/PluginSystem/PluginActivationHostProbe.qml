import QtQuick

/// Host-side stand-in for the application QML that lives in the qrc:/qml tree,
/// registered into the app binary at build time — so it is present before any
/// engine exists, exactly like the real host's QML.
///
/// Loading it makes the engine enumerate the qrc:/qml directory that plugins
/// later register into, which is what puts the type loader's directory cache in
/// the stale state QGCPluginManagerTest reproduces. The binding below is there
/// to prove the invalidation does not disturb host QML that is already live.
Item {
    width:  10
    height: 10

    property real scale2:  2.0
    property real derived: scale2 * 2
}
