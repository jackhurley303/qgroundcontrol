#!/usr/bin/env python3
"""Tests for check_plugin_ui_contract.py — the QML contract watchdog (out-of-tree-plugins.md U5)."""

from __future__ import annotations

import pytest

from check_plugin_ui_contract import UNSTABLE_CPP_TYPES, _tier_by_name, diff_qmldir, diff_qmltypes
from derive_plugin_ui_sdk import parse_qmldir

QMLDIR = """\
module QGroundControl.PluginUI

depends QtQuick

# ---------------------------------------------------------------------------
# Frozen tier — controls
# ---------------------------------------------------------------------------
QGCLabel 1.0 QGCLabel.qml
QGCButton 1.0 QGCButton.qml

# ---------------------------------------------------------------------------
# Frozen tier — singletons.
# ---------------------------------------------------------------------------
singleton ScreenTools 1.0 ScreenTools.qml

# ---------------------------------------------------------------------------
# Unstable tier — map and mission visuals.
# ---------------------------------------------------------------------------
FlightMap 1.0 FlightMap.qml
"""

QMLTYPES = """\
import QtQuick.tooling 1.2

Module {
    Component {
        file: "QGCPalette.h"
        name: "QGCPalette"
        exports: ["QGroundControl.PluginUI/QGCPalette 1.0"]
        Property { name: "window"; type: "QColor" }
    }
    Component {
        file: "PlanMasterController.h"
        name: "PlanMasterController"
        exports: ["QGroundControl.PluginUI/PlanMasterController 1.0"]
        Property { name: "offline"; type: "bool" }
    }
}
"""


def _qmldir(text: str):
    return parse_qmldir(text)


def test_identical_snapshots_produce_no_failures_or_reports():
    qmldir = _qmldir(QMLDIR)
    failures, reports = diff_qmldir(qmldir, QMLDIR, qmldir, QMLDIR)
    assert failures == []
    assert reports == []

    q_failures, q_reports = diff_qmltypes(QMLTYPES, QMLTYPES)
    assert q_failures == []
    assert q_reports == []


def test_frozen_type_removal_fails():
    current_text = QMLDIR.replace("QGCButton 1.0 QGCButton.qml\n", "")
    current = _qmldir(current_text)

    failures, reports = diff_qmldir(_qmldir(QMLDIR), QMLDIR, current, current_text)

    assert any("QGCButton" in f and "removed" in f for f in failures)
    assert reports == []


def test_frozen_type_shape_change_fails():
    current_text = QMLDIR.replace("QGCButton 1.0 QGCButton.qml", "QGCButton 1.0 QGCButtonRenamed.qml")
    current = _qmldir(current_text)

    failures, _ = diff_qmldir(_qmldir(QMLDIR), QMLDIR, current, current_text)

    assert any("QGCButton" in f and "changed shape" in f for f in failures)


def test_frozen_type_addition_passes():
    current_text = QMLDIR.replace(
        "singleton ScreenTools", "QGCNewControl 1.0 QGCNewControl.qml\nsingleton ScreenTools"
    )
    current = _qmldir(current_text)

    failures, reports = diff_qmldir(_qmldir(QMLDIR), QMLDIR, current, current_text)

    assert failures == []
    assert any("QGCNewControl" in r and "added" in r for r in reports)


def test_unstable_type_removal_is_reported_not_failed():
    current_text = QMLDIR.replace("FlightMap 1.0 FlightMap.qml\n", "")
    current = _qmldir(current_text)

    failures, reports = diff_qmldir(_qmldir(QMLDIR), QMLDIR, current, current_text)

    assert failures == []
    assert any("FlightMap" in r and "removed" in r for r in reports)


def test_unstable_type_shape_change_is_reported_not_failed():
    current_text = QMLDIR.replace("FlightMap 1.0 FlightMap.qml", "FlightMap 2.0 FlightMap.qml")
    current = _qmldir(current_text)

    failures, reports = diff_qmldir(_qmldir(QMLDIR), QMLDIR, current, current_text)

    assert failures == []
    assert any("FlightMap" in r and "changed shape" in r for r in reports)


def test_frozen_cpp_type_property_removal_fails():
    current_text = QMLTYPES.replace('Property { name: "window"; type: "QColor" }\n', "")

    failures, reports = diff_qmltypes(QMLTYPES, current_text)

    assert any("QGCPalette" in f and "changed shape" in f for f in failures)
    assert reports == []


def test_unstable_cpp_type_property_removal_is_reported_not_failed():
    assert "PlanMasterController" in UNSTABLE_CPP_TYPES
    current_text = QMLTYPES.replace('Property { name: "offline"; type: "bool" }\n', "")

    failures, reports = diff_qmltypes(QMLTYPES, current_text)

    assert failures == []
    assert any("PlanMasterController" in r and "changed shape" in r for r in reports)


def test_new_frozen_cpp_type_passes():
    new_component = (
        '    Component {\n        file: "New.h"\n        name: "QGCNewType"\n'
        '        exports: ["QGroundControl.PluginUI/QGCNewType 1.0"]\n    }\n'
    )
    current_text = QMLTYPES.rstrip("\n").removesuffix("}") + new_component + "}\n"

    failures, reports = diff_qmltypes(QMLTYPES, current_text)

    assert failures == []
    assert any("QGCNewType" in r and "added" in r for r in reports)


def test_qmldir_only_diff_is_blind_to_cpp_type_shape():
    # A property removed from a frozen C++ type's .qmltypes is invisible to the
    # qmldir diff alone — the roster carries no C++ type entries at all. Both
    # artifacts are required for the check to see the whole contract.
    qmldir = _qmldir(QMLDIR)
    failures, _ = diff_qmldir(qmldir, QMLDIR, qmldir, QMLDIR)
    assert failures == []

    current_qmltypes = QMLTYPES.replace('Property { name: "window"; type: "QColor" }\n', "")
    q_failures, _ = diff_qmltypes(QMLTYPES, current_qmltypes)
    assert q_failures  # only the .qmltypes diff catches it


_DIVIDER = "# " + "-" * 75


def test_a_divider_section_whose_header_does_not_use_a_recognized_tier_word_raises():
    # A tier's section header must be recognized structurally (the roster's
    # own divider-delimited block), not by re-matching the exact "frozen
    # tier" / "unstable tier" wording as a loose substring anywhere in a
    # comment: a later section using different phrasing (a realistic roster
    # edit — "Newly published controls", say) must not silently carry the
    # previous section's tier forward, because that would misclassify a
    # frozen-tier type as unstable and let a breaking change through as a
    # non-fatal report. It must raise instead of guessing.
    text = QMLDIR + f"\n{_DIVIDER}\n# Newly published controls\n{_DIVIDER}\nQGCNewThing 1.0 QGCNewThing.qml\n"
    qmldir = parse_qmldir(text)

    with pytest.raises(ValueError, match="does not start with"):
        _tier_by_name(qmldir, text)


def test_a_type_entry_before_any_tier_header_raises():
    text = "module QGroundControl.PluginUI\n\nQGCLabel 1.0 QGCLabel.qml\n"
    qmldir = parse_qmldir(text)

    with pytest.raises(ValueError, match="QGCLabel"):
        _tier_by_name(qmldir, text)


def test_multiline_header_prose_is_recognized_by_its_first_line_only():
    # src/PluginSystem/PluginUI/qmldir's "Unstable tier" header actually runs
    # three comment lines before the closing divider — only the first line's
    # leading words decide the tier.
    text = (
        "module QGroundControl.PluginUI\n\n"
        f"{_DIVIDER}\n"
        "# Unstable tier — map and mission visuals. Published for lint-time\n"
        "# resolvability only; freezing a core controller's QML surface is not a\n"
        "# commitment the host can credibly keep.\n"
        f"{_DIVIDER}\n"
        "FlightMap 1.0 FlightMap.qml\n"
    )
    qmldir = parse_qmldir(text)

    tiers = _tier_by_name(qmldir, text)

    assert tiers == {"FlightMap": "unstable"}


def test_every_header_variant_already_in_the_real_roster_is_recognized():
    # Guards the recognizer against being too narrow: both tier headers as
    # they actually appear in src/PluginSystem/PluginUI/qmldir (divider
    # above and below, leading tier words, trailing descriptive text) must
    # resolve.
    text = (
        "module QGroundControl.PluginUI\n\n"
        f"{_DIVIDER}\n# Frozen tier — controls\n{_DIVIDER}\n"
        "QGCLabel 1.0 QGCLabel.qml\n\n"
        f"{_DIVIDER}\n# Unstable tier — map and mission visuals.\n{_DIVIDER}\n"
        "FlightMap 1.0 FlightMap.qml\n"
    )
    qmldir = parse_qmldir(text)

    tiers = _tier_by_name(qmldir, text)

    assert tiers == {"QGCLabel": "frozen", "FlightMap": "unstable"}
