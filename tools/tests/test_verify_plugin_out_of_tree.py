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
from pathlib import Path, PureWindowsPath

import pytest
import verify_plugin_out_of_tree as verifier
from verify_plugin_out_of_tree import (
    FORBIDDEN_SYMBOLS_NA_ON_WINDOWS,
    GUARANTEED_QT_COMPONENTS,
    GateContext,
    GateError,
    ManifestError,
    QmlModule,
    Runner,
    TriageEntry,
    artifact_filename,
    assert_control_failed,
    build_command,
    build_marker_script_for,
    check_forbidden_symbols,
    check_marker_embedded,
    check_marker_not_embedded,
    check_marker_symbol_exported,
    check_module_closure,
    check_qrc_closure,
    check_qt_components,
    choose_qt_prefix,
    configure_command,
    dumpbin_export_lister,
    exclude_module_sources,
    expected_test_count,
    find_build_marker_source,
    find_built_artifact,
    find_cmake_lists,
    find_forbidden_symbols,
    find_qml_sources,
    flatten_qml_entries,
    generator_args,
    host_plugin_dir,
    inject_import,
    is_desktop_kit,
    link_path,
    parse_build_marker_source,
    parse_ctest_total,
    parse_dumpbin_exports,
    parse_manifest,
    parse_qmldir_module_files,
    parse_qrc,
    parse_qt_components,
    qmllint_error_lines,
    qmllint_syntax_lines,
    replace_path,
    require_dumpbin,
    run_forbidden_symbol_axis,
    triage_qmllint_output,
    version_key,
    wait_for_next_mtime_tick,
)

REPO_ROOT = Path(__file__).resolve().parents[2]

VALID_MANIFEST = {
    "artifact_name": "SomePlugin",
    "qt_components": ["Core", "Quick"],
    "qml": {
        "enabled": True,
        "source_roots": ["qml"],
        "own_module": None,
        "modules": [],
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
                "modules": [
                    {"uri": "QGroundControl.Something.Foundation", "source_dir": "qml/foundation"}
                ],
                "accepted_triage": [{"pattern": "QGeoCoordinate", "reason": "qmllint blind spot"}],
            },
        )
    )

    assert manifest.artifact_name == "SomePlugin"
    assert manifest.tests.enabled and manifest.tests.cmake_lists == "test/CMakeLists.txt"
    assert manifest.forbidden_symbols.patterns == ("mavlink",)
    assert manifest.qml.own_module == "QGroundControl.Something"
    assert manifest.qml.modules == (
        QmlModule(uri="QGroundControl.Something.Foundation", source_dir="qml/foundation"),
    )
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
                    "modules": [],
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
                    "modules": [],
                    "accepted_triage": [{"pattern": "([", "reason": "typo"}],
                }
            )
        )


def test_own_module_must_be_declared_even_when_absent():
    with pytest.raises(ManifestError, match="missing required key"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "modules": [],
                    "accepted_triage": [],
                }
            )
        )


def test_modules_must_be_declared_even_when_empty():
    with pytest.raises(ManifestError, match="missing required key"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "own_module": None,
                    "accepted_triage": [],
                }
            )
        )


def test_a_module_and_own_module_may_not_name_the_same_uri():
    # A module is either type-registration-only (own_module) or ships its own .qml files
    # (modules), never both — declaring the same URI in both is a manifest that doesn't say
    # which shape the module actually is.
    with pytest.raises(ManifestError, match=r"both declare 'QGroundControl\.Something'"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "own_module": "QGroundControl.Something",
                    "modules": [
                        {"uri": "QGroundControl.Something", "source_dir": "qml/foundation"}
                    ],
                    "accepted_triage": [],
                }
            )
        )


def test_a_duplicate_module_uri_is_an_error():
    with pytest.raises(ManifestError, match="declared more than once"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "own_module": None,
                    "modules": [
                        {"uri": "QGroundControl.X.Foundation", "source_dir": "qml/foundation"},
                        {"uri": "QGroundControl.X.Foundation", "source_dir": "qml/other"},
                    ],
                    "accepted_triage": [],
                }
            )
        )


def test_a_duplicate_module_source_dir_is_an_error():
    with pytest.raises(ManifestError, match="declared more than once"):
        parse_manifest(
            _manifest(
                qml={
                    "enabled": True,
                    "source_roots": ["qml"],
                    "own_module": None,
                    "modules": [
                        {"uri": "QGroundControl.X.Foundation", "source_dir": "qml/foundation"},
                        {"uri": "QGroundControl.X.Components", "source_dir": "qml/foundation"},
                    ],
                    "accepted_triage": [],
                }
            )
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
        r"find_package\(\s*Qt6\s+\$\{QGC_QT_MINIMUM_VERSION\}[^\n]*\bREQUIRED\s*\n"
        r"\s*COMPONENTS\s*\n(?P<required>.*?)\n\s*OPTIONAL_COMPONENTS\s+(?P<optional>.*?)\n\)",
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


# --- The QML axis: plugin-owned modules (S1'/U0) ----------------------------------------------------


def test_exclude_module_sources_drops_files_under_a_declared_module():
    # A module file ships through its own module, never the flat prefix — the flat qrc
    # closure must not demand it.
    sources = [
        "qml/pages/SignInPage.qml",
        "qml/foundation/AcmeTheme.qml",
        "qml/foundation/AcmeLabel.qml",
    ]
    modules = [QmlModule(uri="QGroundControl.Acme.Foundation", source_dir="qml/foundation")]

    assert exclude_module_sources(sources, modules) == ["qml/pages/SignInPage.qml"]


def test_exclude_module_sources_is_a_no_op_with_no_declared_modules():
    sources = ["qml/pages/SignInPage.qml", "qml/foundation/AcmeTheme.qml"]

    assert exclude_module_sources(sources, []) == sources


def test_exclude_module_sources_only_matches_the_directory_not_a_name_prefix():
    # "qml/foundational" is not "qml/foundation" — a naive string prefix match would drop it
    # by mistake.
    sources = ["qml/foundational/Other.qml"]
    modules = [QmlModule(uri="QGroundControl.Acme.Foundation", source_dir="qml/foundation")]

    assert exclude_module_sources(sources, modules) == sources


def test_a_nested_sibling_modules_files_are_excluded_from_its_parents_own_set():
    # find_qml_sources walks a module's source_dir in full, so a sibling module nested
    # inside it (e.g. a Widgets sub-module under Foundation) would otherwise be swept into
    # the parent's own file set too — and the parent's real generated qmldir knows nothing
    # about the nested module's files, so check_module_closure would wrongly call them
    # undeclared. stage_qml_modules excludes every *other* declared module's files this way
    # before closing a given module over its own set.
    child = QmlModule(
        uri="QGroundControl.Acme.Foundation.Widgets", source_dir="qml/foundation/widgets"
    )
    sources = ["qml/foundation/AcmeTheme.qml", "qml/foundation/widgets/ExtraWidget.qml"]

    assert exclude_module_sources(sources, [child]) == ["qml/foundation/AcmeTheme.qml"]


GENERATED_MODULE_QMLDIR = """\
module QGroundControl.Acme.Foundation
typeinfo AcmeFoundationModule.qmltypes
prefer :/qml/QGroundControl/Acme/Foundation/
singleton AcmeTheme 1.0 AcmeTheme.qml
AcmeLabel 1.0 AcmeLabel.qml
AcmeButton 1.0 AcmeButton.qml
depends QtQuick
"""


def test_parses_the_files_a_generated_qmldir_declares():
    assert parse_qmldir_module_files(GENERATED_MODULE_QMLDIR) == {
        "AcmeTheme.qml",
        "AcmeLabel.qml",
        "AcmeButton.qml",
    }


def test_module_closure_passes_when_disk_matches_the_generated_qmldir():
    sources = [
        "qml/foundation/AcmeTheme.qml",
        "qml/foundation/AcmeLabel.qml",
        "qml/foundation/AcmeButton.qml",
    ]

    check_module_closure(
        "QGroundControl.Acme.Foundation", "qml/foundation", sources, GENERATED_MODULE_QMLDIR
    )


def test_a_module_file_the_module_does_not_declare_is_an_error():
    # This is the S1' blindness itself: a .qml file sitting in the module's own directory
    # that was never added to its QML_FILES. Before U0 such a file was excluded from the
    # flat qrc closure (it lives under the module dir) and checked against nothing else —
    # shipped nowhere, linted by nothing, and the gate went green regardless. This proves
    # that hole is provably closed rather than assumed fixed.
    sources = [
        "qml/foundation/AcmeTheme.qml",
        "qml/foundation/AcmeLabel.qml",
        "qml/foundation/AcmeButton.qml",
        "qml/foundation/Stray.qml",
    ]

    with pytest.raises(GateError, match=r"Stray\.qml"):
        check_module_closure(
            "QGroundControl.Acme.Foundation", "qml/foundation", sources, GENERATED_MODULE_QMLDIR
        )


def test_a_qmldir_entry_with_no_file_on_disk_is_an_error():
    sources = ["qml/foundation/AcmeTheme.qml", "qml/foundation/AcmeLabel.qml"]

    with pytest.raises(GateError, match=r"AcmeButton\.qml"):
        check_module_closure(
            "QGroundControl.Acme.Foundation", "qml/foundation", sources, GENERATED_MODULE_QMLDIR
        )


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


# --- Windows: reading exports with dumpbin ------------------------------------------------------------
#
# The fixtures are real `dumpbin /exports` output captured on the Windows test target (MSVC
# 14.44), not text written to suit the parser.

DUMPBIN_FIXTURES = Path(__file__).parent / "fixtures" / "dumpbin"


def _dumpbin(name: str) -> str:
    return (DUMPBIN_FIXTURES / f"{name}.dumpbin").read_text(encoding="utf-8")


def test_dumpbin_exports_of_a_real_plugin_are_read_by_name():
    assert parse_dumpbin_exports(_dumpbin("example_release_exports")) == [
        "qgcPluginBuildMarker",
        "qt_plugin_instance",
        "qt_plugin_query_metadata_v2",
    ]


def test_dumpbin_crlf_output_reads_the_same_as_lf():
    # dumpbin writes CRLF; the fixtures are stored with LF.
    crlf = _dumpbin("example_release_exports").replace("\n", "\r\n")
    assert parse_dumpbin_exports(crlf) == parse_dumpbin_exports(_dumpbin("example_release_exports"))


def test_dumpbin_mangled_rows_keep_the_decorated_name_not_the_annotation():
    names = parse_dumpbin_exports(_dumpbin("sdk_library_exports"))

    assert len(names) == 127
    assert "??0QGCAppService@@QEAA@PEAVQObject@@@Z" in names
    assert all("(" not in name and "=" not in name for name in names)


def test_a_dll_with_no_export_table_is_an_error_not_an_empty_list():
    with pytest.raises(GateError, match="printed no export table"):
        parse_dumpbin_exports(_dumpbin("no_export_table"))


@pytest.mark.parametrize("fixture", ["corrupt_file", "missing_file"])
def test_dumpbin_error_output_is_not_an_export_table(fixture: str):
    with pytest.raises(GateError, match="printed no export table"):
        parse_dumpbin_exports(_dumpbin(fixture))


def test_a_truncated_export_table_is_an_error():
    # The header says three names; two rows is a partial read, and a partial read could
    # have lost exactly the row the marker check wants.
    lines = _dumpbin("example_release_exports").splitlines()
    kept = [line for line in lines if "qt_plugin_query_metadata_v2" not in line]

    with pytest.raises(GateError, match="declares 3 exported name"):
        parse_dumpbin_exports("\n".join(kept))


def test_an_export_section_with_no_table_header_is_an_error():
    lines = _dumpbin("example_release_exports").splitlines()
    kept = [line for line in lines if "ordinal hint RVA" not in line]

    with pytest.raises(GateError, match="cannot read"):
        parse_dumpbin_exports("\n".join(kept))


def test_a_forwarded_export_and_an_ordinal_only_export_are_handled():
    text = (
        "File Type: DLL\n\n  Section contains the following exports for X.dll\n\n"
        "           2 number of functions\n           1 number of names\n\n"
        "    ordinal hint RVA      name\n\n"
        "          1    0          Forwarded = ntdll.RtlSomething\n"
        "          2      00001000 [NONAME]\n\n  Summary\n"
    )
    assert parse_dumpbin_exports(text) == ["Forwarded"]


def test_the_dumpbin_lister_yields_one_bare_name_per_line():
    calls: list[tuple[str, ...]] = []

    def capture(*args: str) -> tuple[int, str]:
        calls.append(args)
        return 0, _dumpbin("example_release_exports")

    returncode, listing = dumpbin_export_lister(capture, "dumpbin")(Path("X.dll"))

    assert calls == [("dumpbin", "/exports", "X.dll")]
    assert returncode == 0
    assert listing.splitlines() == [
        "qgcPluginBuildMarker",
        "qt_plugin_instance",
        "qt_plugin_query_metadata_v2",
    ]


def test_a_dumpbin_failure_surfaces_as_a_failed_read_not_a_missing_marker(tmp_path: Path):
    artifact = tmp_path / "X.dll"
    artifact.write_bytes(b"not a pe image")
    lister = dumpbin_export_lister(lambda *_: (1107, _dumpbin("corrupt_file")), "dumpbin")

    with pytest.raises(GateError, match=r"dumpbin could not read .*exit 1107"):
        check_marker_symbol_exported(artifact, lister, exact_names=True, tool="dumpbin")


def test_the_real_plugin_exports_pass_the_marker_check(tmp_path: Path):
    artifact = tmp_path / "ExamplePlugin.dll"
    artifact.write_bytes(b"")
    lister = dumpbin_export_lister(lambda *_: (0, _dumpbin("example_release_exports")), "dumpbin")

    check_marker_symbol_exported(artifact, lister, exact_names=True)


def test_a_plugin_without_the_marker_export_fails_the_dumpbin_check(tmp_path: Path):
    # The same export table with the marker row removed: only the other two names are left.
    lines = _dumpbin("example_release_exports").splitlines()
    kept = [line for line in lines if "qgcPluginBuildMarker" not in line]
    kept = [line.replace("3 number of", "2 number of") for line in kept]
    artifact = tmp_path / "ExamplePlugin.dll"
    artifact.write_bytes(b"")
    lister = dumpbin_export_lister(lambda *_: (0, "\n".join(kept)), "dumpbin")

    with pytest.raises(GateError, match="exports no qgcPluginBuildMarker"):
        check_marker_symbol_exported(artifact, lister, exact_names=True)


def test_a_mangled_export_containing_the_marker_name_is_not_the_marker(tmp_path: Path):
    # The host resolves the plain C name. A C++ function that merely contains it exports a
    # different symbol, so the substring match used for nm output must not apply here.
    artifact = tmp_path / "ExamplePlugin.dll"
    artifact.write_bytes(b"")
    listing = "?qgcPluginBuildMarker@@YAPEBDXZ\nqt_plugin_instance\n"

    with pytest.raises(GateError, match="exports no qgcPluginBuildMarker"):
        check_marker_symbol_exported(artifact, lambda _: (0, listing), exact_names=True)


def test_a_missing_dumpbin_is_an_error_naming_the_shell_it_needs():
    with pytest.raises(GateError, match=r"dumpbin is not on PATH.*vcvars64"):
        require_dumpbin(lambda _name: None)


def test_a_found_dumpbin_is_returned():
    assert require_dumpbin(lambda name: f"C:/vc/{name}.exe") == "C:/vc/dumpbin.exe"


def test_a_tool_that_is_not_installed_stops_the_gate_with_its_name():
    with pytest.raises(GateError, match="could not run definitely-not-a-real-tool"):
        Runner(verbose=False).capture("definitely-not-a-real-tool", "--version")


def _context(tmp_path: Path, manifest: dict[str, object]) -> GateContext:
    def refuse(*_args: object, **_kwargs: object) -> tuple[int, str]:
        raise AssertionError("no tool may run on the N/A path")

    runner = Runner(verbose=False)
    runner.capture = refuse  # type: ignore[method-assign]
    return GateContext(
        manifest=parse_manifest(manifest),
        work_dir=tmp_path,
        scratch_plugin=tmp_path,
        build_dir=tmp_path,
        sdk_prefix=tmp_path,
        qt_root=tmp_path,
        qmllint=tmp_path,
        runner=runner,
        parallel=1,
    )


def test_the_forbidden_symbol_axis_is_na_on_windows_with_the_link_time_reason(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
):
    monkeypatch.setattr(verifier, "is_windows", lambda: True)
    context = _context(
        tmp_path,
        _manifest(
            forbidden_symbols={"enabled": True, "patterns": ["mavlink"], "reason": "headers-only"}
        ),
    )

    run_forbidden_symbol_axis(context, tmp_path / "absent.dll")

    out = capsys.readouterr().out
    assert "N/A on Windows" in out
    assert "resolves every import at link time" in FORBIDDEN_SYMBOLS_NA_ON_WINDOWS
    assert FORBIDDEN_SYMBOLS_NA_ON_WINDOWS in out


def test_the_forbidden_symbol_axis_still_runs_off_windows(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    monkeypatch.setattr(verifier, "is_windows", lambda: False)
    context = _context(
        tmp_path,
        _manifest(
            forbidden_symbols={"enabled": True, "patterns": ["mavlink"], "reason": "headers-only"}
        ),
    )

    with pytest.raises(AssertionError, match="no tool may run"):
        run_forbidden_symbol_axis(context, _touch(tmp_path / "libX.so"))


def _touch(path: Path) -> Path:
    path.write_bytes(b"")
    return path


# --- Build: generator, configuration, artifact location -------------------------------------------


def test_ninja_is_chosen_when_present_and_no_generator_was_asked_for():
    assert generator_args({}, lambda _name: "/usr/bin/ninja") == ["-G", "Ninja"]


def test_an_explicit_generator_in_the_environment_is_left_alone():
    assert (
        generator_args({"CMAKE_GENERATOR": "Unix Makefiles"}, lambda _name: "/usr/bin/ninja") == []
    )


def test_no_generator_is_forced_when_ninja_is_absent():
    assert generator_args({}, lambda _name: None) == []


def test_both_the_build_and_the_configure_ask_for_release(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(verifier, "is_windows", lambda: True)
    configure = configure_command(Path("b"), Path("s"), Path("sdk"), Path("qt"), {"X": "1"})

    assert "-DCMAKE_BUILD_TYPE=Release" in configure
    assert build_command("b", 4) == [
        "cmake",
        "--build",
        "b",
        "--config",
        "Release",
        "--parallel",
        "4",
    ]
    assert build_command("b", 4, "--target", "t")[3:5] == ["--target", "t"]


def test_off_windows_the_configure_sets_no_build_type(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(verifier, "is_windows", lambda: False)
    configure = configure_command(Path("b"), Path("s"), Path("sdk"), Path("qt"), {"X": "1"})

    assert not any(str(arg).startswith("-DCMAKE_BUILD_TYPE") for arg in configure)


def test_the_artifact_is_found_in_a_single_config_build_dir(tmp_path: Path):
    expected = _touch(tmp_path / artifact_filename("SomePlugin"))

    assert find_built_artifact(tmp_path, "SomePlugin") == expected


def test_the_artifact_is_found_in_a_multi_config_release_folder(tmp_path: Path):
    (tmp_path / "Release").mkdir()
    expected = _touch(tmp_path / "Release" / artifact_filename("SomePlugin"))

    assert find_built_artifact(tmp_path, "SomePlugin") == expected


def test_a_debug_folder_artifact_is_not_taken_for_the_release_one(tmp_path: Path):
    (tmp_path / "Debug").mkdir()
    _touch(tmp_path / "Debug" / artifact_filename("SomePlugin"))

    with pytest.raises(GateError, match="produced no artifact"):
        find_built_artifact(tmp_path, "SomePlugin")


def test_an_artifact_in_two_places_is_an_error_not_a_choice(tmp_path: Path):
    (tmp_path / "Release").mkdir()
    _touch(tmp_path / artifact_filename("SomePlugin"))
    _touch(tmp_path / "Release" / artifact_filename("SomePlugin"))

    with pytest.raises(GateError, match="more than one place"):
        find_built_artifact(tmp_path, "SomePlugin")


def test_the_marker_rebuild_waits_past_one_second_of_file_time():
    # GNU make 3.81 compares file times at one-second resolution; a touch inside the same
    # second as the first build's object file is invisible to it.
    waited: list[float] = []

    wait_for_next_mtime_tick(waited.append)

    assert len(waited) == 1 and waited[0] > 1.0


# --- Qt auto-detect: the host's desktop kit only -------------------------------------------------------


def _kits(*pairs: tuple[str, str]) -> list[Path]:
    return [Path("/qt") / version / kit for version, kit in pairs]


def test_desktop_kits_per_host():
    assert is_desktop_kit("macos", "darwin", "arm64")
    assert is_desktop_kit("gcc_64", "linux", "x86_64")
    assert is_desktop_kit("gcc_arm64", "linux", "aarch64")
    assert is_desktop_kit("msvc2022_64", "win32", "AMD64")
    assert is_desktop_kit("msvc2022_arm64", "win32", "ARM64")
    assert not is_desktop_kit("gcc_arm64", "linux", "x86_64")
    assert not is_desktop_kit("mingw_64", "win32", "AMD64")
    assert not is_desktop_kit("android_arm64_v8a", "linux", "x86_64")
    assert not is_desktop_kit("wasm_singlethread", "darwin", "arm64")
    assert not is_desktop_kit("ios", "darwin", "arm64")


def test_an_android_kit_at_a_newer_version_is_not_taken_for_the_desktop_kit():
    # The defect this replaces: the newest-first pick landed on android_arm64_v8a, which has
    # no qmllint, and failed with "qmllint not executable".
    candidates = _kits(("6.12.0", "android_arm64_v8a"), ("6.12.0", "gcc_64"), ("6.11.1", "gcc_64"))

    assert choose_qt_prefix(candidates, "linux", "x86_64") == Path("/qt/6.12.0/gcc_64")


def test_the_newest_desktop_kit_wins_by_version_not_by_string():
    candidates = _kits(("6.9.0", "macos"), ("6.11.1", "macos"), ("6.10.2", "macos"))

    assert choose_qt_prefix(candidates, "darwin", "arm64") == Path("/qt/6.11.1/macos")


def test_a_host_with_only_foreign_kits_is_an_error_listing_them():
    candidates = _kits(("6.12.0", "android_arm64_v8a"), ("6.12.0", "wasm_multithread"))

    with pytest.raises(GateError, match=r"no desktop Qt kit.*android_arm64_v8a"):
        choose_qt_prefix(candidates, "linux", "x86_64")


def test_no_kits_at_all_is_an_error():
    with pytest.raises(GateError, match="no desktop Qt kit"):
        choose_qt_prefix([], "linux", "x86_64")


def test_two_desktop_kits_at_the_newest_version_are_ambiguous():
    candidates = _kits(("6.12.0", "msvc2019_64"), ("6.12.0", "msvc2022_64"))

    with pytest.raises(GateError, match=r"more than one desktop Qt kit.*--qt-root"):
        choose_qt_prefix(candidates, "win32", "AMD64")


def test_an_older_second_kit_does_not_make_the_newest_ambiguous():
    candidates = _kits(("6.12.0", "msvc2022_64"), ("6.10.0", "msvc2019_64"))

    assert choose_qt_prefix(candidates, "win32", "AMD64") == Path("/qt/6.12.0/msvc2022_64")


# --- Windows has no symlink privilege by default -------------------------------------------------------


def test_a_failed_symlink_off_windows_is_an_error(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    origin = _touch(tmp_path / "origin")
    monkeypatch.setattr(verifier, "is_windows", lambda: False)

    def refuse(*_args: object) -> None:
        raise OSError("not permitted")

    monkeypatch.setattr(os, "symlink", refuse)

    with pytest.raises(GateError, match="cannot link"):
        link_path(origin, tmp_path / "link")


def test_a_file_that_cannot_be_symlinked_on_windows_is_copied(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    origin = tmp_path / "origin.qmltypes"
    origin.write_text("contents")
    monkeypatch.setattr(verifier, "is_windows", lambda: True)

    def refuse(*_args: object) -> None:
        raise OSError("A required privilege is not held by the client")

    monkeypatch.setattr(os, "symlink", refuse)

    link_path(origin, tmp_path / "link.qmltypes")

    assert (tmp_path / "link.qmltypes").read_text() == "contents"


def test_a_directory_that_cannot_be_symlinked_on_windows_becomes_a_junction(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    origin = tmp_path / "QtQuick"
    origin.mkdir()
    monkeypatch.setattr(verifier, "is_windows", lambda: True)
    commands: list[tuple[str, ...]] = []

    def refuse(*_args: object) -> None:
        raise OSError("A required privilege is not held by the client")

    def capture(self: Runner, *args: str | Path, cwd: Path | None = None) -> tuple[int, str]:
        commands.append(tuple(str(arg) for arg in args))
        return 0, ""

    monkeypatch.setattr(os, "symlink", refuse)
    monkeypatch.setattr(Runner, "capture", capture)

    link_path(origin, tmp_path / "link")

    assert commands == [("cmd", "/c", "mklink", "/J", str(tmp_path / "link"), str(origin))]


def test_a_junction_that_cannot_be_made_is_an_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    origin = tmp_path / "QtQuick"
    origin.mkdir()
    monkeypatch.setattr(verifier, "is_windows", lambda: True)

    def refuse(*_args: object) -> None:
        raise OSError("A required privilege is not held by the client")

    monkeypatch.setattr(os, "symlink", refuse)
    monkeypatch.setattr(Runner, "capture", lambda self, *a, **k: (1, "access denied"))

    with pytest.raises(GateError, match="cannot make a junction"):
        link_path(origin, tmp_path / "link")


def test_a_windows_qmllint_path_is_mapped_back_to_the_source_in_either_spelling():
    # qmllint prints C:/... on Windows while the gate holds the path as C:\\... .
    flat = PureWindowsPath("C:/Temp/flat-qml/A.qml")
    forward = "Error: C:/Temp/flat-qml/A.qml:3:1: Failed [import]"
    backward = forward.replace("/", "\\")

    assert replace_path(forward, flat, "qml/A.qml") == "Error: qml/A.qml:3:1: Failed [import]"
    assert replace_path(backward, flat, "qml/A.qml") == "Error: qml/A.qml:3:1: Failed [import]"


def test_a_missing_origin_is_an_error_not_a_traceback(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
    monkeypatch.setattr(verifier, "is_windows", lambda: True)

    def refuse(*_args: object) -> None:
        raise OSError("A required privilege is not held by the client")

    monkeypatch.setattr(os, "symlink", refuse)

    with pytest.raises(GateError, match="cannot copy"):
        link_path(tmp_path / "absent.qmltypes", tmp_path / "link.qmltypes")
