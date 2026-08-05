#!/usr/bin/env python3
"""
QML contract watchdog for QGroundControl.PluginUI (out-of-tree-plugins.md U5).

The QML half of the plugin ABI has no vtable: a renamed property on a published
control fails at panel-load time in someone else's build, invisible to this
repo's CI. This script is that watchdog. It diffs the current derived module
(qmldir roster + .qmltypes, produced by tools/derive_plugin_ui_sdk.py) against a
committed golden snapshot, per stability tier:

  * frozen tier   — a removed type, or one whose shape changed, fails the check.
                     An addition passes.
  * unstable tier — any change is printed as a report, never fails.

Both artifacts are compared because composite (.qml) types never appear in a
.qmltypes at all (Qt generates no metadata for them), so a .qmltypes diff alone
is blind to the control roster, and a qmldir diff alone is blind to a C++ type's
property/signal/method shape.

Tier membership for the qmldir's .qml/singleton entries comes from the roster's
own `# ---...---`-delimited section headers ("Frozen tier" / "Unstable tier"),
which are the single source of truth carried into both the host and SDK
qmldirs. An unrecognized or missing header raises rather than silently
inheriting the previous section's tier — see _tier_by_name. The four C++
types (src/PluginSystem/CMakeLists.txt's QGC_PLUGIN_UI_CPP_TYPES) carry no such
comment — they are re-registered under the second URI, not roster lines — so
their tier is declared here directly, mirroring
../docs/plugin-architecture-end-state.md Part 3b (the canonical tier roster):
QGCPalette, QGCFileDialogController and QGCPluginUIGlobal are frozen;
PlanMasterController is the one C++ type in the unstable map/mission tier.

Usage:
    python3 tools/check_plugin_ui_contract.py \
        --golden test/PluginSystem/golden \
        --current <build>/plugin-sdk/qml/QGroundControl/PluginUI

    python3 tools/check_plugin_ui_contract.py --golden <dir> --current <dir> --update
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from derive_plugin_ui_sdk import QmlDir, TypeEntry, _split_components, parse_qmldir

QMLDIR_NAME = "QGroundControl.PluginUI.qmldir"
QMLTYPES_NAME = "QGroundControl.PluginUI.qmltypes"

# Part 3b: the one C++ type in the unstable map/mission tier. Every other C++
# type re-registered under this module (QGCPalette, QGCFileDialogController,
# QGCPluginUIGlobal) is frozen tier.
UNSTABLE_CPP_TYPES = frozenset({"PlanMasterController"})


_DIVIDER = "# " + "-" * 75


def _tier_by_name(qmldir: QmlDir, source_text: str) -> dict[str, str]:
    """Map each roster entry's type name to "frozen" or "unstable".

    Walks the raw lines in lockstep with the roster's `# ---...---`-delimited
    section-header blocks (the divider convention every section already
    uses). The single header line between two dividers must start with
    "frozen tier" or "unstable tier" — every OTHER divider-delimited block is
    a parse error, and so is a type entry appearing before any block, or a
    block whose tier word can't be recognized.

    This raises rather than guesses on purpose. Carrying the previous tier
    forward past an unrecognized header (matching "frozen tier"/"unstable
    tier" as a loose substring anywhere in the comment, e.g.) is exactly how
    a later section using different phrasing — "# Newly published controls",
    say — would silently misclassify a frozen-tier type as unstable and let
    a breaking change through as a non-fatal report, which is the one
    failure this whole script exists to prevent.
    """
    tier: str | None = None
    tiers: dict[str, str] = {}
    names_remaining = [e.name for e in qmldir.entries]
    index = 0
    lines = source_text.splitlines()
    lineno = 0
    while lineno < len(lines):
        raw = lines[lineno]
        stripped = raw.strip()
        lineno += 1

        if stripped == _DIVIDER:
            if lineno >= len(lines):
                raise ValueError(f"line {lineno}: divider with no header line after it")
            first_header_line = lines[lineno].strip().lstrip("#").strip().lower()
            header_start_lineno = lineno + 1
            # The header may run over several comment lines (prose describing
            # the tier) before the closing divider; only the first line's
            # leading words decide the tier.
            while lineno < len(lines) and lines[lineno].strip() != _DIVIDER:
                if not lines[lineno].strip().startswith("#"):
                    raise ValueError(
                        f"line {lineno + 1}: expected a comment line or closing "
                        "divider inside a section header block"
                    )
                lineno += 1
            if lineno >= len(lines):
                raise ValueError(
                    f"line {header_start_lineno}: section header not closed by a matching divider"
                )
            lineno += 1
            if first_header_line.startswith("frozen tier"):
                tier = "frozen"
            elif first_header_line.startswith("unstable tier"):
                tier = "unstable"
            else:
                raise ValueError(
                    f"line {header_start_lineno}: section header "
                    f"{lines[header_start_lineno - 1].strip()!r} does not start with "
                    "'Frozen tier' or 'Unstable tier'"
                )
            continue

        if not stripped or stripped.startswith("#"):
            continue

        tokens = stripped.split()
        first = tokens[1] if tokens[0] == "singleton" else tokens[0]
        if index < len(names_remaining) and first == names_remaining[index]:
            if tier is None:
                raise ValueError(
                    f"line {lineno}: type {first!r} appears before any "
                    "tier section header — cannot determine its stability tier"
                )
            tiers[first] = tier
            index += 1

    if index != len(names_remaining):
        raise ValueError(
            f"tier walk desynced: matched {index} of {len(names_remaining)} "
            "roster entries against source lines"
        )

    return tiers


def _component_blocks_by_name(qmltypes_text: str) -> dict[str, str]:
    open_brace = qmltypes_text.find("{", qmltypes_text.find("Module"))
    close_brace = qmltypes_text.rfind("}")
    blocks: dict[str, str] = {}
    for block in _split_components(qmltypes_text[open_brace + 1 : close_brace]):
        for line in block.splitlines():
            line = line.strip()
            if line.startswith('name:'):
                name = line.split('"')[1]
                blocks[name] = block
                break
    return blocks


def _entry_shape(entry: TypeEntry) -> tuple[str, str, bool]:
    return (entry.filename, entry.version, entry.singleton)


def diff_qmldir(golden: QmlDir, golden_text: str, current: QmlDir, current_text: str) -> tuple[list[str], list[str]]:
    """Return (failures, reports) comparing two roster-shaped qmldirs."""
    golden_tiers = _tier_by_name(golden, golden_text)
    current_tiers = _tier_by_name(current, current_text)
    golden_by_name = {e.name: e for e in golden.entries}
    current_by_name = {e.name: e for e in current.entries}

    failures: list[str] = []
    reports: list[str] = []

    for name, entry in golden_by_name.items():
        tier = golden_tiers.get(name, "frozen")
        if name not in current_by_name:
            message = f"qmldir: {tier}-tier type {name!r} was removed"
            (failures if tier == "frozen" else reports).append(message)
            continue
        if _entry_shape(entry) != _entry_shape(current_by_name[name]):
            message = (
                f"qmldir: {tier}-tier type {name!r} changed shape "
                f"(was {_entry_shape(entry)}, now {_entry_shape(current_by_name[name])})"
            )
            (failures if tier == "frozen" else reports).append(message)
        elif current_tiers.get(name) != tier:
            reports.append(
                f"qmldir: type {name!r} moved tier ({tier} -> {current_tiers.get(name)})"
            )

    for name in current_by_name:
        if name not in golden_by_name:
            tier = current_tiers.get(name, "frozen")
            reports.append(f"qmldir: {tier}-tier type {name!r} was added")

    return failures, reports


def diff_qmltypes(golden_text: str, current_text: str) -> tuple[list[str], list[str]]:
    """Return (failures, reports) comparing two .qmltypes files' Component blocks."""
    golden_blocks = _component_blocks_by_name(golden_text)
    current_blocks = _component_blocks_by_name(current_text)

    failures: list[str] = []
    reports: list[str] = []

    for name, block in golden_blocks.items():
        tier = "unstable" if name in UNSTABLE_CPP_TYPES else "frozen"
        if name not in current_blocks:
            message = f".qmltypes: {tier}-tier type {name!r} was removed"
            (failures if tier == "frozen" else reports).append(message)
            continue
        if block != current_blocks[name]:
            message = f".qmltypes: {tier}-tier type {name!r} changed shape"
            (failures if tier == "frozen" else reports).append(message)

    for name in current_blocks:
        if name not in golden_blocks:
            tier = "unstable" if name in UNSTABLE_CPP_TYPES else "frozen"
            reports.append(f".qmltypes: {tier}-tier type {name!r} was added")

    return failures, reports


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--golden", required=True, type=Path, help="dir holding the committed snapshot")
    parser.add_argument("--current", required=True, type=Path, help="dir holding the freshly derived module")
    parser.add_argument("--update", action="store_true", help="overwrite the golden snapshot with --current")
    args = parser.parse_args(argv)

    golden_qmldir_path = args.golden / QMLDIR_NAME
    golden_qmltypes_path = args.golden / QMLTYPES_NAME
    current_qmldir_path = args.current / "qmldir"
    current_qmltypes_path = args.current / QMLTYPES_NAME

    if args.update:
        golden_qmldir_path.parent.mkdir(parents=True, exist_ok=True)
        golden_qmldir_path.write_text(current_qmldir_path.read_text(encoding="utf-8"), encoding="utf-8")
        golden_qmltypes_path.write_text(current_qmltypes_path.read_text(encoding="utf-8"), encoding="utf-8")
        print(f"Updated golden snapshot in {args.golden}")
        return 0

    try:
        golden_qmldir_text = golden_qmldir_path.read_text(encoding="utf-8")
        current_qmldir_text = current_qmldir_path.read_text(encoding="utf-8")
        golden_qmltypes_text = golden_qmltypes_path.read_text(encoding="utf-8")
        current_qmltypes_text = current_qmltypes_path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    golden_qmldir = parse_qmldir(golden_qmldir_text)
    current_qmldir = parse_qmldir(current_qmldir_text)

    qmldir_failures, qmldir_reports = diff_qmldir(
        golden_qmldir, golden_qmldir_text, current_qmldir, current_qmldir_text
    )
    qmltypes_failures, qmltypes_reports = diff_qmltypes(golden_qmltypes_text, current_qmltypes_text)

    for report in qmldir_reports + qmltypes_reports:
        print(f"report (non-fatal): {report}")

    failures = qmldir_failures + qmltypes_failures
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        print(
            "\nA frozen-tier type was removed or changed shape. If this is a "
            "deliberate, version-bumped break, re-run with --update to accept "
            "the new snapshot. Otherwise this is the watchdog doing its job.",
            file=sys.stderr,
        )
        return 1

    print("QGroundControl.PluginUI contract check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
