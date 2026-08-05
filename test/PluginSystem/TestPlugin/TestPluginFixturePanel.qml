import QtQuick

/// The fixture's QML payload, compiled into the plugin library's own resources
/// and therefore registered only when the library is loaded. That late
/// registration is the whole point: it is what
/// QGCPluginManagerTest::_lateActivationResolvesPluginQml_test asserts the host
/// copes with.
Item {
    width:  20
    height: 20

    property string marker: "test-plugin-fixture-panel"
}
