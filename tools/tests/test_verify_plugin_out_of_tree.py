#!/usr/bin/env python3
"""Tests for verify_plugin_out_of_tree.py — the SDK's out-of-tree plugin gate.

The gate this tool replaces was bash, and every trap it earned the hard way is a test case
here rather than a comment: `ctest` exits 0 on "No tests were found!!!"; `nm -u` of a
missing file reports no undefined symbols; an `import` appended after the root object is
not parsed as an import at all. Each of those is a silent pass, which is the only failure
mode a gate cannot recover from.
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

import pytest
from verify_plugin_out_of_tree import (
    GUARANTEED_QT_COMPONENTS,
    GateError,
    ManifestError,
    Runner,
    TriageEntry,
    assert_control_failed,
    build_marker_script_for,
    check_forbidden_symbols,
    check_marker_embedded,
    check_marker_not_embedded,
    check_marker_symbol_exported,
    check_qrc_closure,
    check_qt_components,
    expected_test_count,
    find_build_marker_source,
    find_cmake_lists,
    find_forbidden_symbols,
    find_qml_sources,
    flatten_qml_entries,
    host_plugin_dir,
    inject_import,
    parse_build_marker_source,
    parse_ctest_total,
    parse_manifest,
    parse_qrc,
    parse_qt_components,
    qmllint_error_lines,
    qmllint_syntax_lines,
    triage_qmllint_output,
    version_key,
)

REPO_ROOT = Path(__file__).resolve().parents[2]

VALID_MANIFEST = {
    "artifact_name": "SomePlugin",
    "qt_components": ["Core", "Quick"],
    "qml": {
        "enabled": True,
        "source_roots": ["qml"],
        "own_module": None,
        "accepted_triage": [],
    },
    "tests": {"enabled": False, "reason": "no suite"},
    "forbidden_symbols": {"enabled": False, "reason": "links nothing but the SDK"},
    "missing_dependency_control": {"enabled": False, "reason": "no packaged headers used"},
}


def _manifest(**overrides: object) -> dict[str, object]:
    return {**VALID_MANIFEST, **overrides}


# --- Manifest: every axis claimed or explicitly declared N/A -------------------------------


def test_parses_a_fully_declared_manifest():
    manifest = parse_manifest(
        _manifest(
            tests={"enabled": True, "cmake_lists": "test/CMakeLists.txt", "count_pattern": "^x"},
            forbidden_symbols={"enabled": True, "patterns": ["mavlink"], "reason": "headers-only"},
            qml={
                "enabled": True,
                "source_roots": ["qml"],
                "own_module": "QGroundControl.Something",
                "accepted_triage": [{"pattern": "QGeoCoordinate", "reason": "qmllint blind spot"}],
            },
        )
    )

    assert manifest.artifact_name == "SomePlugin"
    assert manifest.tests.enabled and manifest.tests.cmake_lists == "test/CMakeLists.txt"
    assert manifest.forbidden_symbols.patterns == ("mavlink",)
    assert manifest.qml.own_module == "QGroundControl.Something"
    assert manifest.qml.accepted_triage == (
        TriageEntry(pattern="QGeoCoordinate", reason="qmllint blind spot"),
    )


def test_a_schema_key_is_ignored_not_rejected():
    # Editor metadata, mirroring .github/build-config.json.
    assert parse_manifest({"$schema": "../x.json", **VALID_MANIFEST}).artifact_name == "SomePlugin"


@pytest.mark.parametrize(
    "mutation,message",
    [
        ({"artifact_name": None}, "missing required key"),
        ({"qml": None}, "missing required key"),
        ({"tests": None}, "missing required key"),
        ({"forbidden_symbols": None}, "missing required key"),
        ({"missing_dependency_control": None}, "missing required key"),
        ({"qt_components": None}, "missing required key"),
    ],
)
def test_a_missing_axis_is_an_error(mutation, message):
    # A key that simply isn't there would otherwise reduce the gate silently — the exact
    # "flags a plugin can forget" failure this manifest exists to replace.
    body = {key: value for key, value in VALID_MANIFEST.items() if key not in mutation}
    with pytest.raises(ManifestError, match=message):
        parse_manifest(body)


def test_an_unknown_key_is_an_error():
    # A claim the tool never reads is a claim nobody is checking.
    with pytest.raises(ManifestError, match="unknown key"):
        parse_manifest(_manifest(qml_lint_hook="tools/lint.sh"))


def test_an_unknown_key_inside_an_axis_is_an_error():
    with pytest.raises(ManifestError, match="unknown key"):
        parse_manifest(_manifest(tests={"enabled": False, "reason": "none", "skip": True}))


def test_an_enabled_axis_missing_its_payload_is_an_error():
    with pytest.raises(ManifestError, match="missing required key"):
        parse_manifest(_manifest(tests={"enabled": True}))


def test_a_disabled_axis_without_a_reason_is_an_error():
    # Opting out is allowed; opting out silently is not.
    with pytest.raises(ManifestError, match="missing required key"):
        parse_manifest(_manifest(forbidden_symbols={"enabled": False}))


def test_an_enabled_axis_may_not_carry_the_disabled_shape():
    with pytest.raises(ManifestError, match="unknown key"):
        parse_manifest(
            _manifest(
                missing_dependency_control={
                    "enabled": True,
                    "reason": "why",
                    "sdk_subpath": "include/x",
                    "build_target": "T",
                    "expected_pattern": "x",
                }
            )
        )


def test_an_empty_forbidden_symbol_pattern_list_is_an_error():
    # An enabled axis that resolves to zero work is the shape that passes by doing nothing.
    with pytest.raises(ManifestError, match="non-empty array"):
        parse_manifest(
            _manifest(forbidden_symbols={"enabled": True, "patterns": [], "reason": "r"})
        )


def test_empty_source_roots_are_an_error():
    with pytest.raises(ManifestError, match="non-empty array"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": [],
                    "own_module": None,
                    "accepted_triage": [],
                }
            )
        )


def test_a_malformed_triage_regex_is_an_error():
    with pytest.raises(ManifestError, match="not a valid regular expression"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "own_module": None,
                    "accepted_triage": [{"pattern": "([", "reason": "typo"}],
                }
            )
        )


def test_own_module_must_be_declared_even_when_absent():
    with pytest.raises(ManifestError, match="missing required key"):
        parse_manifest(
            _manifest(qml={"enabled": True, "source_roots": ["qml"], "accepted_triage": []})
        )


def test_the_qml_axis_can_be_declared_na_like_every_other_axis():
    # A C++-only plugin must be able to say so. The invariant is "every axis claimed or
    # explicitly declared N/A" — an axis with no off switch forces a plugin to fail the gate
    # for having nothing to check, which is the mirror image of a silent skip.
    manifest = parse_manifest(_manifest(qml={"enabled": False, "reason": "ships no QML"}))

    assert manifest.qml.enabled is False
    assert manifest.qml.reason == "ships no QML"


def test_the_bundled_reference_manifest_is_valid():
    raw = json.loads((REPO_ROOT / "plugins" / "example" / "plugin-verify.json").read_text())
    assert parse_manifest(raw).artifact_name == "ExamplePlugin"


# --- The C++ axis: declared vs guaranteed ----------------------------------------------------

DUAL_MODE_CMAKE = """\
project(SomePlugin VERSION 1.0.0 LANGUAGES CXX)
if(PROJECT_IS_TOP_LEVEL)
    find_package(QGCPluginAPI 2 REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick)
endif()
"""


def test_parses_the_components_a_plugin_actually_asks_for():
    assert parse_qt_components(DUAL_MODE_CMAKE) == ("Core", "Quick")


def test_ignores_a_non_qt6_find_package():
    # find_package(QGCPluginAPI 2 REQUIRED) must not be read as a component list.
    assert "QGCPluginAPI" not in parse_qt_components(DUAL_MODE_CMAKE)


def test_stops_at_optional_components():
    text = "find_package(Qt6 REQUIRED COMPONENTS Core OPTIONAL_COMPONENTS Test)\n"
    assert parse_qt_components(text) == ("Core",)


def test_a_plugin_with_no_qt6_find_package_is_an_error():
    # Zero components is not "no Qt dependency" — it is an axis with nothing to compare.
    with pytest.raises(GateError, match="no find_package"):
        parse_qt_components("project(SomePlugin)\n")


def test_a_manifest_that_drifts_from_the_cmakelists_is_an_error():
    with pytest.raises(GateError, match="does not match"):
        check_qt_components(["Core"], DUAL_MODE_CMAKE)


def test_an_unguaranteed_component_is_an_error():
    text = "find_package(Qt6 REQUIRED COMPONENTS Core Charts)\n"
    with pytest.raises(GateError, match="does not guarantee: Charts"):
        check_qt_components(["Core", "Charts"], text)


def test_a_guaranteed_declaration_passes():
    check_qt_components(["Core", "Quick"], DUAL_MODE_CMAKE)


def test_a_duplicate_declaration_is_an_error():
    # The comparison below is set-based, so a duplicate would be invisible in it.
    with pytest.raises(ManifestError, match="more than once"):
        check_qt_components(["Core", "Quick", "Core"], DUAL_MODE_CMAKE)


def test_a_paren_inside_a_comment_does_not_truncate_the_component_list():
    # The call-matching regex stops at the first ')', so a comment containing one used to
    # cut the list short — and the resulting mismatch message names the CMake call as
    # authoritative, inviting an author to "fix" the manifest and drop a real dependency.
    text = (
        "find_package(Qt6 REQUIRED COMPONENTS\n"
        "    Core\n"
        "    # only for the desktop arm (see QTBUG-1234)\n"
        "    Charts\n"
        ")\n"
    )
    assert parse_qt_components(text) == ("Core", "Charts")
    with pytest.raises(GateError, match="does not guarantee: Charts"):
        check_qt_components(["Core", "Charts"], text)


def test_a_commented_out_find_package_is_not_read_as_live():
    text = (
        "# find_package(Qt6 REQUIRED COMPONENTS Core Widgets)\n"
        "find_package(Qt6 REQUIRED COMPONENTS Core)\n"
    )
    assert parse_qt_components(text) == ("Core",)


def test_a_trailing_comment_is_not_read_as_component_names():
    # `#` ends the token it starts, not the line — skipping only that word would parse the
    # prose after it as components and fail a perfectly valid CMakeLists.
    text = (
        "find_package(Qt6 REQUIRED COMPONENTS\n"
        "    Core\n"
        "    # Quick is only needed by the in-tree arm\n"
        "    Network\n"
        ")\n"
    )
    assert parse_qt_components(text) == ("Core", "Network")


def test_a_subdirectory_find_package_is_not_invisible(tmp_path: Path):
    # A find_package(Qt6 ...) one directory down resolves against the developer's full Qt
    # exactly as happily as one in the root, so a root-only scan leaves the axis blind to
    # precisely the dependency it exists to catch.
    (tmp_path / "CMakeLists.txt").write_text(DUAL_MODE_CMAKE)
    (tmp_path / "test").mkdir()
    (tmp_path / "test" / "CMakeLists.txt").write_text(
        "find_package(Qt6 REQUIRED COMPONENTS Charts)\n"
    )

    texts = "\n".join(path.read_text() for path in find_cmake_lists(tmp_path))
    assert "Charts" in parse_qt_components(texts)
    with pytest.raises(GateError, match="does not guarantee: Charts"):
        check_qt_components(["Core", "Quick", "Charts"], texts)


def test_the_cmake_scan_skips_the_gates_own_build_tree_but_only_at_the_root(tmp_path: Path):
    (tmp_path / "CMakeLists.txt").write_text(DUAL_MODE_CMAKE)
    (tmp_path / "build-standalone").mkdir()
    (tmp_path / "build-standalone" / "CMakeLists.txt").write_text("generated\n")
    (tmp_path / "src" / "build").mkdir(parents=True)
    (tmp_path / "src" / "build" / "CMakeLists.txt").write_text("real source\n")

    found = {path.relative_to(tmp_path).as_posix() for path in find_cmake_lists(tmp_path)}
    assert found == {"CMakeLists.txt", "src/build/CMakeLists.txt"}


def test_the_guaranteed_component_set_matches_the_hosts_cmakelists():
    # The tool ships a second copy of the host's REQUIRED component list because it must run
    # from an SDK zip with no checkout. This is the tripwire that keeps the copy honest —
    # when the repo is present, the two must agree exactly.
    text = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"find_package\(Qt6\s*\n\s*\$\{QGC_QT_MINIMUM_VERSION\}[^\n]*\n\s*REQUIRED\s*\n"
        r"\s*COMPONENTS\s*\n(?P<required>.*?)\n\s*OPTIONAL_COMPONENTS\s*\n(?P<optional>.*?)\n\)",
        text,
        re.DOTALL,
    )
    assert match is not None, "the host's COMPONENTS block moved — re-anchor this test"
    declared = {
        word
        for group in ("required", "optional")
        for line in match.group(group).splitlines()
        for word in line.split()
        if not word.startswith("#")
    }

    assert declared == set(GUARANTEED_QT_COMPONENTS)


# --- The test axis -----------------------------------------------------------------------------


def test_expected_count_is_derived_from_the_plugins_own_cmake():
    text = "add_test(NAME A COMMAND A)\nadd_test(NAME B COMMAND B)\n"
    assert expected_test_count(text, r"^add_test\(") == 2


def test_a_pattern_that_matches_nothing_is_an_error():
    # A count of zero makes the comparison vacuous, so an empty suite would read as a pass.
    with pytest.raises(GateError, match="matched nothing"):
        expected_test_count("# no tests here\n", r"^add_test\(")


def test_ctest_reporting_no_tests_reads_as_zero_not_as_success():
    # Verbatim `ctest --test-dir <empty suite> -N` output, which exits 0. The count, not the
    # exit code, is the measurement.
    assert parse_ctest_total("Test project /tmp/build\n\nTotal Tests: 0\n") == 0


def test_ctest_output_with_no_total_line_is_an_error():
    # Silence must not be able to look like a measurement of zero.
    with pytest.raises(GateError, match="no 'Total Tests:' line"):
        parse_ctest_total("No tests were found!!!\n")


def test_ctest_total_is_read_from_the_summary_line():
    listing = "Test  #1: A\nTest  #2: B\n\nTotal Tests: 2\n"
    assert parse_ctest_total(listing) == 2


# --- The forbidden-symbol axis --------------------------------------------------------------------


def test_finds_forbidden_symbols_case_insensitively():
    output = "                 U _mavlink_msg_heartbeat_decode\n                 U _qt_something\n"
    assert find_forbidden_symbols(output, ["mavlink"]) == ["U _mavlink_msg_heartbeat_decode"]


def test_a_clean_artifact_passes(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    check_forbidden_symbols(artifact, ["mavlink"], lambda _: (0, "     U _qt_thing\n"))


def test_a_missing_artifact_is_a_failure_not_a_clean_run(tmp_path: Path):
    # `nm -u <missing file> | grep …` reports "no undefined symbols" just as cheerfully as a
    # genuinely clean binary. A check that passes when its input is missing is not a check.
    def nm(_path: Path) -> tuple[int, str]:
        raise AssertionError("nm must not be reached when the artifact is missing")

    with pytest.raises(GateError, match="nothing to check"):
        check_forbidden_symbols(tmp_path / "absent.dylib", ["mavlink"], nm)


def test_an_artifact_nm_cannot_read_is_a_failure_not_a_clean_run(tmp_path: Path):
    # Measured: nm exits 1 with "The file was not recognized as a valid object file", which
    # matches no forbidden pattern. Without the status check, a binary nm could not parse is
    # indistinguishable from a clean one — the same trap as the missing file, one step later.
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"not a mach-o")
    with pytest.raises(GateError, match="could not read"):
        check_forbidden_symbols(
            artifact, ["mavlink"], lambda _: (1, "nm: error: not a valid object file\n")
        )


def test_an_empty_symbol_listing_is_broken_instrumentation_not_cleanliness(tmp_path: Path):
    # A linked shared library always has undefined symbols. None at all means the
    # measurement failed, whatever the exit status said.
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    with pytest.raises(GateError, match="broken measurement"):
        check_forbidden_symbols(artifact, ["mavlink"], lambda _: (0, "\n"))


def test_a_forbidden_symbol_fails_the_gate(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    with pytest.raises(GateError, match="forbidden undefined symbol"):
        check_forbidden_symbols(artifact, ["mavlink"], lambda _: (0, "     U _mavlink_x\n"))


# --- The build-marker axis ----------------------------------------------------------------------------

MARKER_SOURCE = (
    "// Generated by qgc_plugin_build_marker(). Do not edit.\n"
    "#include <QtCore/QtGlobal>\n"
    'extern "C" Q_DECL_EXPORT const char* qgcPluginBuildMarker() {\n'
    '    return "20260814223000-ABCDEF0123456789";\n'
    "}\n"
)


def _marker_source(value: str) -> str:
    return MARKER_SOURCE.replace("20260814223000-ABCDEF0123456789", value)


def test_reads_the_marker_the_generator_wrote():
    assert parse_build_marker_source(MARKER_SOURCE) == "20260814223000-ABCDEF0123456789"


def test_a_marker_source_with_no_literal_is_an_error():
    with pytest.raises(GateError, match="no string literal"):
        parse_build_marker_source('extern "C" const char* qgcPluginBuildMarker() { return x; }')


def test_an_empty_marker_is_an_error_not_a_marker():
    # An empty marker is exactly what the host reads for a plugin that carries none, so it
    # must not be able to pass as one that does.
    with pytest.raises(GateError, match="empty string"):
        parse_build_marker_source(_marker_source(""))


def test_a_standalone_build_with_no_marker_source_is_an_error(tmp_path: Path):
    # The failure U1b exists for: the plugin's standalone CMakeLists.txt never called the
    # SDK's generator, so only the out-of-tree build — the one that ships — is unmarked.
    with pytest.raises(GateError, match="never called qgc_plugin_build_marker"):
        find_build_marker_source(tmp_path)


def test_a_marker_never_added_to_the_target_says_so(tmp_path: Path):
    # The generator script is written at configure time by the call itself, so its presence
    # proves the call was made — the mistake is one line further on, and the message has to
    # say which of the two it is.
    (tmp_path / "SomePlugin_GenerateBuildMarker.cmake").write_text("# generator\n")
    with pytest.raises(GateError, match="never added to the plugin target"):
        find_build_marker_source(tmp_path)


def test_the_marker_source_is_found_anywhere_in_the_build_tree(tmp_path: Path):
    nested = tmp_path / "sub" / "dir"
    nested.mkdir(parents=True)
    (nested / "SomePlugin_BuildMarker.cc").write_text(MARKER_SOURCE)
    assert find_build_marker_source(tmp_path).name == "SomePlugin_BuildMarker.cc"


def test_two_marker_sources_are_an_error(tmp_path: Path):
    (tmp_path / "OnePlugin_BuildMarker.cc").write_text(MARKER_SOURCE)
    (tmp_path / "TwoPlugin_BuildMarker.cc").write_text(MARKER_SOURCE)
    with pytest.raises(GateError, match="more than one build-marker source"):
        find_build_marker_source(tmp_path)


def test_a_missing_generator_script_is_an_error(tmp_path: Path):
    source = tmp_path / "SomePlugin_BuildMarker.cc"
    source.write_text(MARKER_SOURCE)
    with pytest.raises(GateError, match="no marker generator script"):
        build_marker_script_for(source)


def test_the_generator_script_sits_beside_its_output(tmp_path: Path):
    source = tmp_path / "SomePlugin_BuildMarker.cc"
    source.write_text(MARKER_SOURCE)
    script = tmp_path / "SomePlugin_GenerateBuildMarker.cmake"
    script.write_text("# generator\n")
    assert build_marker_script_for(source) == script


def test_an_exported_marker_symbol_passes(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    check_marker_symbol_exported(
        artifact, lambda _: (0, "0000000000003f10 T _qgcPluginBuildMarker\n")
    )


def test_an_unexported_marker_symbol_fails(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    with pytest.raises(GateError, match="exports no qgcPluginBuildMarker"):
        check_marker_symbol_exported(artifact, lambda _: (0, "0000000000003f10 T _somethingElse\n"))


def test_a_missing_artifact_is_not_a_missing_marker(tmp_path: Path):
    def nm(_path: Path) -> tuple[int, str]:
        raise AssertionError("nm must not be reached when the artifact is missing")

    with pytest.raises(GateError, match="no standalone artifact"):
        check_marker_symbol_exported(tmp_path / "absent.dylib", nm)


def test_nm_failing_is_not_a_missing_marker(tmp_path: Path):
    # This axis fails on "symbol not found", which is what broken instrumentation also
    # looks like — so the instrument is checked first and reported as itself.
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"not a mach-o")
    with pytest.raises(GateError, match="could not read"):
        check_marker_symbol_exported(artifact, lambda _: (1, "nm: error: not an object file\n"))


def test_an_empty_defined_symbol_listing_is_broken_instrumentation(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"")
    with pytest.raises(GateError, match="broken instrumentation"):
        check_marker_symbol_exported(artifact, lambda _: (0, "\n"))


def test_the_generated_marker_must_be_inside_the_linked_artifact(tmp_path: Path):
    # A regenerated marker that never reached the binary is the in-place-upgrade failure in
    # miniature: new value on disk, old value executing.
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"\x00\x00stale-marker\x00")
    check_marker_embedded(artifact, "stale-marker", "label")
    with pytest.raises(GateError, match="never reached the linked binary"):
        check_marker_embedded(artifact, "fresh-marker", "label")


def test_the_previous_marker_must_be_gone_after_a_relink(tmp_path: Path):
    artifact = tmp_path / "libSomePlugin.dylib"
    artifact.write_bytes(b"\x00\x00stale-marker\x00")
    check_marker_not_embedded(artifact, "fresh-marker", "label")
    with pytest.raises(GateError, match="still contains the previous marker"):
        check_marker_not_embedded(artifact, "stale-marker", "label")


# --- The QML axis: the .qrc closure ------------------------------------------------------------------

QRC = """\
<RCC>
    <qresource prefix="/qml">
        <file alias="A.qml">qml/views/A.qml</file>
        <file alias="B.qml">qml/components/B.qml</file>
    </qresource>
    <qresource prefix="/qmlimages">
        <file alias="plugin.svg">images/plugin.svg</file>
    </qresource>
</RCC>
"""


def test_flattens_the_layout_that_actually_ships():
    # A file in qml/views/ resolves a sibling in qml/components/ by same-directory lookup at
    # runtime; linting the source tree instead reports those siblings as "not found".
    assert flatten_qml_entries(parse_qrc(QRC)) == {
        "A.qml": "qml/views/A.qml",
        "B.qml": "qml/components/B.qml",
    }


def test_an_unaliased_qml_entry_is_an_error():
    # It still ships, just to a path no sibling resolves through — broken at runtime and
    # invisible to a scan that only looks for `alias=`.
    text = QRC.replace('<file alias="B.qml">', "<file>")
    with pytest.raises(GateError, match="no alias="):
        flatten_qml_entries(parse_qrc(text))


def test_two_entries_sharing_an_alias_are_an_error():
    text = QRC.replace('alias="B.qml"', 'alias="A.qml"')
    with pytest.raises(GateError, match=r"two different files to 'A\.qml'"):
        flatten_qml_entries(parse_qrc(text))


def test_single_quoted_attributes_parse(tmp_path: Path):
    # The bash this replaces matched `alias="…"` textually, so a reformatted .qrc silently
    # staged nothing and qmllint over an empty file list exited 0.
    text = QRC.replace('alias="A.qml"', "alias='A.qml'")
    assert "A.qml" in flatten_qml_entries(parse_qrc(text))


def test_a_qrc_entry_with_no_file_on_disk_is_an_error(tmp_path: Path):
    with pytest.raises(GateError, match="do not exist"):
        check_qrc_closure(["qml/A.qml"], [], tmp_path)


def test_a_qml_file_the_qrc_does_not_ship_is_an_error(tmp_path: Path):
    (tmp_path / "qml").mkdir()
    (tmp_path / "qml" / "A.qml").write_text("import QtQuick\nItem {}\n")
    (tmp_path / "qml" / "Orphan.qml").write_text("import QtQuick\nItem {}\n")
    with pytest.raises(GateError, match="does not ship"):
        check_qrc_closure(["qml/A.qml"], ["qml/A.qml", "qml/Orphan.qml"], tmp_path)


def test_an_empty_shipped_set_is_an_error(tmp_path: Path):
    # Positive control on the gate's own input: qmllint with no files prints usage, emits no
    # diagnostics and exits 0, so an empty set would read as a clean lint.
    with pytest.raises(GateError, match="ships no QML at all"):
        check_qrc_closure([], [], tmp_path)


def test_a_prefix_without_a_leading_slash_is_the_same_prefix():
    # rcc normalises it; the closure check must too, or a valid .qrc fails for a reason
    # that has nothing to do with the plugin.
    text = QRC.replace('prefix="/qml"', 'prefix="qml"')
    assert "A.qml" in flatten_qml_entries(parse_qrc(text))


def test_a_qml_entry_outside_the_qml_prefix_is_an_error():
    # It still ships, just where no sibling resolves it — as broken as an unaliased entry,
    # and just as invisible to a scan that only looks at the prefix it expected.
    text = QRC.replace('<qresource prefix="/qmlimages">', '<qresource prefix="/other">').replace(
        '<file alias="plugin.svg">images/plugin.svg</file>',
        '<file alias="Stray.qml">qml/Stray.qml</file>',
    )
    with pytest.raises(GateError, match="outside the '/qml' prefix"):
        flatten_qml_entries(parse_qrc(text))


def test_source_scan_skips_the_gates_own_build_tree_but_only_at_the_root(tmp_path: Path):
    # The gate builds inside the very tree it scans, so its own build-standalone/ must go.
    # A nested directory that merely happens to be named build/ is real plugin source: the
    # bash anchored this exclusion for exactly that reason, and dropping a real source dir
    # would mean verifying a plugin that is not the one that ships.
    (tmp_path / "qml").mkdir()
    (tmp_path / "qml" / "A.qml").write_text("Item {}\n")
    (tmp_path / "build-standalone" / "qml").mkdir(parents=True)
    (tmp_path / "build-standalone" / "qml" / "Generated.qml").write_text("Item {}\n")
    (tmp_path / "src" / "build").mkdir(parents=True)
    (tmp_path / "src" / "build" / "Real.qml").write_text("Item {}\n")

    assert find_qml_sources(tmp_path, ["."]) == ["qml/A.qml", "src/build/Real.qml"]


def test_source_roots_must_exist(tmp_path: Path):
    with pytest.raises(GateError, match="not a directory"):
        find_qml_sources(tmp_path, ["qml"])


# --- The QML axis: triage and injection -----------------------------------------------------------

LINT_OUTPUT = """\
Warning: A.qml:3:3: Detected height on an item managed by a layout [Quick.layout-positioning]
Error: A.qml:4:8: Type "QGeoCoordinate" of property "coordinate" not found [unresolved-type]
Error: B.qml:2:1: Failed to import QGroundControl.Controls. [import]
Info: B.qml:2:1: Unused import [unused-imports]
"""


def test_triage_accepts_only_named_classes_and_only_error_lines():
    unaccepted, unused = triage_qmllint_output(
        LINT_OUTPUT, [TriageEntry(pattern="QGeoCoordinate", reason="qmllint blind spot")]
    )

    assert unaccepted == ["Error: B.qml:2:1: Failed to import QGroundControl.Controls. [import]"]
    assert unused == []


def test_triage_reports_an_entry_that_matched_nothing():
    # A triage list that only ever grows is how a stale exception ends up swallowing real
    # diagnostics — this repo lost 30 files' worth of them to exactly that.
    _, unused = triage_qmllint_output(
        LINT_OUTPUT,
        [
            TriageEntry(pattern="QGeoCoordinate", reason="qmllint blind spot"),
            TriageEntry(pattern="LongGoneType", reason="fixed three releases ago"),
        ],
    )

    assert [entry.pattern for entry in unused] == ["LongGoneType"]


def test_triage_does_not_silence_warnings_into_errors():
    unaccepted, _ = triage_qmllint_output(LINT_OUTPUT, [])
    assert all(line.startswith("Error: ") for line in unaccepted)
    assert len(unaccepted) == 2


def test_a_syntax_error_produces_no_error_line_at_all():
    # Measured against Qt 6.11.1: qmllint reports a parse error as `Warning: ... [syntax]`
    # and exits 255, and no severity flag promotes it. So a pass decided purely from the
    # "Error: " lines calls a file that cannot even be parsed clean.
    syntax_only = (
        "Warning: A.qml:4:18: Expected token `identifier' [syntax]\n    property int = 3\n"
    )
    assert qmllint_error_lines(syntax_only) == []
    assert triage_qmllint_output(syntax_only, []) == ([], [])
    # Which is why it is detected by class, not by absence of error lines:
    assert len(qmllint_syntax_lines(syntax_only)) == 1


def test_a_syntax_error_is_still_seen_when_a_triaged_error_explains_the_exit_code():
    # The trap in the first attempt at this guard. An accepted-triage entry must match
    # something every run, so for any plugin that has one the exit code is non-zero and the
    # error-line list is non-empty by construction — a guard keyed on "non-zero exit AND no
    # error lines" is therefore permanently disarmed for exactly the plugins with the most
    # QML. Verbatim shape of a real mixed run (Qt 6.11.1, rc 255).
    mixed = (
        "Warning: Broken.qml:4:18: Expected token `identifier' [syntax]\n"
        'Error: Triaged.qml:4:53: Type "QGeoCoordinate" of property "c" not found [import]\n'
    )
    triage = [TriageEntry(pattern="QGeoCoordinate", reason="qmllint blind spot")]
    unaccepted, unused = triage_qmllint_output(mixed, triage)

    assert unaccepted == [] and unused == []  # triage is satisfied; nothing to report
    assert qmllint_error_lines(mixed) != []  # ...and the exit code is fully "explained"
    assert len(qmllint_syntax_lines(mixed)) == 1  # only the class check still sees it


def test_injected_import_lands_inside_the_import_block():
    # Appended after the root object it is not parsed as an import at all, and qmllint says
    # nothing — a control that always "passes" while testing nothing.
    source = "import QtQuick\nimport QtQuick.Layouts\n\nItem {\n    id: root\n}\n"
    injected = inject_import(source, "QGroundControl.Controls")
    lines = injected.splitlines()

    assert "import QGroundControl.Controls" in lines
    assert lines.index("import QGroundControl.Controls") < lines.index("Item {")


def test_injecting_into_a_file_with_no_imports_is_an_error():
    with pytest.raises(GateError, match="no import statement"):
        inject_import("Item {\n}\n", "QGroundControl.Controls")


# --- Negative-control assertion -----------------------------------------------------------------------


def test_a_control_that_exits_zero_is_a_failed_gate():
    with pytest.raises(GateError, match="decorative"):
        assert_control_failed("control", 0, "everything fine", "ToolStrip was not found")


def test_a_control_that_fails_for_the_wrong_reason_is_a_failed_gate():
    # A typo in a fixture path, a missing Qt module, any unrelated breakage exits non-zero
    # too — and the control would then "pass" while proving nothing.
    with pytest.raises(GateError, match="not testing what it claims"):
        assert_control_failed("control", 1, "cmake: command not found", "ToolStrip was not found")


def test_a_control_that_fails_for_the_named_reason_passes():
    assert_control_failed(
        "control", 1, "Error: ToolStrip was not found.", "ToolStrip was not found"
    )


def test_the_base_module_control_is_decided_by_the_gates_verdict_not_raw_qmllint_output():
    # Both raw signals lie. For any plugin with a triage entry qmllint's exit code is
    # non-zero on every run by construction (an entry matching nothing is itself a failure),
    # and the module name appears in the output whenever qmllint echoes a source line —
    # including a merely-unused import that resolved fine. Reduced to the unaccepted set,
    # both assertions mean what they say. This replays that exact shape.
    output = (
        'Error: A.qml:4:8: Type "QGeoCoordinate" of property "coordinate" not found\n'
        "Info: A.qml:2:1: Unused import [unused-imports]\n"
        "import QGroundControl.Controls\n"
    )
    unaccepted, _ = triage_qmllint_output(
        output, [TriageEntry(pattern="QGeoCoordinate", reason="qmllint blind spot")]
    )

    # Raw qmllint would satisfy both assertions here, and the control would claim a pass.
    assert unaccepted == []
    assert re.search(re.escape("QGroundControl.Controls"), output) is not None
    # Decided from the gate's verdict instead, it correctly reports the gate as decorative.
    with pytest.raises(GateError, match="decorative"):
        assert_control_failed(
            "negative control (base-module reach)",
            1 if unaccepted else 0,
            "\n".join(unaccepted) or output,
            re.escape("QGroundControl.Controls"),
        )


# --- The gate must not touch the developer's environment ----------------------------------------------


def test_the_runner_hands_its_environment_to_child_processes(tmp_path: Path):
    # A plugin's CMake may deploy itself on POST_BUILD, so every child build runs against a
    # scratch HOME: verifying a plugin must never overwrite the copy the developer is
    # running, least of all with a deliberately crippled negative-control build.
    runner = Runner(env={**os.environ, "HOME": str(tmp_path)}, verbose=False)
    returncode, output = runner.capture(
        sys.executable, "-c", "import os; print(os.environ['HOME'])"
    )

    assert returncode == 0
    assert output.strip() == str(tmp_path)


def test_qt_prefixes_are_ordered_by_version_not_by_string():
    # '6.9.0' > '6.11.1' lexicographically, so a developer keeping an older Qt beside a
    # newer one would silently get every run built and linted against the old one.
    assert sorted(["6.9.0", "6.11.1", "6.10.2"], key=version_key)[-1] == "6.11.1"


def test_the_deploy_dir_distinguishes_a_daily_build_from_a_stable_one():
    stable = host_plugin_dir("QGroundControl", "QGroundControl", stable_build=True)
    daily = host_plugin_dir("QGroundControl", "QGroundControl", stable_build=False)

    assert stable.name == "plugins" and stable.parent.name == "QGroundControl"
    assert daily.parent.name == "QGroundControl Daily"
