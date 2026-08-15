#!/usr/bin/env python3
"""Derive a reproducible upstream PR branch from the fork mainline.

The fork mainline (`plugin-infrastructure-with-qdrive`) carries everything and never
regresses; PR branches sent upstream are cut from `upstream/master` and regenerated
from mainline path-by-path, never hand-maintained in parallel. This script is the single
harness behind every such derivation — each PR is a `PRSpec` (include paths pulled
wholesale from mainline, a doc-content patch, and delete paths removed after) rather than
its own bespoke script.

Examples:
    ./tools/derive_pr_branch.py plugin-sdk
    ./tools/derive_pr_branch.py plugin-sdk --verify-only
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from _bootstrap import ensure_tools_dir

ensure_tools_dir(__file__)

from common.file_traversal import find_repo_root
from common.git import run_git
from common.logging import log_error, log_info, log_ok, log_step
from common.proc import run_captured

# .github/CONTRIBUTING.md requires Conventional Commits on every commit in a PR, and
# .github/workflows/pr-checks.yml lints the PR *title* to the same grammar. A derived branch
# is what upstream actually reads, so its subjects must satisfy this — the fork's own plain
# subjects stop at mainline. Shared with check_pr_routing.py so there is one owner.
CONVENTIONAL_SUBJECT = re.compile(
    r"^(feat|fix|perf|revert|docs|style|chore|refactor|test|build|ci)(\([^)]*\))?!?: .+"
)

# .github/workflows/pr-checks.yml caps the PR title at 80; CONTRIBUTING points at the usual
# git convention for subjects. 72 keeps `git log --oneline` readable and clears both.
MAX_SUBJECT_LENGTH = 72


@dataclass(frozen=True)
class PRSpec:
    """One upstream PR branch's derivation recipe."""

    branch: str
    source_ref: str
    """Ref the PR branch is cut from (e.g. `upstream/master`, or another PR branch for stacking)."""
    mainline_ref: str
    """Ref content is pulled from (always the fork mainline)."""
    include_paths: tuple[str, ...]
    """Paths taken wholesale from `mainline_ref` via `git checkout -- <path>`."""
    upstream_ref: str = "upstream/master"
    """The true upstream base this PR ultimately targets, and the only sound ref to measure
    staleness against. When `source_ref` is another *derived* branch (stacking), that branch's
    content has already been collapsed to mainline's by its own derivation — so comparing
    against it hides every revert the stack inherits, and reports the lower branch's own
    derived commits as though they were work being dropped. Staleness is measured here."""
    patch_paths: tuple[str, ...] = ()
    """Paths where only the mainline-vs-`source_ref` diff is applied (shared files with
    content from multiple PRs interleaved; a wholesale take would pull in the other PR's
    content too)."""
    delete_paths: tuple[str, ...] = ()
    """Paths removed after the includes land (e.g. a submodule excluded from this PR)."""
    doc_rewrites: tuple[tuple[str, tuple[tuple[str, str], ...]], ...] = ()
    """(path, (old_text, new_text)...) — content rewrites applied after includes/patches
    land, for mentions of an excluded plugin that live in an otherwise-generic shared
    file. Every path here must also be in include_paths (asserted in __post_init__) — a
    rewrite path missing from include_paths would be silently skipped by derive() and
    never scanned by verify()'s forbidden-terms grep, since that's scoped to include_paths
    too."""
    forbidden_terms: tuple[str, ...] = field(default_factory=lambda: ("qdrive", "claude"))
    """Case-insensitive terms that must not appear in any tracked file on the derived branch."""
    seam_tokens: tuple[str, ...] = ()
    """Symbols whose every base-app consumer must ship in this PR. A contribution seam is
    inert without the code that reads it — carrying all of `src/PluginSystem` while the QML
    that binds to `QGroundControl.pluginManager` stays behind yields a branch that compiles,
    passes its tests, and does nothing. `check_seam_consumers` is what makes that loud."""
    seam_exempt: tuple[str, ...] = ()
    """Paths allowed to mention a seam token without being included — prose that merely
    names the symbol, or this script itself."""

    commit_subject: str = ""
    """Conventional Commits subject for a spec that derives as one commit.

    Required unless `commit_groups` is set. There is no generic fallback on purpose: the
    old `Derive <branch> from <ref>` subject is not Conventional Commits, so every branch
    this harness produced would have failed .github/CONTRIBUTING.md's PR requirements the
    moment it was opened — invisible locally, because nothing on the fork checks it."""

    commit_groups: tuple[tuple[str, tuple[str, ...]], ...] = ()
    """Ordered (subject, paths) groups, each committed separately.

    Empty means one synthetic commit for the whole branch, which is right for a spec of a
    few files. For a large spec it is not: a reviewer gets one commit of hundreds of files
    whose message says only that a script generated it, with no bisect granularity and none
    of the rationale that accumulated on mainline.

    Determinism is unaffected — the groups are declared here, so the same inputs still
    produce the same tree *and* now the same history on every re-derivation. Grouping is
    path-level like everything else: a group lists paths, never hunks.

    Every include/patch path must appear in exactly one group (asserted below). That is
    deliberate friction — adding a path to include_paths without saying which commit tells
    its story is the same unrecorded decision as adding it without a spec at all."""

    def __post_init__(self) -> None:
        assert self.branch != self.mainline_ref, (
            f"PRSpec.branch must not equal mainline_ref — 'branch -f' would reset the "
            f"fork mainline itself ({self.mainline_ref!r})"
        )
        for rewrite_path, _ in self.doc_rewrites:
            included = any(
                rewrite_path == inc or rewrite_path.startswith(f"{inc}/")
                for inc in self.include_paths
            )
            assert included, (
                f"doc_rewrites path {rewrite_path!r} on {self.branch!r} is not covered by "
                f"include_paths — it would be silently skipped by derive() and never "
                f"scanned by verify()'s forbidden-terms grep"
            )
        assert bool(self.commit_subject) != bool(self.commit_groups), (
            f"{self.branch!r} must set exactly one of commit_subject (one commit) or "
            f"commit_groups (a series) — neither leaves the branch with no message, both "
            f"leaves it ambiguous which one derive() should use"
        )
        for subject in [self.commit_subject, *(s for s, _ in self.commit_groups)]:
            if not subject:
                continue
            assert CONVENTIONAL_SUBJECT.match(subject), (
                f"{self.branch!r} subject is not Conventional Commits, which "
                f".github/CONTRIBUTING.md requires of every commit in a PR: {subject!r}"
            )
            assert len(subject) <= MAX_SUBJECT_LENGTH, (
                f"{self.branch!r} subject is {len(subject)} chars, over the "
                f"{MAX_SUBJECT_LENGTH} cap: {subject!r}"
            )
        if self.commit_groups:
            grouped: list[str] = []
            for _subject, paths in self.commit_groups:
                grouped.extend(paths)
            duplicated = sorted({p for p in grouped if grouped.count(p) > 1})
            assert not duplicated, (
                f"commit_groups on {self.branch!r} list these paths more than once, so the "
                f"later group would commit nothing: {duplicated}"
            )
            declared = set(self.include_paths) | set(self.patch_paths)
            missing = sorted(declared - set(grouped))
            assert not missing, (
                f"commit_groups on {self.branch!r} omit {len(missing)} declared path(s) — "
                f"they would land in no commit at all: {missing}"
            )
            unknown = sorted(set(grouped) - declared)
            assert not unknown, (
                f"commit_groups on {self.branch!r} name paths absent from include_paths/"
                f"patch_paths, so nothing would be staged for them: {unknown}"
            )


PLUGIN_SDK = PRSpec(
    branch="upstream-pr-plugin-sdk",
    source_ref="upstream-pr-replay-fidelity",
    mainline_ref="plugin-infrastructure-with-qdrive",
    include_paths=(
        "src/PluginAPI",
        "src/PluginSystem",
        "test/PluginSystem",
        "test/UnitTestFramework/BaseClasses/TempDirectoryTest.h",
        "test/UnitTestFramework/BaseClasses/CMakeLists.txt",
        "plugins/example",
        "plugins/README.md",
        "plugins/CMakeLists.txt",
        "cmake/modules/PluginHelpers.cmake",
        "cmake/install/InstallPluginSDK.cmake",
        "cmake/install/QGCPluginAPIConfig.cmake.in",
        # The build-marker generator: included by PluginHelpers.cmake in-tree and
        # installed beside the config file above, so a standalone plugin calls the
        # same function rather than reimplementing it.
        "cmake/install/QGCPluginBuildMarker.cmake",
        # Derives the packaged form of the QGroundControl.PluginUI module from
        # src/PluginSystem/PluginUI/qmldir; InstallPluginSDK.cmake invokes it.
        "tools/derive_plugin_ui_sdk.py",
        "tools/tests/test_derive_plugin_ui_sdk.py",
        # The out-of-tree gate. Ships inside the SDK package (InstallPluginSDK.cmake), so a
        # third-party author runs the same tool CI does; the schema is its manifest contract.
        "tools/verify_plugin_out_of_tree.py",
        "tools/plugin-verify.schema.json",
        "tools/tests/test_verify_plugin_out_of_tree.py",
        # The QML contract watchdog (out-of-tree-plugins.md U5), invoked by the patched-in
        # macos.yml step "Check QML contract (frozen-tier watchdog)". Without this the derived
        # branch's CI would call a script that doesn't exist on it.
        "tools/check_plugin_ui_contract.py",
        "tools/tests/test_check_plugin_ui_contract.py",
        # The .qgcplugin packer. Ships inside the SDK package alongside the gate above, so
        # an author produces packages against the same rules PluginInstaller enforces.
        "tools/pack_plugin.py",
        "tools/tests/test_pack_plugin.py",
        "src/QGCApplication.h",
        "src/QGCApplication.cc",
        "src/Comms/MAVLinkProtocol.h",
        "src/Comms/MAVLinkProtocol.cc",
        "src/Comms/LogReplayLink.h",
        "src/Comms/LogReplayLink.cc",
        "src/Comms/LinkManager.h",
        "src/Comms/LinkManager.cc",
        "src/Comms/ReplaySeekApplier.h",
        "src/Comms/ReplaySeekApplier.cc",
        "src/Settings/SettingsManager.h",
        "src/Settings/SettingsManager.cc",
        "src/FactSystem/ParameterManager.h",
        "src/FactSystem/ParameterManager.cc",
        "src/MissionManager/PlanManager.h",
        "src/MissionManager/PlanManager.cc",
        "src/MissionManager/MissionManager.h",
        "src/Vehicle/TrajectoryPoints.h",
        "src/Vehicle/TrajectoryPoints.cc",
        "src/Vehicle/Vehicle.h",
        "src/Vehicle/Vehicle.cc",
        # The tool-drawer exit-path fix (360b27945): plugin-agnostic — any custom
        # toolbarSource that fails to load could strand the user with no exit. Folded into
        # this spec rather than a sibling (Open questions, decided): the trap only bites
        # plugin toolbars even though the file is core UI.
        "src/MainWindow/MainWindow.qml",
        "test/QmlUITests/CMakeLists.txt",
        "test/QmlUITests/ToolDrawerEscapeUITest.cc",
        "test/QmlUITests/ToolDrawerEscapeUITest.h",
        "src/FlyView/FlyViewPluginPanel.qml",
        "src/FlyView/FlyViewPluginButtonStrip.qml",
        "src/FlyView/FlightDisplayViewReplayVideo.qml",
        "src/FlyView/CMakeLists.txt",
        "src/PlanView/PlanViewPluginPanel.qml",
        "src/PlanView/PlanViewPluginButtonStrip.qml",
        "src/PlanView/PlanViewActionContext.h",
        "src/PlanView/PlanViewActionContext.cc",
        "src/PlanView/CMakeLists.txt",
        # The consumption side of the contribution seams. QGCPluginManager's four
        # Q_PROPERTYs (toolMenuItems, replayExtension, flyViewPanelItems,
        # planViewPanelItems) are inert without these: QGroundControlQmlGlobal is what
        # puts `pluginManager` on the QML global in the first place, and the rest are the
        # only things that bind to it. Their CMake registration lives in
        # src/QmlControls/ and src/Toolbar/CMakeLists.txt, both unmodified upstream.
        "src/QmlControls/QGroundControlQmlGlobal.h",
        "src/QmlControls/QGroundControlQmlGlobal.cc",
        "src/FlyView/FlyView.qml",
        "src/FlyView/FlyViewVideo.qml",
        "src/FlyView/FlyViewWidgetLayer.qml",
        "src/PlanView/PlanView.qml",
        "src/Toolbar/SelectViewDropdown.qml",
        "src/AppSettings/PluginSettings.qml",
        "src/AppSettings/CMakeLists.txt",
        "src/Settings/PluginSettings.h",
        "src/Settings/PluginSettings.cc",
        "src/Settings/CMakeLists.txt",
        # Registration-by-data for the settings page above. PluginSettings.qml is inert
        # without the SettingsPages.json entry that lists it and the icon that entry names:
        # the page ships, nothing links to it, and `qrc:/InstrumentValueIcons/plugins.svg`
        # dangles. seam_tokens cannot catch this — the reference is a JSON string, not a
        # symbol any grep for QGCPluginManager would see.
        "src/AppSettings/pages/SettingsPages.json",
        "resources/InstrumentValueIcons/plugins.svg",
        # FactMetaData backing PluginSettings.cc's settings group. An empty facts list is
        # still load-bearing: SettingsManager resolves the group by file at startup.
        "src/Settings/Plugin.SettingsGroup.json",
        # The core-plugin side of the split between the compiled-in core plugin and runtime
        # plugins (3aa59f821). QGCCorePlugin is what hands the runtime plugin system its
        # entry point, so PR 1 does not build without this delta.
        "src/API/QGCCorePlugin.h",
        "src/API/QGCCorePlugin.cc",
        "src/API/README.md",
        # The plan-view toolbar's plugin indicator strip (22b5e1cb2) — the PlanView half of
        # the same contribution seam FlyView's panels use.
        "src/PlanView/PlanToolBarIndicators.qml",
        # macOS release signing. Under the hardened runtime dyld refuses to load any library
        # not signed by the app's own Team ID, so without this entitlement a correctly-signed
        # third-party plugin cannot load in a release build at all — the SDK ships and does
        # nothing on the one configuration users actually install.
        "cmake/install/SignMacBundle.cmake",
        "deploy/macos/qgroundcontrol-release.entitlements",
        # Covers LogReplayConfiguration::deferStreamStart, which is this PR's addition to
        # LogReplayLink (not replay-fidelity's). The file exists and is registered upstream,
        # so a wholesale take needs no CMake change.
        "test/Comms/LinkConfigurationTest.h",
        "test/Comms/LinkConfigurationTest.cc",
        "src/Comms/CMakeLists.txt",
        "src/MAVLink/CMakeLists.txt",
        "CMakeLists.txt",
        "src/CMakeLists.txt",
        "src/qgc_version.h.in",
        "test/CMakeLists.txt",
    ),
    patch_paths=(
        ".github/workflows/macos.yml",
        # Shared with every other hook in the file, so it is patched rather than taken
        # wholesale. The delta this PR needs is the schema hook for plugin-verify.json —
        # without it the branch would carry the schema and the manifests with nothing
        # enforcing them. NB the patch is the whole source_ref→mainline delta on this path,
        # so a second fork-only hook landing here would ride along and need splitting out.
        ".pre-commit-config.yaml",
    ),
    delete_paths=("plugins/qdrive", ".gitmodules"),
    seam_tokens=("QGCPluginManager", "QGroundControl.pluginManager"),
    seam_exempt=(
        # src/API/README.md was exempt here while it stayed behind on mainline; it now
        # ships in include_paths, which already suppresses the seam check.
        "tools/derive_pr_branch.py",  # this file names the tokens it checks for
        # Uses a real base-app path as fixture data, so it mentions the token without
        # consuming the seam. Test data that mirrors a production symbol trips greps
        # written against production.
        "tools/tests/test_check_pr_routing.py",
    ),
    # Six commits instead of one 165-file blob. The order is a reading order for a reviewer
    # who has never seen this work: the ABI boundary first, then the system behind it, then
    # what it lets a plugin reach, then where a plugin shows up, then a worked example, then
    # the gates that keep it honest. Each builds on the last; none is independently mergeable
    # (that is what the PR is for), which is why these are commits and not further stacking.
    commit_groups=(
        (
            "feat(PluginAPI): add the plugin SDK boundary library",
            (
                "src/PluginAPI",
                "cmake/install/QGCPluginAPIConfig.cmake.in",
            ),
        ),
        (
            "feat(PluginSystem): add the runtime plugin system",
            (
                "src/PluginSystem",
                "src/API/QGCCorePlugin.h",
                "src/API/QGCCorePlugin.cc",
                "src/API/README.md",
                "src/Settings/PluginSettings.h",
                "src/Settings/PluginSettings.cc",
                "src/Settings/Plugin.SettingsGroup.json",
                "src/Settings/SettingsManager.h",
                "src/Settings/SettingsManager.cc",
                "src/Settings/CMakeLists.txt",
                "src/QGCApplication.h",
                "src/QGCApplication.cc",
                "src/qgc_version.h.in",
                "CMakeLists.txt",
                "src/CMakeLists.txt",
            ),
        ),
        (
            "feat(PluginSystem): expose host services to plugins",
            (
                "src/Comms/MAVLinkProtocol.h",
                "src/Comms/MAVLinkProtocol.cc",
                "src/Comms/LogReplayLink.h",
                "src/Comms/LogReplayLink.cc",
                "src/Comms/LinkManager.h",
                "src/Comms/LinkManager.cc",
                "src/Comms/ReplaySeekApplier.h",
                "src/Comms/ReplaySeekApplier.cc",
                "src/Comms/CMakeLists.txt",
                "src/FactSystem/ParameterManager.h",
                "src/FactSystem/ParameterManager.cc",
                "src/MissionManager/PlanManager.h",
                "src/MissionManager/PlanManager.cc",
                "src/MissionManager/MissionManager.h",
                "src/Vehicle/TrajectoryPoints.h",
                "src/Vehicle/TrajectoryPoints.cc",
                "src/Vehicle/Vehicle.h",
                "src/Vehicle/Vehicle.cc",
                "src/MAVLink/CMakeLists.txt",
            ),
        ),
        (
            "feat(PluginSystem): add plugin contribution points to the UI",
            (
                "src/QmlControls/QGroundControlQmlGlobal.h",
                "src/QmlControls/QGroundControlQmlGlobal.cc",
                "src/FlyView/FlyViewPluginPanel.qml",
                "src/FlyView/FlyViewPluginButtonStrip.qml",
                "src/FlyView/FlightDisplayViewReplayVideo.qml",
                "src/FlyView/FlyView.qml",
                "src/FlyView/FlyViewVideo.qml",
                "src/FlyView/FlyViewWidgetLayer.qml",
                "src/FlyView/CMakeLists.txt",
                "src/PlanView/PlanViewPluginPanel.qml",
                "src/PlanView/PlanViewPluginButtonStrip.qml",
                "src/PlanView/PlanViewActionContext.h",
                "src/PlanView/PlanViewActionContext.cc",
                "src/PlanView/PlanView.qml",
                "src/PlanView/PlanToolBarIndicators.qml",
                "src/PlanView/CMakeLists.txt",
                "src/Toolbar/SelectViewDropdown.qml",
                "src/AppSettings/PluginSettings.qml",
                "src/AppSettings/pages/SettingsPages.json",
                "src/AppSettings/CMakeLists.txt",
                "resources/InstrumentValueIcons/plugins.svg",
                "src/MainWindow/MainWindow.qml",
            ),
        ),
        (
            "feat(PluginSystem): add the example plugin and SDK packaging",
            (
                "plugins/example",
                "plugins/README.md",
                "plugins/CMakeLists.txt",
                "cmake/modules/PluginHelpers.cmake",
                "cmake/install/InstallPluginSDK.cmake",
                "cmake/install/QGCPluginBuildMarker.cmake",
                "tools/derive_plugin_ui_sdk.py",
                "tools/pack_plugin.py",
                "cmake/install/SignMacBundle.cmake",
                "deploy/macos/qgroundcontrol-release.entitlements",
            ),
        ),
        (
            "test(PluginSystem): add plugin system tests and CI gates",
            (
                "test/PluginSystem",
                "test/UnitTestFramework/BaseClasses/TempDirectoryTest.h",
                "test/UnitTestFramework/BaseClasses/CMakeLists.txt",
                "test/QmlUITests/ToolDrawerEscapeUITest.h",
                "test/QmlUITests/ToolDrawerEscapeUITest.cc",
                "test/QmlUITests/CMakeLists.txt",
                "test/Comms/LinkConfigurationTest.h",
                "test/Comms/LinkConfigurationTest.cc",
                "test/CMakeLists.txt",
                "tools/verify_plugin_out_of_tree.py",
                "tools/plugin-verify.schema.json",
                "tools/check_plugin_ui_contract.py",
                "tools/tests/test_derive_plugin_ui_sdk.py",
                "tools/tests/test_verify_plugin_out_of_tree.py",
                "tools/tests/test_check_plugin_ui_contract.py",
                "tools/tests/test_pack_plugin.py",
                ".github/workflows/macos.yml",
                ".pre-commit-config.yaml",
            ),
        ),
    ),
    doc_rewrites=(
        (
            "plugins/README.md",
            (
                (
                    "`plugins/example/` for a working `TIER SDK` plugin and `plugins/qdrive/` for `TIER\n"
                    "INTERNAL`.",
                    "`plugins/example/` for a working `TIER SDK` plugin. `TIER INTERNAL` grants full "
                    "access to QGC internals for a plugin that ships alongside a specific host build.",
                ),
                (
                    "normal CMake commands after the call — see\n"
                    "[`plugins/qdrive/CMakeLists.txt`](qdrive/CMakeLists.txt) for an example with extra Qt modules.",
                    "normal CMake commands after the call — see `plugins/example/CMakeLists.txt`.",
                ),
                (
                    "`plugins/example/CMakeLists.txt` and `plugins/qdrive/CMakeLists.txt`.",
                    "`plugins/example/CMakeLists.txt`.",
                ),
                (
                    "the tier every plugin in this tree uses (`example`, `qdrive`, and the test\n"
                    "  fixture).",
                    "the tier every plugin in this tree uses (`example` and the test\n  fixture).",
                ),
                (
                    "plugin (out-of-tree QDrive included) can adopt it by adding its own manifest.",
                    "plugin, including one built entirely out-of-tree, can adopt it by adding its "
                    "own manifest.",
                ),
            ),
        ),
        (
            "src/PluginSystem/README.md",
            (
                (
                    "- `example/` — minimal plugin with a tool menu item\n- `qdrive/` — full-featured plugin with AWS integration\n",
                    "- `example/` — minimal plugin with a tool menu item\n",
                ),
            ),
        ),
        (
            "src/MAVLink/CMakeLists.txt",
            (
                (
                    "# Export as CACHE INTERNAL so SDK-tier plugins (e.g. plugins/qdrive/CMakeLists.txt)\n"
                    "# can consume these paths directly.",
                    "# Export as CACHE INTERNAL so SDK-tier plugins can consume these paths directly.",
                ),
            ),
        ),
    ),
)

REPLAY_FIDELITY = PRSpec(
    branch="upstream-pr-replay-fidelity",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    commit_subject="fix(Comms): rebuild replay state when seeking a log",
    include_paths=(
        "src/Comms/LogReplayLink.h",
        "src/Comms/LogReplayLink.cc",
        "src/Comms/ReplaySeekApplier.h",
        "src/Comms/ReplaySeekApplier.cc",
        "src/Comms/LinkManager.h",
        "src/Comms/LinkManager.cc",
        "src/Comms/CMakeLists.txt",
        "src/FactSystem/ParameterManager.h",
        "src/FactSystem/ParameterManager.cc",
        "src/MissionManager/PlanManager.h",
        "src/MissionManager/PlanManager.cc",
        "src/MissionManager/MissionManager.h",
        "src/MissionManager/PlanMasterController.cc",
        "src/Vehicle/TrajectoryPoints.h",
        "src/Vehicle/TrajectoryPoints.cc",
        "src/Vehicle/Vehicle.h",
        "src/Vehicle/Vehicle.cc",
        "src/FlyView/FlyViewMap.qml",
        "src/QmlControls/LogReplayStatusBar.qml",
        "test/Comms/LogReplayLinkTest.h",
        "test/Comms/LogReplayLinkTest.cc",
        "test/Comms/ReplaySeekApplierTest.h",
        "test/Comms/ReplaySeekApplierTest.cc",
        "test/Comms/SyntheticTlog.h",
        "test/Comms/SyntheticTlog.cc",
        "test/Comms/CMakeLists.txt",
        "test/FactSystem/ParameterManagerTest.h",
        "test/FactSystem/ParameterManagerTest.cc",
    ),
)

GSTREAMER_RPATH = PRSpec(
    branch="upstream-pr-gstreamer-rpath",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    commit_subject="fix(GStreamer): stop shadowing Qt's FFmpeg libraries",
    # Independent of the other two specs — a stock-QGC defect fix (routing rule 1), sharing no
    # file with them, so it is a sibling of replay-fidelity rather than stacked on it. All three
    # paths are unmodified-upstream on mainline apart from this fix, which is what makes a
    # wholesale take sound here.
    include_paths=(
        "cmake/GStreamer/Link.cmake",
        "cmake/find-modules/FindGStreamer.cmake",
        "src/VideoManager/VideoReceiver/GStreamer/CMakeLists.txt",
    ),
)

# The three specs below replace hand-authored branches of the same name that predated this
# harness. Each is a routing-rule-1 fix — it improves stock QGC with no plugin loaded — and
# each shares no file with any other spec, so all three are independent siblings rather than
# part of the plugin-sdk stack. Deriving them force-updates the old hand-made branches, which
# is the point: a hand-maintained PR branch drifts silently (all three sat 103 commits behind
# upstream with no spec to measure them against).

MEDIA_BACKEND = PRSpec(
    branch="upstream-pr-media-backend",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    commit_subject="fix(Platform): use FFmpeg media backend with HW decode off",
    include_paths=("src/Utilities/Platform/Platform.cc",),
)

INITIAL_CONNECT_SKIP = PRSpec(
    branch="upstream-pr-initial-connect-skip",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    commit_subject="fix(Vehicle): skip standard-modes request on replay links",
    include_paths=(
        "src/Vehicle/InitialConnectStateMachine.h",
        "src/Vehicle/InitialConnectStateMachine.cc",
    ),
)

MOCKLINK_BYTESSENT = PRSpec(
    branch="upstream-pr-mocklink-bytessent",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    commit_subject="feat(MockLink): emit bytesSent for parity with real links",
    include_paths=("src/Comms/MockLink/MockLink.cc",),
)

SPECS: dict[str, PRSpec] = {
    "plugin-sdk": PLUGIN_SDK,
    "replay-fidelity": REPLAY_FIDELITY,
    "gstreamer-rpath": GSTREAMER_RPATH,
    "media-backend": MEDIA_BACKEND,
    "initial-connect-skip": INITIAL_CONNECT_SKIP,
    "mocklink-bytessent": MOCKLINK_BYTESSENT,
}


def _run(repo_root: Path, *args: str) -> None:
    log_step(" ".join(args))
    result = run_git(*args, cwd=repo_root)
    if result.returncode != 0:
        log_error(result.stderr.strip() or result.stdout.strip())
        raise SystemExit(1)


def _commit_in_groups(spec: PRSpec, repo_root: Path) -> None:
    """Commit the staged derivation as the spec's declared series instead of one commit.

    `git commit -- <paths>` commits only those paths from the index, so the groups peel the
    staged tree apart in declared order. __post_init__ has already proven the groups
    partition include_paths + patch_paths exactly, so nothing can be stranded — but a group
    can still be legitimately empty (a path whose mainline content matches the branch base),
    and an empty commit would be noise, so those are skipped.

    The residual sweep at the end is not a fallback for a bad partition; it catches the
    delete_paths, which are not part of the partition and are no-ops on a branch cut from
    upstream/master unless an include path dragged one in.
    """
    for subject, paths in spec.commit_groups:
        staged = run_git("diff", "--cached", "--name-only", "--", *paths, cwd=repo_root)
        if staged.returncode != 0:
            log_error(staged.stderr.strip())
            raise SystemExit(1)
        if not staged.stdout.strip():
            log_info(f"No content for commit group '{subject}' — skipping")
            continue
        _run(repo_root, "commit", "-m", subject, "--", *paths)

    residual = run_git("diff", "--cached", "--name-only", cwd=repo_root)
    if residual.returncode == 0 and residual.stdout.strip():
        _run(repo_root, "commit", "-m", "chore: drop fork-only paths")


def derive(spec: PRSpec, repo_root: Path) -> None:
    starting_branch = run_git("symbolic-ref", "--short", "HEAD", cwd=repo_root).stdout.strip()

    log_info(f"Deriving '{spec.branch}' from '{spec.source_ref}'")
    if starting_branch == spec.branch:
        # branch -f refuses to move a branch checked out in this worktree; step off it
        # first so re-derivation works without a manual `git checkout` in between runs.
        _run(repo_root, "checkout", spec.mainline_ref)
        starting_branch = spec.mainline_ref
    _run(repo_root, "branch", "-f", spec.branch, spec.source_ref)
    _run(repo_root, "checkout", spec.branch)

    for path in spec.include_paths:
        _run(repo_root, "checkout", spec.mainline_ref, "--", path)

    for path in spec.patch_paths:
        diff = run_git("diff", spec.source_ref, spec.mainline_ref, "--", path, cwd=repo_root)
        if diff.returncode != 0:
            log_error(diff.stderr.strip())
            raise SystemExit(1)
        if not diff.stdout.strip():
            log_info(f"No fork delta on '{path}', nothing to patch")
            continue
        apply_result = run_captured(
            ["git", "apply", "--whitespace=nowarn", "-"],
            cwd=repo_root,
            input_text=diff.stdout,
        )
        if apply_result.returncode != 0:
            log_error(f"Patch for '{path}' failed to apply: {apply_result.stderr.strip()}")
            raise SystemExit(1)
        _run(repo_root, "add", path)

    for path in spec.delete_paths:
        target = repo_root / path
        if not target.exists():
            continue
        _run(repo_root, "rm", "-rf", "--ignore-unmatch", path)

    for rel_path, rewrites in spec.doc_rewrites:
        file_path = repo_root / rel_path
        text = file_path.read_text(encoding="utf-8")
        for old, new in rewrites:
            if old not in text:
                log_error(f"Expected rewrite text not found in {rel_path}: {old!r}")
                raise SystemExit(1)
            text = text.replace(old, new)
        file_path.write_text(text, encoding="utf-8")
        _run(repo_root, "add", rel_path)

    if spec.commit_groups:
        _commit_in_groups(spec, repo_root)
    else:
        # No reference to this script by name: it is fork-only tooling absent from every
        # branch derive() produces, so a reviewer following the pointer finds nothing.
        _run(
            repo_root,
            "commit",
            "-m",
            f"{spec.commit_subject}\n\n"
            f"Regenerated wholesale from the fork mainline — do not hand-edit this branch; "
            f"re-derive it instead.",
        )
    log_ok(
        f"'{spec.branch}' derived at {run_git('rev-parse', 'HEAD', cwd=repo_root).stdout.strip()}"
    )

    # Leave the worktree back where it started so a later re-derivation of the same spec
    # doesn't hit "cannot force update the branch used by worktree".
    _run(repo_root, "checkout", starting_branch)


def _tracked_files(ref: str, path: str, repo_root: Path) -> list[str]:
    result = run_git("ls-tree", "-r", "--name-only", ref, "--", path, cwd=repo_root)
    return result.stdout.split() if result.returncode == 0 else []


def _blob(ref: str, path: str, repo_root: Path) -> str | None:
    result = run_git("rev-parse", f"{ref}:{path}", cwd=repo_root)
    return result.stdout.strip() if result.returncode == 0 else None


def check_source_reverts(spec: PRSpec, repo_root: Path) -> bool:
    """Report include paths where a wholesale checkout would revert upstream work.

    `derive()` takes each include path wholesale from mainline, which is sound only while
    mainline already carries everything upstream has for that path. The moment upstream
    moves ahead — it lands a fix the fork has not merged — the checkout silently *reverts*
    it. Nothing else catches this: reverting a fix still compiles and still passes the
    fork's own tests, so a green build is no evidence at all.

    Measured against `spec.upstream_ref`, never `source_ref` — see that field's docstring
    for why the distinction is load-bearing for a stacked spec.

    Non-fatal by default, since a fork mainline is routinely behind upstream mid-stream.
    It must read clean before the PRs are opened, which is what --submission-check gates.
    """
    reverted: list[tuple[str, str]] = []
    for include in spec.include_paths:
        for path in _tracked_files(spec.upstream_ref, include, repo_root):
            upstream_blob = _blob(spec.upstream_ref, path, repo_root)
            if upstream_blob is None or upstream_blob == _blob(spec.mainline_ref, path, repo_root):
                continue
            # Blobs differ — but that is only a revert if upstream carries commits for
            # this path that mainline lacks. (A file mainline simply moved ahead on is
            # fine; that is the derivation working as intended.)
            log = run_git(
                "log",
                "--oneline",
                f"{spec.mainline_ref}..{spec.upstream_ref}",
                "--",
                path,
                cwd=repo_root,
            )
            if log.returncode == 0 and log.stdout.strip():
                reverted.append((path, log.stdout.strip().splitlines()[0]))

    if reverted:
        log_error(
            f"{len(reverted)} include path(s) would REVERT commits reachable from "
            f"'{spec.upstream_ref}' that '{spec.mainline_ref}' does not carry. Merge them "
            f"into mainline and re-derive before opening the PR:"
        )
        for path, commit in reverted:
            log_error(f"    {path}  — drops {commit}")
        return False

    log_ok(f"no include path reverts '{spec.upstream_ref}'")
    return True


def check_seam_consumers(spec: PRSpec, repo_root: Path) -> bool:
    """Every base-app consumer of a seam token must ship in the same PR.

    The failure this prevents is a branch that builds and tests green while the feature it
    advertises is unreachable — the seam's readers left behind on mainline.
    """
    if not spec.seam_tokens:
        return True

    skip = spec.include_paths + spec.patch_paths + spec.delete_paths + spec.seam_exempt
    missing: list[tuple[str, str]] = []
    for token in spec.seam_tokens:
        grep = run_git("grep", "-lI", token, spec.mainline_ref, cwd=repo_root)
        # git grep: 1 == no matches (fine), anything else == the search never ran. Treating
        # those alike would let this check go silently blind and report a clean pass.
        if grep.returncode == 1:
            continue
        if grep.returncode != 0:
            log_error(f"git grep for '{token}' failed: {grep.stderr.strip()}")
            return False
        for line in grep.stdout.splitlines():
            path = line.split(":", 1)[1] if ":" in line else line
            if any(path == p or path.startswith(f"{p}/") for p in skip):
                continue
            missing.append((path, token))

    if missing:
        log_error(
            f"{len(missing)} base-app consumer(s) of a seam this PR ships are not in "
            f"include_paths — the seam would be inert on '{spec.branch}':"
        )
        for path, token in missing:
            log_error(f"    {path}  (reads {token})")
        return False

    log_ok("every seam consumer is included")
    return True


def verify(spec: PRSpec, repo_root: Path, submission_check: bool = False) -> bool:
    log_info(f"Verifying '{spec.branch}'")
    ok = True

    ls_tree = run_git("ls-tree", "-r", "--name-only", spec.branch, cwd=repo_root)
    tracked = ls_tree.stdout.splitlines()

    for path in spec.delete_paths:
        if any(f == path or f.startswith(f"{path}/") for f in tracked):
            log_error(f"'{path}' still tracked on '{spec.branch}'")
            ok = False

    # Scoped to the paths this derivation actually contributed — the whole branch also
    # carries unrelated pre-existing upstream content (e.g. upstream's own AGENTS.md),
    # which is not this check's concern.
    scanned_paths = spec.include_paths + spec.patch_paths
    for term in spec.forbidden_terms:
        grep = run_git("grep", "-liI", term, spec.branch, "--", *scanned_paths, cwd=repo_root)
        if grep.returncode == 0 and grep.stdout.strip():
            log_error(f"Forbidden term '{term}' found in: {grep.stdout.strip()}")
            ok = False

    if not check_seam_consumers(spec, repo_root):
        ok = False

    # Advisory unless --submission-check: mainline being behind upstream is a normal
    # mid-stream state, but it must be resolved before the branch is sent anywhere.
    if not check_source_reverts(spec, repo_root) and submission_check:
        ok = False

    if ok:
        log_ok(f"'{spec.branch}' passes structural checks")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr", choices=sorted(SPECS), help="Which PR spec to derive/verify")
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="Skip derivation, only verify the existing branch",
    )
    parser.add_argument(
        "--submission-check",
        action="store_true",
        help="Treat an upstream-revert report as fatal — run this before opening the PR",
    )
    args = parser.parse_args()

    spec = SPECS[args.pr]
    repo_root = find_repo_root(Path(__file__).parent)

    if not args.verify_only:
        derive(spec, repo_root)

    return 0 if verify(spec, repo_root, submission_check=args.submission_check) else 1


if __name__ == "__main__":
    sys.exit(main())
