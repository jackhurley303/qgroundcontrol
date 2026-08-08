#!/usr/bin/env python3
"""Verify that a QGroundControl plugin builds, tests and lints against the published SDK alone.

The question this answers is the only one that matters for a released plugin: does it
still work when there is no QGroundControl source tree anywhere near it? The two arms are
indistinguishable at runtime — a plugin loads into the host's QQmlEngine either way — so a
stray host-tree include or an `import QGroundControl.Controls` compiles and renders
perfectly in the in-tree dev loop and fails only once someone else builds it.

The tool is plugin-agnostic: everything specific to a plugin lives in its own
`plugin-verify.json` manifest. This file validates that manifest itself, in full, with no
third-party dependency; `plugin-verify.schema.json` beside it states the same contract in a
form an editor or a CI hook can read, and is never the only thing enforcing it.

Every axis is claimed or explicitly declared N/A. A manifest with an unknown key, a
missing key, or an axis that resolves to zero work is an error — never a silent skip,
because a gate that degrades to a pass is worse than no gate.

Isolation is from QGC source *and* from the developer's Qt install. `qmllint --bare`
plus the tool-published allowlists below are what make the second half true: a dependency
on a Qt module the SDK does not guarantee fails here, not three weeks later in someone
else's leaner Qt. The allowlists are data this tool ships and versions — the local Qt
install's contents are deliberately not the source of truth for "guaranteed".

Stdlib only, single file plus its schema: it installs into the SDK package and must run
from an unpacked zip that contains no QGC checkout and no `tools/` package.

Usage:
    python3 verify_plugin_out_of_tree.py <plugin-dir> [--sdk <prefix>] [--build-dir <dir>]
        [--qt-root <dir>] [--work <dir>] [--parallel <n>]
        [--deploy] [--keep] [--report-json <file>]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable, Sequence

MANIFEST_FILENAME = "plugin-verify.json"

# Directory names never descended into, and only at the plugin root: the gate builds inside
# the very tree it inspects, and a plugin's own git dir is not part of what it ships.
# Anchored, never matched by name at any depth — a bare `build` exclusion would quietly
# drop a real source directory that happened to be named build/, and the gate would then be
# inspecting a plugin that is not the one that ships.
_EXCLUDED_DIR_NAMES = frozenset({".git", "build"})


def _is_excluded_dir(name: str) -> bool:
    return name in _EXCLUDED_DIR_NAMES or name.startswith("build-")


def _prune_top_level(base: Path, dirpath: str, dirnames: list[str]) -> None:
    """os.walk filter: drop the excluded directories, but only where they sit at the root."""
    if Path(dirpath).resolve() == base:
        dirnames[:] = [name for name in dirnames if not _is_excluded_dir(name)]


# --- The guaranteed sets (Pillar 4) ----------------------------------------------------
#
# "Guaranteed" means: present in every environment that builds and runs QGroundControl,
# and therefore safe for a plugin to depend on. It is a promise this tool publishes, not
# an observation of whatever Qt happens to be installed on the machine running it — which
# is exactly why a Qt5Compat.GraphicalEffects dependency once passed a local green run and
# failed only in CI, where that module isn't installed.

# Mirrors the COMPONENTS and OPTIONAL_COMPONENTS blocks of the host's root CMakeLists.txt.
# The tool cannot read that file (it runs from a zip with no checkout), so this is a
# deliberate second copy — kept honest by a unit test that diffs the two whenever the repo
# is present. The optional ones are included because "optional" there means the *host* can
# build without them, not that a Qt install might lack them: every one comes from qtbase,
# qtdeclarative, or a module .github/build-config.json installs. Qt5Compat, Charts,
# WebEngine and the rest appear in neither block, which is what makes them catchable.
GUARANTEED_QT_COMPONENTS = frozenset(
    {
        "Bluetooth",
        "Concurrent",
        "Core",
        "Graphs",
        "Gui",
        "HttpServer",
        "LinguistTools",
        "Location",
        "LocationPrivate",
        "Multimedia",
        "MultimediaQuickPrivate",
        "Network",
        "OpenGL",
        "Positioning",
        "Qml",
        "QmlIntegration",
        "Quick",
        "Quick3D",
        "QuickControls2",
        "QuickTest",
        "QuickVectorImage",
        "QuickWidgets",
        "Sensors",
        "SerialPort",
        "Sql",
        "StateMachine",
        "Svg",
        "Test",
        "TextToSpeech",
        "Xml",
    }
)

# Top-level entries under <qt>/qml staged onto the lint import path: the QML modules that
# ship with a base Qt install, plus those the host's CI Qt adds (.github/build-config.json,
# `qt.modules`). Everything else in the developer's Qt — Qt5Compat, QtCharts, QtWebView,
# … — is deliberately absent, so importing it fails here rather than in CI.
GUARANTEED_QML_MODULES = (
    "QML",
    "Qt",
    "QtCore",
    "QtGraphs",
    "QtLocation",
    "QtMultimedia",
    "QtPositioning",
    "QtQml",
    "QtQuick",
    "QtQuick3D",
    "QtScxml",
    "QtSensors",
    "QtTextToSpeech",
    "QtWebSockets",
)

# Not modules, but qmllint resolves nothing at all without them under --bare.
GUARANTEED_QML_SUPPORT_FILES = ("builtins.qmltypes", "jsroot.qmltypes")

# Negative control 2's fixture reaches this type: a real host control that the published
# QML module deliberately does not export. Embedded here rather than in a manifest — the
# published subset is one SDK-wide fact, identical for every plugin.
UNPUBLISHED_TYPE_FIXTURE = "ToolStrip"

# Negative control 3 injects this import: a base QGroundControl QML module that a plugin
# may never reach into, and that the packaged SDK does not carry.
BASE_MODULE_REACH_IMPORT = "QGroundControl.Controls"

# The one resource prefix a plugin's QML may ship under. Sibling components resolve by
# same-directory lookup at runtime, so every .qml has to land in the same flat prefix.
QML_RESOURCE_PREFIX = "/qml"


class GateError(Exception):
    """A gate failure: the plugin, or the gate's own instrumentation, is not sound."""


class ManifestError(GateError):
    """The manifest does not describe every axis exactly once."""


# --- Manifest ---------------------------------------------------------------------------


@dataclass(frozen=True)
class TriageEntry:
    """One accepted qmllint diagnostic, with the reason it is accepted."""

    pattern: str
    reason: str


@dataclass(frozen=True)
class QmlAxis:
    enabled: bool
    reason: str | None = None
    source_roots: tuple[str, ...] = ()
    """Directories, relative to the plugin root, whose every .qml file must be shipped."""
    own_module: str | None = None
    """The plugin's own QML module URI, or null when it registers none."""
    accepted_triage: tuple[TriageEntry, ...] = ()


@dataclass(frozen=True)
class TestAxis:
    enabled: bool
    reason: str | None = None
    cmake_lists: str | None = None
    count_pattern: str | None = None


@dataclass(frozen=True)
class ForbiddenSymbolsAxis:
    enabled: bool
    reason: str | None = None
    patterns: tuple[str, ...] = ()


@dataclass(frozen=True)
class MissingDependencyControl:
    """Negative control 1: strip a declared SDK dependency, expect the C++ axis to notice."""

    enabled: bool
    reason: str | None = None
    sdk_subpath: str | None = None
    build_target: str | None = None
    expected_pattern: str | None = None


@dataclass(frozen=True)
class Manifest:
    artifact_name: str
    qt_components: tuple[str, ...]
    qml: QmlAxis
    tests: TestAxis
    forbidden_symbols: ForbiddenSymbolsAxis
    missing_dependency_control: MissingDependencyControl


def _require_keys(obj: object, required: Iterable[str], where: str) -> dict[str, object]:
    """Both directions at once: a missing key is an unclaimed axis, an unknown key is a
    claim this tool never reads. Either one means the manifest and the gate disagree about
    what is being checked, which is the failure this whole file exists to prevent."""
    if not isinstance(obj, dict):
        raise ManifestError(f"{where}: expected an object, got {type(obj).__name__}")
    expected = set(required)
    actual = set(obj)
    missing = sorted(expected - actual)
    unknown = sorted(actual - expected)
    if missing:
        raise ManifestError(f"{where}: missing required key(s): {', '.join(missing)}")
    if unknown:
        raise ManifestError(f"{where}: unknown key(s): {', '.join(unknown)}")
    return obj


def _str(obj: dict[str, object], key: str, where: str) -> str:
    value = obj[key]
    if not isinstance(value, str) or not value.strip():
        raise ManifestError(f"{where}.{key}: expected a non-empty string")
    return value


def _str_list(obj: dict[str, object], key: str, where: str) -> tuple[str, ...]:
    value = obj[key]
    if not isinstance(value, list) or not value:
        raise ManifestError(f"{where}.{key}: expected a non-empty array of strings")
    for item in value:
        if not isinstance(item, str) or not item.strip():
            raise ManifestError(f"{where}.{key}: every entry must be a non-empty string")
    return tuple(value)


def _bool(obj: dict[str, object], key: str, where: str) -> bool:
    value = obj[key]
    if not isinstance(value, bool):
        raise ManifestError(f"{where}.{key}: expected a boolean")
    return value


def _enabled(obj: object, where: str) -> bool:
    if not isinstance(obj, dict) or "enabled" not in obj:
        raise ManifestError(f"{where}: expected an object with an 'enabled' key")
    return _bool(obj, "enabled", where)


def _parse_qml_axis(raw: object) -> QmlAxis:
    if not _enabled(raw, "qml"):
        off = _require_keys(raw, ("enabled", "reason"), "qml")
        return QmlAxis(enabled=False, reason=_str(off, "reason", "qml"))
    obj = _require_keys(raw, ("enabled", "source_roots", "own_module", "accepted_triage"), "qml")
    own_module = obj["own_module"]
    if own_module is not None and (not isinstance(own_module, str) or not own_module.strip()):
        raise ManifestError("qml.own_module: expected a module URI string, or null")
    triage_raw = obj["accepted_triage"]
    if not isinstance(triage_raw, list):
        raise ManifestError("qml.accepted_triage: expected an array")
    triage: list[TriageEntry] = []
    for index, item in enumerate(triage_raw):
        where = f"qml.accepted_triage[{index}]"
        entry = _require_keys(item, ("pattern", "reason"), where)
        pattern = _str(entry, "pattern", where)
        try:
            re.compile(pattern)
        except re.error as exc:
            raise ManifestError(f"{where}.pattern: not a valid regular expression ({exc})") from exc
        triage.append(TriageEntry(pattern=pattern, reason=_str(entry, "reason", where)))
    return QmlAxis(
        enabled=True,
        source_roots=_str_list(obj, "source_roots", "qml"),
        own_module=own_module,
        accepted_triage=tuple(triage),
    )


def _parse_test_axis(raw: object) -> TestAxis:
    if not _enabled(raw, "tests"):
        obj = _require_keys(raw, ("enabled", "reason"), "tests")
        return TestAxis(enabled=False, reason=_str(obj, "reason", "tests"))
    obj = _require_keys(raw, ("enabled", "cmake_lists", "count_pattern"), "tests")
    pattern = _str(obj, "count_pattern", "tests")
    try:
        re.compile(pattern)
    except re.error as exc:
        raise ManifestError(f"tests.count_pattern: not a valid regular expression ({exc})") from exc
    return TestAxis(
        enabled=True,
        cmake_lists=_str(obj, "cmake_lists", "tests"),
        count_pattern=pattern,
    )


def _parse_forbidden_symbols(raw: object) -> ForbiddenSymbolsAxis:
    where = "forbidden_symbols"
    if not _enabled(raw, where):
        obj = _require_keys(raw, ("enabled", "reason"), where)
        return ForbiddenSymbolsAxis(enabled=False, reason=_str(obj, "reason", where))
    obj = _require_keys(raw, ("enabled", "patterns", "reason"), where)
    patterns = _str_list(obj, "patterns", where)
    for pattern in patterns:
        try:
            re.compile(pattern)
        except re.error as exc:
            raise ManifestError(f"{where}.patterns: {pattern!r} is not a regex ({exc})") from exc
    return ForbiddenSymbolsAxis(enabled=True, reason=_str(obj, "reason", where), patterns=patterns)


def _parse_missing_dependency_control(raw: object) -> MissingDependencyControl:
    where = "missing_dependency_control"
    if not _enabled(raw, where):
        obj = _require_keys(raw, ("enabled", "reason"), where)
        return MissingDependencyControl(enabled=False, reason=_str(obj, "reason", where))
    obj = _require_keys(raw, ("enabled", "sdk_subpath", "build_target", "expected_pattern"), where)
    pattern = _str(obj, "expected_pattern", where)
    try:
        re.compile(pattern)
    except re.error as exc:
        raise ManifestError(f"{where}.expected_pattern: not a regex ({exc})") from exc
    return MissingDependencyControl(
        enabled=True,
        sdk_subpath=_str(obj, "sdk_subpath", where),
        build_target=_str(obj, "build_target", where),
        expected_pattern=pattern,
    )


def parse_manifest(raw: object) -> Manifest:
    """Validate a decoded plugin-verify.json into a Manifest, or raise ManifestError."""
    if not isinstance(raw, dict):
        raise ManifestError("manifest: expected a JSON object")
    # `$schema` is editor/pre-commit metadata, mirroring .github/build-config.json.
    body = {key: value for key, value in raw.items() if key != "$schema"}
    obj = _require_keys(
        body,
        (
            "artifact_name",
            "qt_components",
            "qml",
            "tests",
            "forbidden_symbols",
            "missing_dependency_control",
        ),
        "manifest",
    )
    return Manifest(
        artifact_name=_str(obj, "artifact_name", "manifest"),
        qt_components=_str_list(obj, "qt_components", "manifest"),
        qml=_parse_qml_axis(obj["qml"]),
        tests=_parse_test_axis(obj["tests"]),
        forbidden_symbols=_parse_forbidden_symbols(obj["forbidden_symbols"]),
        missing_dependency_control=_parse_missing_dependency_control(
            obj["missing_dependency_control"]
        ),
    )


def load_manifest(plugin_root: Path) -> Manifest:
    path = plugin_root / MANIFEST_FILENAME
    if not path.is_file():
        raise ManifestError(
            f"no {MANIFEST_FILENAME} in {plugin_root} — every plugin must declare every "
            f"axis of the gate, including the ones it opts out of"
        )
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ManifestError(f"{path}: not valid JSON ({exc})") from exc
    return parse_manifest(raw)


# --- The C++ axis: declared vs guaranteed ------------------------------------------------

# Words that may follow COMPONENTS without being one.
_FIND_PACKAGE_KEYWORDS = frozenset(
    {
        "REQUIRED",
        "QUIET",
        "CONFIG",
        "EXACT",
        "MODULE",
        "GLOBAL",
        "NO_MODULE",
        "NO_DEFAULT_PATH",
        "OPTIONAL_COMPONENTS",
    }
)

_FIND_PACKAGE_QT6 = re.compile(
    r"find_package\s*\(\s*Qt6\b(?P<args>[^)]*)\)", re.IGNORECASE | re.DOTALL
)


def strip_cmake_comments(cmake_text: str) -> str:
    """Drop `#`-to-end-of-line before anything else looks at the text.

    Stripping later is not equivalent: the call-matching regex stops at the first `)`, so a
    closing paren inside a comment truncates the component list — and the resulting mismatch
    message names the CMake call as authoritative, inviting an author to "fix" the manifest
    and silently drop a genuinely unguaranteed component. A commented-out `find_package` is
    the same bug pointing the other way.
    """
    return "\n".join(line.split("#", 1)[0] for line in cmake_text.splitlines())


def parse_qt_components(cmake_text: str) -> tuple[str, ...]:
    """The Qt6 components a plugin's CMake actually asks for.

    Raises when there is no such call at all: a plugin that links Qt without declaring it
    would otherwise make this axis resolve to zero work and pass by default.
    """
    components: list[str] = []
    for match in _FIND_PACKAGE_QT6.finditer(strip_cmake_comments(cmake_text)):
        args = match.group("args")
        upper = args.upper()
        index = upper.find("COMPONENTS")
        if index < 0:
            continue
        for word in args[index + len("COMPONENTS") :].split():
            if word.upper() in _FIND_PACKAGE_KEYWORDS:
                break
            if word.startswith("$"):
                continue
            components.append(word)
    if not components:
        raise GateError(
            "no find_package(Qt6 ... COMPONENTS ...) call found in the plugin's CMake — "
            "nothing to diff against the SDK's guaranteed component set"
        )
    seen: dict[str, None] = {}
    for component in components:
        seen.setdefault(component, None)
    return tuple(seen)


def find_cmake_lists(plugin_root: Path) -> list[Path]:
    """Every CMakeLists.txt in the plugin, not just the top-level one.

    A `find_package(Qt6 ... COMPONENTS ...)` in a subdirectory is exactly as capable of
    depending on an unguaranteed Qt module as one in the root, and it resolves against the
    developer's own full Qt just as happily — so a root-only scan leaves the axis blind
    one directory down.
    """
    found: list[Path] = []
    base = plugin_root.resolve()
    for dirpath, dirnames, filenames in os.walk(base):
        _prune_top_level(base, dirpath, dirnames)
        if "CMakeLists.txt" in filenames:
            found.append(Path(dirpath) / "CMakeLists.txt")
    if not found:
        raise GateError(f"no CMakeLists.txt anywhere under {plugin_root}")
    return sorted(found)


def check_qt_components(declared: Sequence[str], cmake_text: str) -> None:
    """Two checks on one axis, both of which must hold.

    The manifest's list must match what the plugin's CMake asks for (otherwise the manifest
    is decoration that drifts), and every component must be one the SDK guarantees
    (otherwise the plugin depends on a Qt module a leaner install won't have — the failure
    that motivated this whole axis).
    """
    duplicates = sorted({name for name in declared if list(declared).count(name) > 1})
    if duplicates:
        raise ManifestError(
            f"qt_components lists {', '.join(duplicates)} more than once — the set comparison "
            f"below would hide it"
        )
    actual = parse_qt_components(cmake_text)
    if set(declared) != set(actual):
        raise GateError(
            f"qt_components in the manifest {sorted(declared)} does not match the "
            f"find_package(Qt6 ... COMPONENTS ...) call {sorted(actual)} — one of the two "
            f"is wrong, and the manifest is what the guaranteed-set check reads"
        )
    unguaranteed = sorted(set(declared) - GUARANTEED_QT_COMPONENTS)
    if unguaranteed:
        raise GateError(
            f"the plugin requires Qt6 component(s) the SDK does not guarantee: "
            f"{', '.join(unguaranteed)}. They may resolve against this machine's Qt and "
            f"still be absent where the host is built."
        )


# --- The test axis -----------------------------------------------------------------------


def expected_test_count(cmake_text: str, pattern: str) -> int:
    """How many tests the plugin's own CMake registers.

    Derived rather than hardcoded so adding a test cannot leave the check behind. Zero is
    an error, not an answer: a count of zero would make the comparison below vacuous, which
    is how a suite that never configured reads as a clean pass.
    """
    count = len(re.findall(pattern, cmake_text, re.MULTILINE))
    if count == 0:
        raise GateError(
            f"the test-count pattern /{pattern}/ matched nothing — the expected-count "
            f"check has nothing to compare against, so it would pass on an empty suite"
        )
    return count


def parse_ctest_total(output: str) -> int:
    """The count `ctest -N` reports.

    `ctest` prints "No tests were found!!!" and exits 0, so a missing enable_testing() — or
    a test/ subdirectory that silently didn't configure — reads as a clean pass to anything
    that only looks at the exit code. A missing "Total Tests:" line is likewise an error
    here, never a zero: silence must not be able to look like a measurement.
    """
    match = re.search(r"^Total Tests:\s*(\d+)\s*$", output, re.MULTILINE)
    if match is None:
        raise GateError(
            "ctest -N printed no 'Total Tests:' line — the suite count could not be read, "
            "which is not the same as a count of zero"
        )
    return int(match.group(1))


# --- The forbidden-symbol axis ------------------------------------------------------------


def find_forbidden_symbols(nm_output: str, patterns: Sequence[str]) -> list[str]:
    """Undefined symbols the plugin must not have. Matched case-insensitively."""
    hits: list[str] = []
    compiled = [re.compile(pattern, re.IGNORECASE) for pattern in patterns]
    for line in nm_output.splitlines():
        if any(expression.search(line) for expression in compiled):
            hits.append(line.strip())
    return hits


def check_forbidden_symbols(
    artifact: Path, patterns: Sequence[str], nm_runner: Callable[[Path], tuple[int, str]]
) -> None:
    """Verify the instrument, three ways, before believing a clean result.

    `nm -u <missing file> | grep …` reports "no undefined symbols" just as cheerfully when
    nm found no file to read — so the artifact must exist, nm must have succeeded, and it
    must have printed something. All three matter: nm exits 1 with a diagnostic on a file it
    cannot parse, and that diagnostic matches no forbidden pattern, so a binary nm could not
    read is otherwise indistinguishable from a clean one. A linked shared library always has
    undefined symbols, so an empty listing is broken instrumentation, not cleanliness.
    """
    if not artifact.is_file():
        raise GateError(
            f"no standalone artifact at {artifact} — there is nothing to check for "
            f"forbidden symbols, and an absent file must not read as a clean one"
        )
    returncode, output = nm_runner(artifact)
    if returncode != 0:
        raise GateError(
            f"nm could not read {artifact} (exit {returncode}) — its symbols were never "
            f"inspected, which is not the same as finding none.\n{output}"
        )
    if not output.strip():
        raise GateError(
            f"nm listed no undefined symbols at all in {artifact} — a linked shared library "
            f"always has some, so this is a broken measurement rather than a clean result"
        )
    hits = find_forbidden_symbols(output, patterns)
    if hits:
        raise GateError(
            "forbidden undefined symbol(s) in the standalone artifact:\n  " + "\n  ".join(hits)
        )


# --- The QML axis --------------------------------------------------------------------------


@dataclass(frozen=True)
class QrcEntry:
    prefix: str
    alias: str | None
    path: str


def parse_qrc(text: str) -> list[QrcEntry]:
    """Every <file> entry in a .qrc, aliased or not.

    Unaliased entries are kept rather than skipped: such an entry still ships the file, just
    to a different resource path than every sibling resolves through — broken at runtime and
    invisible to a scan that only looks for `alias=`.
    """
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        raise GateError(f"the plugin's .qrc is not valid XML ({exc})") from exc
    entries: list[QrcEntry] = []
    for resource in root.iter("qresource"):
        prefix = resource.get("prefix", "")
        for file_element in resource.iter("file"):
            path = (file_element.text or "").strip()
            if not path:
                raise GateError("the plugin's .qrc has a <file> entry with no path")
            entries.append(QrcEntry(prefix=prefix, alias=file_element.get("alias"), path=path))
    return entries


def flatten_qml_entries(entries: Sequence[QrcEntry]) -> dict[str, str]:
    """alias -> source path, for the .qml entries of the flat /qml prefix.

    The resource tree aliases every .qml flat into one prefix, so a file in qml/views/
    resolves a sibling in qml/components/ by plain same-directory lookup at runtime.
    Reconstructing that layout is what makes qmllint see the shape that actually ships
    instead of the source directory tree.
    """
    flat: dict[str, str] = {}
    unaliased: list[str] = []
    misprefixed: list[str] = []
    for entry in entries:
        if not entry.path.endswith(".qml"):
            continue
        if "/" + entry.prefix.strip("/") != QML_RESOURCE_PREFIX:
            # Not skipped: a .qml under some other prefix still ships, just where no sibling
            # resolves it — the same breakage as an unaliased entry, and just as invisible
            # to a scan that only looks at the prefix it expected to find.
            misprefixed.append(f"{entry.path} (under prefix {entry.prefix!r})")
            continue
        if entry.alias is None:
            unaliased.append(entry.path)
            continue
        existing = flat.get(entry.alias)
        if existing is not None:
            raise GateError(
                f"the .qrc aliases two different files to {entry.alias!r} "
                f"({existing} and {entry.path}) — the flat prefix cannot hold both, so one "
                f"of them would never ship"
            )
        flat[entry.alias] = entry.path
    if misprefixed:
        raise GateError(
            f"QML entries in the .qrc outside the {QML_RESOURCE_PREFIX!r} prefix:\n  "
            + "\n  ".join(sorted(misprefixed))
        )
    if unaliased:
        raise GateError(
            "QML entries in the .qrc with no alias=:\n  "
            + "\n  ".join(sorted(unaliased))
            + "\nEvery .qml must be aliased flat into the QML prefix — sibling components "
            "resolve by same-directory lookup at runtime, and an unaliased file lands "
            "somewhere else."
        )
    return flat


def find_qml_sources(plugin_root: Path, source_roots: Sequence[str]) -> list[str]:
    """Every .qml under the declared source roots, as plugin-root-relative POSIX paths.

    The plugin's own build tree is skipped — this tool creates one inside the scratch copy
    it is inspecting, and a generated .qml in there is not something the plugin ships.
    """
    found: list[str] = []
    # Resolved on both sides: the scratch tree lives under a temp dir that is itself a
    # symlink on macOS (/var -> /private/var), so an unresolved base makes every path look
    # like it is outside the plugin.
    base = plugin_root.resolve()
    for root_name in source_roots:
        root = (base / root_name).resolve()
        if not root.is_dir():
            raise GateError(f"qml.source_roots names {root_name!r}, which is not a directory")
        for dirpath, dirnames, filenames in os.walk(root):
            _prune_top_level(base, dirpath, dirnames)
            for filename in filenames:
                if filename.endswith(".qml"):
                    relative = Path(dirpath, filename).relative_to(base)
                    found.append(relative.as_posix())
    return sorted(set(found))


def check_qrc_closure(shipped: Iterable[str], sources: Sequence[str], plugin_root: Path) -> None:
    """Close the .qrc over the source tree both ways.

    A .qml that isn't in the .qrc doesn't ship, and fails at runtime on the one page that
    loads it; a .qrc entry with no file is a build that cannot find a file whose absence is
    otherwise inert.
    """
    shipped_set = set(shipped)
    missing = sorted(path for path in shipped_set if not (plugin_root / path).is_file())
    if missing:
        raise GateError(
            "the .qrc references QML files that do not exist:\n  " + "\n  ".join(missing)
        )
    unshipped = sorted(set(sources) - shipped_set)
    if unshipped:
        raise GateError(
            "QML files under the declared source roots that the .qrc does not ship:\n  "
            + "\n  ".join(unshipped)
            + "\nAdd them to the .qrc or delete them."
        )
    if not shipped_set:
        # Positive control on the gate's own input. Everything above reports what is WRONG;
        # nothing so far would notice the set being EMPTY — and qmllint with no files prints
        # usage, emits no diagnostics, and exits 0. Same shape as the ctest trap above.
        raise GateError(
            "the .qrc ships no QML at all — qmllint over an empty file list reports nothing "
            "and exits 0, so this would read as a clean lint"
        )


def inject_import(qml_text: str, import_statement: str) -> str:
    """Insert an import into the import block, for negative control 3.

    Appending it after the root object would not be parsed as an import at all, and qmllint
    would say nothing about it — a control that always "passes" while testing nothing. This
    repo has been bitten by exactly that twice, so the absence of an import block is an
    error rather than a fallback to appending.
    """
    lines = qml_text.splitlines()
    for index, line in enumerate(lines):
        if line.startswith("import "):
            lines.insert(index + 1, f"import {import_statement}")
            return "\n".join(lines) + "\n"
    raise GateError(
        "the file chosen for the base-module reach control has no import statement, so an "
        "injected import could not land inside the import block — the control would test "
        "nothing"
    )


def qmllint_error_lines(output: str) -> list[str]:
    """The diagnostics triage reasons about. Deliberately narrow — see below for what it
    cannot see."""
    return [line for line in output.splitlines() if line.startswith("Error: ")]


def qmllint_syntax_lines(output: str) -> list[str]:
    """Diagnostics about a file qmllint could not parse, at whatever severity.

    qmllint reports these as `Warning: ... [syntax]` and exits non-zero; there is no
    severity flag that promotes them, so they never appear as `Error:` lines and triage
    never sees them. They are checked separately, and are never triageable: a file that
    cannot be parsed is not a file whose diagnostics anyone can reason about.

    An exit code alone cannot stand in for this. Triaged errors legitimately leave the exit
    code non-zero, so for any plugin with an accepted-triage entry — and such an entry must
    match something every run or the gate fails — a non-zero exit is the normal state, and
    a guard keyed on it would be permanently disarmed for exactly the plugins that have the
    most QML.
    """
    return [line for line in output.splitlines() if "[syntax]" in line]


def triage_qmllint_output(
    output: str, accepted: Sequence[TriageEntry]
) -> tuple[list[str], list[TriageEntry]]:
    """Split qmllint's errors into (unaccepted, triage entries that matched nothing).

    An entry that matches nothing is reported so the caller can fail on it: an accepted-
    triage list that only ever grows is how a stale exception ends up silently swallowing
    real diagnostics.
    """
    compiled = [(entry, re.compile(entry.pattern)) for entry in accepted]
    matched: set[str] = set()
    unaccepted: list[str] = []
    for line in qmllint_error_lines(output):
        for entry, expression in compiled:
            if expression.search(line):
                matched.add(entry.pattern)
                break
        else:
            unaccepted.append(line)
    unused = [entry for entry in accepted if entry.pattern not in matched]
    return unaccepted, unused


def assert_control_failed(label: str, returncode: int, output: str, pattern: str) -> None:
    """A negative control must fail, and fail for the reason it is named for.

    A non-zero exit on its own is not evidence — a typo in a fixture path, a missing Qt
    module, any unrelated breakage produces one too, and the control would then "pass"
    while proving nothing about the axis it claims to test.
    """
    if returncode == 0:
        raise GateError(f"{label} passed (exit 0) — the gate is decorative.\n{output}")
    if re.search(pattern, output, re.MULTILINE) is None:
        raise GateError(
            f"{label} failed, but not for the expected reason (no match for /{pattern}/) — "
            f"the control is not testing what it claims.\n{output}"
        )


# --- Platform ------------------------------------------------------------------------------


def artifact_filename(name: str) -> str:
    if sys.platform == "darwin":
        return f"lib{name}.dylib"
    if os.name == "nt":
        return f"{name}.dll"
    return f"lib{name}.so"


def host_plugin_dir(app_name: str, org_name: str, stable_build: bool) -> Path:
    """Where the host loads plugins from — mirrors cmake/modules/PluginHelpers.cmake."""
    directory = app_name if stable_build else f"{app_name} Daily"
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    elif os.name == "nt":
        base = Path(os.environ.get("APPDATA", str(Path.home())))
    else:
        base = Path.home() / ".local" / "share"
    return base / org_name / directory / "plugins"


# --- Process plumbing -----------------------------------------------------------------------


@dataclass
class Runner:
    """Runs commands, echoing what it runs. Captured output is returned, never swallowed."""

    env: dict[str, str] | None = None
    verbose: bool = True

    def capture(self, *args: str | Path, cwd: Path | None = None) -> tuple[int, str]:
        command = [str(arg) for arg in args]
        if self.verbose:
            print(f"  $ {' '.join(command)}", flush=True)
        result = subprocess.run(
            command, cwd=cwd, capture_output=True, text=True, check=False, env=self.env
        )
        return result.returncode, result.stdout + result.stderr

    def check(self, label: str, *args: str | Path, cwd: Path | None = None) -> str:
        returncode, output = self.capture(*args, cwd=cwd)
        if returncode != 0:
            raise GateError(f"{label} (exit {returncode})\n{output}")
        return output


def step(message: str) -> None:
    print(f"\n=== {message} ===", flush=True)


# --- Environment resolution --------------------------------------------------------------------


def version_key(name: str) -> tuple[int, ...]:
    """Numeric ordering for a dotted version directory name, falling back to zeros."""
    parts: list[int] = []
    for chunk in name.split("."):
        parts.append(int(chunk) if chunk.isdigit() else 0)
    return tuple(parts)


def resolve_qt_root(explicit: str | None) -> Path:
    if explicit:
        root = Path(explicit).expanduser()
    elif os.environ.get("QT_ROOT"):
        root = Path(os.environ["QT_ROOT"]).expanduser()
    else:
        candidates = sorted(
            (
                path
                for path in Path.home().glob("Qt/*/*")
                if (path / "lib" / "cmake" / "Qt6").is_dir()
            ),
            # Version-ordered, not lexicographic: '6.9.0' sorts after '6.11.1' as a string,
            # so a developer keeping an older Qt beside a newer one would silently have
            # every run built, staged and linted against the old one.
            key=lambda path: version_key(path.parent.name),
        )
        if not candidates:
            raise GateError("no Qt prefix found; pass --qt-root or set QT_ROOT")
        root = candidates[-1]
    if not (root / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake").is_file():
        raise GateError(f"{root} is not a Qt prefix (no lib/cmake/Qt6/qt.toolchain.cmake)")
    return root


def resolve_qmllint(qt_root: Path) -> Path:
    override = os.environ.get("QMLLINT")
    qmllint = (
        Path(override)
        if override
        else qt_root / "bin" / ("qmllint.exe" if os.name == "nt" else "qmllint")
    )
    if not os.access(qmllint, os.X_OK):
        raise GateError(f"qmllint not executable at {qmllint} (set QMLLINT)")
    return qmllint


def find_qgc_build_dir(plugin_root: Path) -> Path | None:
    """A QGC build dir to self-install the SDK from — a dev-loop convenience only.

    Nothing outside this function may assume a QGC source tree exists; that invariant is
    the whole point of the gate.
    """
    for parent in plugin_root.resolve().parents:
        if not (parent / "src" / "PluginAPI").is_dir():
            continue
        # Most recently configured wins, by the cache file's own mtime — this host keeps
        # several non-interchangeable build dirs (a CLI one and per-kit ones), and picking
        # by name would silently install the SDK from whichever sorts last.
        candidates = [parent / "build-test", *(parent / "build").glob("*")]
        configured = [c for c in candidates if (c / "CMakeCache.txt").is_file()]
        if configured:
            return max(configured, key=lambda c: (c / "CMakeCache.txt").stat().st_mtime)
    return None


def read_sdk_host_identity(sdk_prefix: Path) -> tuple[str, str, bool]:
    """APP_NAME / ORG_NAME / STABLE_BUILD, as the SDK's own CMake config exports them."""
    matches = list(sdk_prefix.glob("lib/cmake/QGCPluginAPI/QGCPluginAPIConfig.cmake"))
    if not matches:
        raise GateError(f"{sdk_prefix} has no QGCPluginAPIConfig.cmake to read host identity from")
    text = matches[0].read_text(encoding="utf-8")

    def value(name: str) -> str:
        match = re.search(rf'^set\({name}\s+"([^"]*)"\)', text, re.MULTILINE)
        if match is None:
            raise GateError(f"{matches[0]} does not export {name}")
        return match.group(1)

    stable = value("QGCPluginAPI_STABLE_BUILD").upper()
    return (
        value("QGCPluginAPI_APP_NAME"),
        value("QGCPluginAPI_ORG_NAME"),
        stable in {"ON", "TRUE", "1", "YES", "Y"},
    )


def stage_guaranteed_qt_qml(qt_root: Path, destination: Path) -> Path:
    """Symlink the guaranteed Qt QML modules into an import path of exactly that set.

    Combined with `qmllint --bare`, this is what isolates the gate from the developer's own
    Qt install: a module that is physically present but not guaranteed simply is not on the
    path, so importing it fails here the same way it fails where the host is built.
    """
    destination.mkdir(parents=True, exist_ok=True)
    source = qt_root / "qml"
    if not source.is_dir():
        raise GateError(f"{qt_root} has no qml/ directory to stage a guaranteed subset from")
    staged = 0
    for name in (*GUARANTEED_QML_MODULES, *GUARANTEED_QML_SUPPORT_FILES):
        origin = source / name
        if origin.exists():
            os.symlink(origin, destination / name)
            staged += 1
    if staged == 0:
        raise GateError(
            f"none of the guaranteed QML modules exist under {source} — the lint import "
            f"path would be empty and every plugin would fail for the wrong reason"
        )
    return destination


# --- The gate ------------------------------------------------------------------------------------


@dataclass
class GateContext:
    manifest: Manifest
    work_dir: Path
    scratch_plugin: Path
    build_dir: Path
    sdk_prefix: Path
    qt_root: Path
    qmllint: Path
    runner: Runner
    parallel: int
    qml_import_paths: list[Path] = field(default_factory=list)
    linted_file_count: int | None = None
    """None when the QML axis was declared N/A — not the same as having linted nothing."""
    tests_registered: int | None = None
    controls_run: int = 0
    """How many negative controls actually ran. The closing summary claims they all went
    red; that claim has to be counted, not assumed, since an axis declared N/A takes its
    controls with it."""


def copy_plugin_to_scratch(plugin_root: Path, destination: Path) -> None:
    # Anchored to the plugin root, never matched by name at any depth: a bare `build`
    # exclusion would quietly drop a real source directory that happened to be named
    # build/, and the gate would then verify a plugin that is not the one that ships.
    top_level = plugin_root.resolve()

    def ignore(directory: str, names: list[str]) -> set[str]:
        if Path(directory).resolve() != top_level:
            return set()
        return {name for name in names if _is_excluded_dir(name)}

    shutil.copytree(plugin_root, destination, ignore=ignore, symlinks=True)
    for parent in destination.resolve().parents:
        if (parent / "src" / "PluginAPI").is_dir():
            raise GateError(
                f"the scratch tree still sits inside a QGC checkout ({parent}) — the gate "
                f"would be meaningless"
            )


def configure_and_build(context: GateContext) -> Path:
    step("Configuring standalone (find_package(QGCPluginAPI), no QGC source present)")
    context.runner.check(
        "standalone configure",
        "cmake",
        "-B",
        context.build_dir,
        "-S",
        context.scratch_plugin,
        f"-DCMAKE_PREFIX_PATH={context.sdk_prefix};{context.qt_root}",
        f"-DCMAKE_TOOLCHAIN_FILE={context.qt_root / 'lib' / 'cmake' / 'Qt6' / 'qt.toolchain.cmake'}",
    )
    step("Building standalone (plugin and every test target)")
    context.runner.check(
        "standalone build",
        "cmake",
        "--build",
        context.build_dir,
        "--parallel",
        str(context.parallel),
    )
    artifact = context.build_dir / artifact_filename(context.manifest.artifact_name)
    if not artifact.is_file():
        raise GateError(f"the standalone build produced no artifact at {artifact}")
    return artifact


def run_test_axis(context: GateContext) -> None:
    axis = context.manifest.tests
    if not axis.enabled:
        step(f"Test axis declared N/A: {axis.reason}")
        # "Declared N/A" is a claim, and this gate does not take claims on trust. If the
        # standalone build registered tests after all, the reason is stale and the suite is
        # going unrun — the same silent reduction as a missing key, just written down.
        _, listing = context.runner.capture("ctest", "--test-dir", str(context.build_dir), "-N")
        registered = parse_ctest_total(listing)
        if registered:
            raise GateError(
                f"the test axis is declared N/A, but the standalone build registered "
                f"{registered} test(s) — they would never be run"
            )
        return
    step("Running the test suite standalone")
    if axis.cmake_lists is None or axis.count_pattern is None:
        raise ManifestError("tests.enabled is true without cmake_lists and count_pattern")
    cmake_lists = context.scratch_plugin / axis.cmake_lists
    if not cmake_lists.is_file():
        raise GateError(f"tests.cmake_lists names {axis.cmake_lists}, which does not exist")
    expected = expected_test_count(cmake_lists.read_text(encoding="utf-8"), axis.count_pattern)
    _, listing = context.runner.capture("ctest", "--test-dir", str(context.build_dir), "-N")
    found = parse_ctest_total(listing)
    if found != expected:
        raise GateError(
            f"standalone ctest registered {found} tests, expected {expected} — a suite that "
            f"isn't there cannot pass"
        )
    print(f"{expected} test targets registered standalone.")
    context.tests_registered = expected
    context.runner.check(
        "standalone tests",
        "ctest",
        "--test-dir",
        str(context.build_dir),
        "--output-on-failure",
    )


def run_forbidden_symbol_axis(context: GateContext, artifact: Path) -> None:
    axis = context.manifest.forbidden_symbols
    if not axis.enabled:
        step(f"Forbidden-symbol axis declared N/A: {axis.reason}")
        return
    step(f"Undefined-symbol check on the standalone artifact ({axis.reason})")

    # ELF: read the dynamic table, since a shared object's static symtab is routinely
    # stripped and `nm -u` would then report "no symbols" and exit 1. Mach-O has no such
    # split, and macOS nm rejects -D. Windows has no nm at all — the check's own empty-output
    # and exit-status guards are what stop a platform gap from reading as a clean result.
    nm_flags = ["-u"] if sys.platform == "darwin" else ["-D", "-u"]

    def nm(path: Path) -> tuple[int, str]:
        return context.runner.capture("nm", *nm_flags, str(path))

    check_forbidden_symbols(artifact, axis.patterns, nm)
    print("No forbidden undefined symbols.")


def find_plugin_qrc(scratch_plugin: Path, artifact_name: str) -> Path:
    qrc = scratch_plugin / f"{artifact_name}.qrc"
    if not qrc.is_file():
        raise GateError(
            f"no {qrc.name} beside the plugin's CMakeLists.txt — the QML axis reads the "
            f"resource file named after the artifact"
        )
    return qrc


def stage_flat_qml(context: GateContext) -> tuple[Path, dict[str, str]]:
    """Rebuild the flat resource layout the plugin actually ships, in a scratch dir."""
    qrc = find_plugin_qrc(context.scratch_plugin, context.manifest.artifact_name)
    flat_map = flatten_qml_entries(parse_qrc(qrc.read_text(encoding="utf-8")))
    sources = find_qml_sources(context.scratch_plugin, context.manifest.qml.source_roots)
    check_qrc_closure(flat_map.values(), sources, context.scratch_plugin)

    flat_dir = context.work_dir / "flat-qml"
    flat_dir.mkdir(parents=True, exist_ok=True)
    for alias, source in flat_map.items():
        if "/" in alias or "\\" in alias:
            raise GateError(
                f"the .qrc alias {alias!r} has a directory component — every .qml must be "
                f"aliased to a bare filename, or siblings do not resolve at runtime"
            )
        shutil.copy2(context.scratch_plugin / source, flat_dir / alias)
    return flat_dir, flat_map


def build_qml_import_paths(context: GateContext) -> list[Path]:
    paths = [stage_guaranteed_qt_qml(context.qt_root, context.work_dir / "qt-qml-allowlist")]
    published = context.sdk_prefix / "qml"
    if not (published / "QGroundControl" / "PluginUI").is_dir():
        raise GateError(f"{context.sdk_prefix} publishes no QML module under qml/QGroundControl")
    paths.append(published)

    own_module = context.manifest.qml.own_module
    if own_module is not None:
        relative = Path(*own_module.split("."))
        generated = context.build_dir / "qml" / relative
        if not generated.is_dir():
            raise GateError(
                f"qml.own_module declares {own_module}, but the standalone build generated no "
                f"module metadata at {generated} — the plugin's own QML would not resolve"
            )
        # Stage only the declared module, never the directory holding it: in-tree that
        # directory is the host build's whole qml/ tree, and putting it on the import path
        # would silently restore every base module the gate exists to keep off it.
        stage = context.work_dir / "own-module"
        (stage / relative).parent.mkdir(parents=True, exist_ok=True)
        os.symlink(generated, stage / relative)
        paths.append(stage)
    return paths


def run_qmllint(context: GateContext, files: Sequence[Path]) -> tuple[int, str]:
    command: list[str | Path] = [
        context.qmllint,
        # "do not include default import directories": without this, a module that is
        # physically installed in the developer's Qt resolves even though the SDK does not
        # guarantee it, and the gate goes green on a dependency CI cannot satisfy.
        "--bare",
        # qmllint's default severity for these is "warning", which prints but exits 0.
        "--import",
        "error",
        "--unresolved-type",
        "error",
    ]
    for path in context.qml_import_paths:
        command += ["-I", path]
    command += list(files)
    return context.runner.capture(*command)


@dataclass(frozen=True)
class QmlLintResult:
    returncode: int
    output: str
    """Already unflattened back to real source paths — triage patterns are matched against
    this, so a pattern written from what the log shows is the pattern that works."""
    error_lines: list[str]
    syntax_lines: list[str]
    unaccepted: list[str]
    unused: list[TriageEntry]


def lint_qml_set(
    context: GateContext, files: Sequence[Path], flat_map: dict[str, str] | None = None
) -> QmlLintResult:
    returncode, output = run_qmllint(context, files)
    if flat_map:
        flat_dir = files[0].parent if files else None
        if flat_dir is not None:
            for alias, source in flat_map.items():
                output = output.replace(str(flat_dir / alias), source)
    unaccepted, unused = triage_qmllint_output(output, context.manifest.qml.accepted_triage)
    return QmlLintResult(
        returncode=returncode,
        output=output,
        error_lines=qmllint_error_lines(output),
        syntax_lines=qmllint_syntax_lines(output),
        unaccepted=unaccepted,
        unused=unused,
    )


def run_qml_axis(context: GateContext) -> tuple[Path, dict[str, str]] | None:
    if not context.manifest.qml.enabled:
        # The two QML negative controls go with it: they exist to prove this axis can go
        # red, and a control for an axis that isn't running proves nothing.
        step(f"QML axis declared N/A: {context.manifest.qml.reason}")
        # And, as with the test axis, the claim is checked rather than believed — turning
        # this axis off is how a plugin would skip the lint, the two-way .qrc closure and
        # both QML controls at once.
        stray = find_qml_sources(context.scratch_plugin, ["."])
        if stray:
            raise GateError(
                "the QML axis is declared N/A, but the plugin ships QML:\n  " + "\n  ".join(stray)
            )
        return None
    step("Linting QML against the published module and the guaranteed Qt subset only")
    flat_dir, flat_map = stage_flat_qml(context)
    context.qml_import_paths = build_qml_import_paths(context)
    files = [flat_dir / alias for alias in sorted(flat_map)]
    context.linted_file_count = len(files)

    result = lint_qml_set(context, files, flat_map)
    print(result.output)

    if result.syntax_lines:
        # Never triageable, and checked before anything else: a file qmllint could not parse
        # produces no `Error:` line at all, so a verdict read off the error lines alone calls
        # it clean.
        raise GateError(
            "qmllint could not parse QML the plugin ships:\n  " + "\n  ".join(result.syntax_lines)
        )
    if result.returncode != 0 and not result.error_lines:
        # The backstop for anything outside both taxonomies — qmllint itself failing to run,
        # a future diagnostic class, a bad option. A non-zero exit that nothing accounts for
        # is a gate that does not know what it measured.
        raise GateError(
            f"qmllint exited {result.returncode} with no diagnostic this gate recognises — "
            f"the failure is outside the taxonomy triage reasons about:\n{result.output}"
        )
    if result.unaccepted:
        raise GateError(
            "qmllint found error(s) outside the accepted triage list:\n  "
            + "\n  ".join(result.unaccepted)
        )
    if result.unused:
        raise GateError(
            "accepted-triage entries that matched nothing in this run:\n  "
            + "\n  ".join(f"{entry.pattern} ({entry.reason})" for entry in result.unused)
            + "\nDelete them — a triage list that only grows silently swallows real "
            "diagnostics."
        )
    print(f"qmllint clean ({len(files)} files; known-accepted findings excluded).")
    return flat_dir, flat_map


def run_missing_dependency_control(context: GateContext) -> None:
    axis = context.manifest.missing_dependency_control
    if not axis.enabled:
        step(f"Negative control (missing SDK dependency) declared N/A: {axis.reason}")
        return
    if axis.sdk_subpath is None or axis.build_target is None or axis.expected_pattern is None:
        raise ManifestError(
            "missing_dependency_control.enabled is true without sdk_subpath, build_target "
            "and expected_pattern"
        )
    step(f"Negative control: the C++ axis fails without {axis.sdk_subpath}")
    if not (context.sdk_prefix / axis.sdk_subpath).exists():
        raise GateError(
            f"missing_dependency_control names {axis.sdk_subpath}, which the SDK does not "
            f"contain — removing it would remove nothing and the control would test nothing"
        )
    crippled = context.work_dir / "sdk-without-dependency"
    removed = Path(axis.sdk_subpath)

    def ignore(directory: str, names: list[str]) -> set[str]:
        try:
            relative = Path(directory).resolve().relative_to(context.sdk_prefix.resolve())
        except ValueError:
            return set()
        return {name for name in names if relative / name == removed}

    shutil.copytree(context.sdk_prefix, crippled, ignore=ignore, symlinks=True)

    build = context.work_dir / "neg-build"
    returncode, output = context.runner.capture(
        "cmake",
        "-B",
        str(build),
        "-S",
        str(context.scratch_plugin),
        f"-DCMAKE_PREFIX_PATH={crippled};{context.qt_root}",
        f"-DCMAKE_TOOLCHAIN_FILE={context.qt_root / 'lib' / 'cmake' / 'Qt6' / 'qt.toolchain.cmake'}",
    )
    if returncode == 0:
        # Either layer may catch it and which one is not the point — only that the failure
        # is about the missing dependency. Asserting one specific stage would make this
        # control fail on a packaging improvement.
        returncode, build_output = context.runner.capture(
            "cmake",
            "--build",
            str(build),
            "--target",
            axis.build_target,
            "--parallel",
            str(context.parallel),
        )
        output += build_output
    assert_control_failed(
        "negative control (missing SDK dependency)", returncode, output, axis.expected_pattern
    )
    context.controls_run += 1
    print("OK — failed as designed, on the missing dependency.")


def run_unpublished_type_control(context: GateContext) -> None:
    step("Negative control: the QML axis rejects a type outside the published subset")
    fixture = context.work_dir / "UnpublishedTypeControl.qml"
    fixture.write_text(
        "import QtQuick\n"
        "import QGroundControl.PluginUI\n"
        "\n"
        f"Item {{ {UNPUBLISHED_TYPE_FIXTURE} {{}} }}\n",
        encoding="utf-8",
    )
    returncode, output = run_qmllint(context, [fixture])
    assert_control_failed(
        "negative control (unpublished type)",
        returncode,
        output,
        rf"{UNPUBLISHED_TYPE_FIXTURE} was not found",
    )
    # ...and rejected for the right reason. When a module fails to import outright, qmllint
    # reports "<Type> was not found" for every type in it — so without this second assertion
    # the control would still go green if the published module stopped being published at
    # all, while claiming to have tested the subset boundary inside a module that resolves.
    if "Failed to import QGroundControl.PluginUI" in output:
        raise GateError(
            "the unpublished-type control was rejected only because the whole published "
            "module failed to import — it proves nothing about the subset boundary.\n" + output
        )
    context.controls_run += 1
    print("OK — unpublished type rejected, from inside a module that resolves.")


def run_base_module_reach_control(
    context: GateContext, flat_dir: Path, flat_map: dict[str, str]
) -> None:
    step("Negative control: the QML axis rejects a reach back into a base QGC module")
    # Routed through the same lint path the real axis uses — its import paths, its error
    # extraction, its triage — because a control that calls qmllint directly tests
    # everything except the thing under test. A triage class widened by accident leaves the
    # real gate green while a raw-qmllint control still goes red.
    targets = sorted(flat_dir.glob("*.qml"))
    if not targets:
        raise GateError("no staged QML to inject a base-module reach into")
    target = targets[0]
    original = target.read_text(encoding="utf-8")
    target.write_text(inject_import(original, BASE_MODULE_REACH_IMPORT), encoding="utf-8")
    try:
        result = lint_qml_set(context, targets, flat_map)
        # Assert on the gate's VERDICT, not on qmllint's raw output. Neither of the raw
        # signals is evidence on its own: for any plugin with a triage entry the exit code is
        # non-zero on every run by construction (an entry that matches nothing is itself a
        # failure), and the module name appears in the output whenever qmllint echoes a
        # source line — including a merely-unused import that resolved fine. Reduced to the
        # unaccepted set, both assertions mean what they say: the reach was rejected, and it
        # was rejected as an unaccepted error rather than triaged away.
        assert_control_failed(
            "negative control (base-module reach)",
            1 if result.unaccepted else 0,
            "\n".join(result.unaccepted) or result.output,
            re.escape(BASE_MODULE_REACH_IMPORT),
        )
    finally:
        target.write_text(original, encoding="utf-8")
    context.controls_run += 1
    print("OK — base-module import rejected by the gate's own lint path.")


def deploy(context: GateContext, artifact: Path) -> Path:
    step("Deploying the standalone-built plugin")
    app_name, org_name, stable = read_sdk_host_identity(context.sdk_prefix)
    destination = host_plugin_dir(app_name, org_name, stable)
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(artifact, destination / artifact.name)
    print(f"Deployed to: {destination}")
    return destination


def run_gate(args: argparse.Namespace) -> int:
    plugin_root = Path(args.plugin).expanduser().resolve()
    if not plugin_root.is_dir():
        raise GateError(f"{plugin_root} is not a directory")
    manifest = load_manifest(plugin_root)

    if not (plugin_root / "CMakeLists.txt").is_file():
        raise GateError(f"no CMakeLists.txt in {plugin_root}")
    step("Checking declared Qt components against the SDK's guaranteed set")
    cmake_texts = "\n".join(
        path.read_text(encoding="utf-8") for path in find_cmake_lists(plugin_root)
    )
    check_qt_components(manifest.qt_components, cmake_texts)
    print(f"Declared components all guaranteed: {', '.join(manifest.qt_components)}")

    qt_root = resolve_qt_root(args.qt_root)
    qmllint = resolve_qmllint(qt_root)

    if args.work:
        work_dir = Path(args.work).expanduser().resolve()
        if work_dir.exists():
            shutil.rmtree(work_dir)
        work_dir.mkdir(parents=True)
        temporary = None
    else:
        temporary = tempfile.mkdtemp(prefix="plugin-out-of-tree.")
        work_dir = Path(temporary)

    # Every child build runs against a scratch HOME. A plugin's CMake may deploy itself on
    # POST_BUILD — the dual-mode reference plugin does, and so does the in-tree helper it
    # mirrors — and verifying a plugin must not overwrite the copy the developer is actually
    # running, least of all with a deliberately crippled negative-control build. --deploy is
    # the only path to the real plugin dir, and it copies from here, afterwards, explicitly.
    sandbox_home = work_dir / "home"
    sandbox_home.mkdir(parents=True, exist_ok=True)
    child_env = dict(os.environ)
    child_env["HOME"] = str(sandbox_home)
    child_env["APPDATA"] = str(sandbox_home)
    runner = Runner(env=child_env)

    passed = False
    try:
        if args.sdk:
            sdk_prefix = Path(args.sdk).expanduser().resolve()
        else:
            build_dir = (
                Path(args.build_dir).resolve()
                if args.build_dir
                else find_qgc_build_dir(plugin_root)
            )
            if build_dir is None:
                raise GateError(
                    "no QGC build dir found to install the SDK from; pass --sdk or --build-dir"
                )
            step(f"Installing the QGCPluginSDK component from {build_dir}")
            sdk_prefix = work_dir / "sdk"
            runner.check(
                "installing the QGCPluginSDK component",
                "cmake",
                "--install",
                str(build_dir),
                "--component",
                "QGCPluginSDK",
                "--prefix",
                str(sdk_prefix),
            )
        if not (sdk_prefix / "include" / "QGCPluginAPI").is_dir():
            raise GateError(f"{sdk_prefix} is not a QGCPluginSDK prefix (no include/QGCPluginAPI)")

        step("Copying the plugin to a scratch tree with no QGroundControl source")
        scratch_plugin = work_dir / plugin_root.name
        copy_plugin_to_scratch(plugin_root, scratch_plugin)
        print(f"Scratch tree: {scratch_plugin}")

        context = GateContext(
            manifest=manifest,
            work_dir=work_dir,
            scratch_plugin=scratch_plugin,
            build_dir=scratch_plugin / "build-standalone",
            sdk_prefix=sdk_prefix,
            qt_root=qt_root,
            qmllint=qmllint,
            runner=runner,
            parallel=args.parallel,
        )

        artifact = configure_and_build(context)
        run_test_axis(context)
        run_forbidden_symbol_axis(context, artifact)
        qml_result = run_qml_axis(context)
        run_missing_dependency_control(context)
        if qml_result is not None:
            run_unpublished_type_control(context)
            run_base_module_reach_control(context, *qml_result)

        deployed = deploy(context, artifact) if args.deploy else None

        if args.report_json:
            report = {
                "plugin": str(plugin_root),
                "artifact": str(artifact),
                "sdk_prefix": str(sdk_prefix),
                "tests_registered": context.tests_registered,
                "qml_files_linted": context.linted_file_count,
                "controls_run": context.controls_run,
                "deployed_to": str(deployed) if deployed else None,
            }
            Path(args.report_json).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(f"\nReport written to {args.report_json}")

        passed = True
        # The control count is stated, not assumed. Claiming "every negative control went
        # red" over a run where an N/A axis took its controls with it is the same class of
        # untrue-but-reassuring summary the whole gate exists to eliminate.
        print(
            f"\nOut-of-tree gate PASSED: built and inspected against {sdk_prefix} with no "
            f"QGC source present; {context.controls_run} negative control(s) ran and every "
            f"one went red as designed."
        )
        print(f"artifact={artifact}")
        return 0
    finally:
        if temporary is not None and passed and not args.keep:
            shutil.rmtree(temporary, ignore_errors=True)
        else:
            print(f"Scratch tree kept at: {work_dir}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0] if __doc__ else None,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("plugin", help="the plugin directory to verify")
    parser.add_argument("--sdk", help="an already-installed QGCPluginSDK prefix")
    parser.add_argument("--build-dir", help="a QGC build dir to self-install the SDK from")
    parser.add_argument("--qt-root", help="Qt prefix (default: $QT_ROOT, else the newest ~/Qt/*/*)")
    parser.add_argument(
        "--work",
        help="scratch directory, kept after the run (default: a fresh temporary one, "
        "removed on success)",
    )
    parser.add_argument("--parallel", type=int, default=4, help="build jobs (default 4)")
    parser.add_argument(
        "--deploy",
        action="store_true",
        help="on success, copy the standalone artifact into the host's runtime plugin dir",
    )
    parser.add_argument("--keep", action="store_true", help="keep the scratch tree on success")
    parser.add_argument("--report-json", help="write a machine-readable result to this file")
    args = parser.parse_args(argv)

    try:
        return run_gate(args)
    except GateError as exc:
        print(f"\nGATE FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
