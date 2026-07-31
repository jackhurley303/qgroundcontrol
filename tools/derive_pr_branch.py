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
import sys
from dataclasses import dataclass, field
from pathlib import Path

from _bootstrap import ensure_tools_dir

ensure_tools_dir(__file__)

from common.file_traversal import find_repo_root
from common.git import run_git
from common.logging import log_error, log_info, log_ok, log_step
from common.proc import run_captured


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


PLUGIN_SDK = PRSpec(
    branch="upstream-pr-plugin-sdk",
    source_ref="upstream/master",
    mainline_ref="plugin-infrastructure-with-qdrive",
    include_paths=(
        "src/PluginAPI",
        "src/PluginSystem",
        "test/PluginSystem",
        "test/UnitTestFramework/BaseClasses/TempDirectoryTest.h",
        "test/UnitTestFramework/BaseClasses/CMakeLists.txt",
        "plugins/example",
        "plugins/template",
        "plugins/README.md",
        "plugins/CMakeLists.txt",
        "cmake/modules/PluginHelpers.cmake",
        "cmake/install/InstallPluginSDK.cmake",
        "cmake/install/QGCPluginAPIConfig.cmake.in",
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
        "src/FlyView/FlyViewPluginPanel.qml",
        "src/FlyView/FlyViewPluginButtonStrip.qml",
        "src/FlyView/FlightDisplayViewReplayVideo.qml",
        "src/FlyView/CMakeLists.txt",
        "src/PlanView/PlanViewPluginPanel.qml",
        "src/PlanView/PlanViewPluginButtonStrip.qml",
        "src/PlanView/PlanViewActionContext.h",
        "src/PlanView/PlanViewActionContext.cc",
        "src/PlanView/CMakeLists.txt",
        "src/AppSettings/PluginSettings.qml",
        "src/AppSettings/CMakeLists.txt",
        "src/Settings/PluginSettings.h",
        "src/Settings/PluginSettings.cc",
        "src/Settings/CMakeLists.txt",
        "src/Comms/CMakeLists.txt",
        "src/MAVLink/CMakeLists.txt",
        "CMakeLists.txt",
        "src/CMakeLists.txt",
        "src/qgc_version.h.in",
        "test/CMakeLists.txt",
    ),
    patch_paths=(".github/workflows/macos.yml",),
    delete_paths=("plugins/qdrive", ".gitmodules"),
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
                    "gated by matching `hostBuildId` (rebuilds together with the host). `qdrive` is this tier.",
                    "gated by matching `hostBuildId` (rebuilds together with the host).",
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

SPECS: dict[str, PRSpec] = {
    "plugin-sdk": PLUGIN_SDK,
    "replay-fidelity": REPLAY_FIDELITY,
}


def _run(repo_root: Path, *args: str) -> None:
    log_step(" ".join(args))
    result = run_git(*args, cwd=repo_root)
    if result.returncode != 0:
        log_error(result.stderr.strip() or result.stdout.strip())
        raise SystemExit(1)


def derive(spec: PRSpec, repo_root: Path) -> None:
    starting_branch = run_git(
        "symbolic-ref", "--short", "HEAD", cwd=repo_root
    ).stdout.strip()

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

    _run(
        repo_root,
        "commit",
        "-m",
        f"Derive {spec.branch} from {spec.source_ref}\n\n"
        f"Generated by tools/derive_pr_branch.py — do not hand-edit; re-run the script "
        f"against the current mainline instead.",
    )
    log_ok(f"'{spec.branch}' derived at {run_git('rev-parse', 'HEAD', cwd=repo_root).stdout.strip()}")

    # Leave the worktree back where it started so a later re-derivation of the same spec
    # doesn't hit "cannot force update the branch used by worktree".
    _run(repo_root, "checkout", starting_branch)


def verify(spec: PRSpec, repo_root: Path) -> bool:
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

    if ok:
        log_ok(f"'{spec.branch}' passes structural checks")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr", choices=sorted(SPECS), help="Which PR spec to derive/verify")
    parser.add_argument(
        "--verify-only", action="store_true", help="Skip derivation, only verify the existing branch"
    )
    args = parser.parse_args()

    spec = SPECS[args.pr]
    repo_root = find_repo_root(Path(__file__).parent)

    if not args.verify_only:
        derive(spec, repo_root)

    return 0 if verify(spec, repo_root) else 1


if __name__ == "__main__":
    sys.exit(main())
