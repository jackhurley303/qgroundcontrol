import QtQuick

import QGroundControl.PluginUI

// Negative control for the out-of-tree SDK gate (out-of-tree-plugins.md U6): reaches
// ToolStrip, a real host QML type that QGroundControl.PluginUI does not publish. The
// gate step in .github/workflows/macos.yml runs qmllint on this file with the same
// escalated-severity flags used on the template's real panel and asserts a non-zero
// exit — proving the gate actually rejects out-of-subset QML rather than silently
// passing (qmllint exits 0 on every diagnostic by default; see SDK-README.md).
Item {
    ToolStrip {}
}
