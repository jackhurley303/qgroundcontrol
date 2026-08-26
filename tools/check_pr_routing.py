#!/usr/bin/env python3
"""Audit PLUGIN_SDK routing coverage against mainline HEAD, read-only.

`tools/derive_pr_branch.py` only surfaces a routing gap when someone actually attempts
a derivation — by then several `git checkout`s have already run, and fixing the spec
means committing before the retry can even start (it switches branches, which a dirty
tree blocks). This script checks the same failure classes without touching the working
tree or switching branches at all, so it is safe to run anytime, including with an
uncommitted edit to `derive_pr_branch.py` itself sitting in the tree.

Five checks, all on by default:

  1. **Routing coverage** — base-app paths changed since `--since` (default: the fork's
     merge-base with `upstream/master`, i.e. every fork commit) that no spec's
     `include_paths` or `patch_paths` covers. Excludes paths no longer present on mainline
     (already resolved), `plugins/qdrive` (never routed, by design), and the two recorded
     exemption lists below.
  2. **Forbidden terms** — each spec's `forbidden_terms` grepped against mainline HEAD
     directly, scoped to that spec's own `include_paths` + `patch_paths`. Catches a
     plugin name leaking into a base-app comment the moment it lands, not whenever
     someone next derives.
  3. **doc_rewrites staleness** — each rewrite's exact `old` string checked against its
     target file's current mainline content. `doc_rewrites` has no independent
     staleness check in `derive()`; it only fails loudly mid-derivation.
  4. **Seam consumers** — delegated straight to `derive_pr_branch.check_seam_consumers`,
     which already greps `mainline_ref` and so needs no branch either. Guards the worst
     failure mode: a branch that compiles, tests green, and does nothing.
  5. **Style** — clang-format drift in routed files the fork authored, advisory only.

Not covered here: `check_source_reverts` (staleness vs `upstream/master`). That one is
inherently about the derived branch's base, is expected to be dirty mid-stream, and is
already gated at submission by `--submission-check`. Commit *subjects* are not checked
either: Conventional Commits binds every commit in a PR, and the commits this fork submits
are derived ones whose subjects come from each spec's `commit_subject`/`commit_groups`,
asserted at import in `derive_pr_branch`. A mainline commit is not in a PR — only its
content is, re-committed under the spec's subject — so mainline subjects are unconstrained.

Exit 0 if all five are clean; exit 1 and print every finding otherwise. Advisory by
design — like `check_source_reverts`, this is a pre-flight, not a submission gate.

Usage:
    ./tools/check_pr_routing.py
    ./tools/check_pr_routing.py --since HEAD~50
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from _bootstrap import ensure_tools_dir

ensure_tools_dir(__file__)

from common.file_traversal import find_repo_root
from common.git import run_git
from common.logging import log_error, log_info, log_ok, log_step
from common.proc import run_captured
from derive_pr_branch import SPECS, PRSpec, check_seam_consumers


def _default_since(mainline_ref: str, repo_root: Path) -> str:
    """The fork's merge-base with upstream — i.e. audit every fork commit, not a slice.

    This used to default to the last commit touching `derive_pr_branch.py`, which made the
    window as narrow as two commits and let a clean `[OK]` read as a clean bill of health
    for the whole fork. It wasn't: at the time that default was replaced, the wide window
    reported 20 unrouted base-app paths, including the macOS entitlement without which a
    signed release build cannot load a plugin at all.

    Narrow windows are still available via `--since HEAD~n` for a fast pre-commit loop.
    """
    base = run_git("merge-base", "upstream/master", mainline_ref, cwd=repo_root)
    if base.returncode != 0 or not base.stdout.strip():
        raise SystemExit(
            f"Could not compute merge-base of upstream/master and '{mainline_ref}' for --since"
        )
    return base.stdout.strip()


def _mainline_covered_paths() -> tuple[str, ...]:
    covered: set[str] = set()
    for spec in SPECS.values():
        covered.update(spec.include_paths)
        covered.update(spec.patch_paths)
    return tuple(covered)


def _is_covered(path: str, covered: tuple[str, ...]) -> bool:
    return any(path == c or path.startswith(f"{c}/") for c in covered)


# Deliberately never routed to any PRSpec. The first three are fork-only tooling with
# nothing upstream to receive them. ruff.toml is the one shared file here: it exists
# upstream, but its entire fork delta is the E402 per-file-ignores for the two scripts
# above, which are meaningless in a tree where those scripts don't exist.
# ⚠ Revisit ruff.toml's entry if a genuinely upstream-bound lint change ever lands in it —
# this exemption is scoped to the delta being fork-only, not to the file being fork-only.
_ROUTING_EXEMPT = (
    "tools/derive_pr_branch.py",
    "tools/check_pr_routing.py",
    "tools/tests/test_check_pr_routing.py",
    "ruff.toml",
)

# Routing rule 4 — mainline-only, permanently. Upstream has nothing to receive these: they
# exist only because this fork carries a plugin submodule and its build artifacts. Listing
# them is what makes a clean audit mean "every path was examined" rather than "the window
# was too narrow to reach them".
_MAINLINE_ONLY = (
    ".gitmodules",
    "plugins/.gitignore",
    "plugins/.gitkeep",
)

# Routed in principle, deliberately not specced yet. Unlike the two lists above these are
# open questions with a recorded answer, so they stay visible: the audit prints them every
# run and only stops counting them as failures.
_DEFERRED_ROUTING = {
    "qgcresources.qrc": (
        "rule 1 (fixes stock QGC: registers QGCLogoBlack.svg for custom-example's "
        "dangling reference) — held with the other 9 upstream dangling resource refs, "
        "which are a single PR's worth of work not yet specced"
    ),
}


def check_routing_coverage(mainline_ref: str, since: str, repo_root: Path) -> bool:
    log_step(f"routing coverage since {since}")
    diff = run_git("diff", "--name-only", since, mainline_ref, "--", ".", cwd=repo_root)
    if diff.returncode != 0:
        log_error(diff.stderr.strip())
        return False

    covered = _mainline_covered_paths()
    uncovered: list[str] = []
    deferred: list[str] = []
    for path in diff.stdout.splitlines():
        if not path or path.startswith("plugins/qdrive"):
            continue
        if path in _ROUTING_EXEMPT or path in _MAINLINE_ONLY:
            continue
        exists = run_git("cat-file", "-e", f"{mainline_ref}:{path}", cwd=repo_root)
        if exists.returncode != 0:
            continue  # deleted since `since` — nothing to route
        if _is_covered(path, covered):
            continue
        upstream_diff = run_git("diff", "upstream/master", mainline_ref, "--", path, cwd=repo_root)
        if upstream_diff.returncode == 0 and not upstream_diff.stdout.strip():
            continue  # converged back to upstream's content — nothing left to route
        if path in _DEFERRED_ROUTING:
            deferred.append(path)
            continue
        uncovered.append(path)

    for path in deferred:
        log_info(f"deferred: {path} — {_DEFERRED_ROUTING[path]}")

    if uncovered:
        log_error(f"{len(uncovered)} changed path(s) not covered by any PRSpec:")
        for path in uncovered:
            log_error(f"    {path}")
        return False

    log_ok(f"every changed base-app path is routed ({len(deferred)} deferred by decision)")
    return True


def rewrites_for(spec: PRSpec, path: str) -> tuple[tuple[str, str], ...]:
    """This spec's doc_rewrites for one path, or () if it has none."""
    for rel_path, rewrites in spec.doc_rewrites:
        if rel_path == path:
            return rewrites
    return ()


def apply_doc_rewrites(text: str, rewrites: tuple[tuple[str, str], ...]) -> str:
    """Apply one path's rewrites, mirroring derive()'s str.replace loop exactly."""
    for old, new in rewrites:
        text = text.replace(old, new)
    return text


def check_forbidden_terms(mainline_ref: str, repo_root: Path) -> bool:
    """Mirrors derive()'s effective content: doc_rewrites applied, then scanned.

    A raw grep of mainline would false-positive on every doc_rewrites target — those
    files legitimately mention a plugin name on mainline and only lose it once derived.
    """
    log_step("forbidden terms on mainline HEAD (post-doc_rewrites)")
    ok = True
    for name, spec in SPECS.items():
        scanned = spec.include_paths + spec.patch_paths
        if not scanned:
            continue
        for term in spec.forbidden_terms:
            grep = run_git("grep", "-liI", term, mainline_ref, "--", *scanned, cwd=repo_root)
            if grep.returncode != 0 or not grep.stdout.strip():
                continue
            for line in grep.stdout.strip().splitlines():
                path = line.split(":", 1)[1] if ":" in line else line
                rewrites = rewrites_for(spec, path)
                if not rewrites:
                    ok = False
                    log_error(f"'{name}': forbidden term '{term}' found in: {path}")
                    continue
                text = run_git("show", f"{mainline_ref}:{path}", cwd=repo_root).stdout
                if term.lower() in apply_doc_rewrites(text, rewrites).lower():
                    ok = False
                    log_error(f"'{name}': forbidden term '{term}' survives doc_rewrites in: {path}")

    if ok:
        log_ok("no forbidden terms survive in any spec's covered paths")
    return ok


def check_doc_rewrites(mainline_ref: str, repo_root: Path) -> bool:
    log_step("doc_rewrites staleness")
    ok = True
    for name, spec in SPECS.items():
        for rel_path, rewrites in spec.doc_rewrites:
            show = run_git("show", f"{mainline_ref}:{rel_path}", cwd=repo_root)
            if show.returncode != 0:
                log_error(f"'{name}': cannot read '{rel_path}' at {mainline_ref}")
                ok = False
                continue
            text = show.stdout
            for old, _new in rewrites:
                if old not in text:
                    log_error(f"'{name}': stale doc_rewrite in '{rel_path}' — not found: {old!r}")
                    ok = False

    if ok:
        log_ok("every doc_rewrite still matches its target file")
    return ok


def _routed_files(mainline_ref: str, repo_root: Path) -> list[str]:
    paths = sorted({p for spec in SPECS.values() for p in spec.include_paths + spec.patch_paths})
    listing = run_git("ls-tree", "-r", "--name-only", mainline_ref, "--", *paths, cwd=repo_root)
    return listing.stdout.split() if listing.returncode == 0 else []


def check_style(mainline_ref: str, repo_root: Path) -> bool:
    """Report clang-format drift in routed files the fork *authored*, advisory only.

    Deliberately not a gate, and deliberately not run over every routed file. Two measured
    reasons (2026-08-15), both of which would reverse if upstream tightened:

      - Upstream's own CI (`.github/workflows/pre-commit.yml`) runs the hooks with
        `continue-on-error: true` and posts results as a PR comment; only a crash of the
        runner fails the job. Formatting is not a merge gate there.
      - Upstream does not hold itself to it either — of the 12 C++ files it added in its
        last 103 commits, 6 fail `.clang-format`.

    So reformatting is a judgement call, not an obligation, and mass-reformatting a legacy
    file the PR happens to touch would breach CONTRIBUTING's "No unrelated changes" — the
    reason this only counts files absent from `upstream_ref`. What it buys is that the
    number is *visible* before submission instead of arriving as a CI comment on the PR.
    """
    log_step("style of fork-authored routed files (advisory)")
    upstream_ref = next(iter({spec.upstream_ref for spec in SPECS.values()}))
    upstream_files = set(_tracked_at(upstream_ref, repo_root))
    candidates = [
        f
        for f in _routed_files(mainline_ref, repo_root)
        if f not in upstream_files and f.endswith((".cc", ".h", ".cpp"))
    ]
    if not candidates:
        log_ok("no fork-authored C++ in the routed set")
        return True

    formatter = _find_clang_format()
    if formatter is None:
        # Never report "clean" here: a missing binary produces the same silence as a pass.
        log_info(
            f"clang-format not found — {len(candidates)} fork-authored file(s) UNCHECKED. "
            f"Install it (pre-commit pins v22.1.5) to see this number."
        )
        return True

    result = run_captured(
        [formatter, "--dry-run", "-Werror", *candidates], cwd=repo_root, input_text=""
    )
    offenders = sorted(
        {
            line.split(":", 1)[0]
            for line in result.stderr.splitlines()
            if "code should be clang-formatted" in line
        }
    )
    if offenders:
        log_info(
            f"{len(offenders)} of {len(candidates)} fork-authored routed file(s) differ from "
            f".clang-format — advisory, see check_style's docstring before reformatting"
        )
    else:
        log_ok(f"all {len(candidates)} fork-authored routed files match .clang-format")
    return True


def _tracked_at(ref: str, repo_root: Path) -> list[str]:
    listing = run_git("ls-tree", "-r", "--name-only", ref, cwd=repo_root)
    return listing.stdout.split() if listing.returncode == 0 else []


def _find_clang_format() -> str | None:
    """Prefer the version pre-commit pins over whatever is on PATH.

    A different clang-format version reformats differently, so a PATH binary can report
    drift that CI would not, or miss drift CI would catch. CODING_STYLE.md calls this out.
    """
    cached = sorted(Path.home().glob(".cache/pre-commit/*/py_env-*/bin/clang-format"))
    if cached:
        return str(cached[0])
    return shutil.which("clang-format")


def check_seams(repo_root: Path) -> bool:
    """Reuse derive_pr_branch's own seam check — it already greps `mainline_ref`.

    `check_seam_consumers` never touches the derived branch, so it costs nothing to run
    here and it guards the worst failure mode of the three: a branch that compiles, passes
    every test, and does nothing because the code reading its seams stayed on mainline.
    """
    log_step("seam consumers (delegated to derive_pr_branch)")
    ok = True
    for name, spec in SPECS.items():
        if not spec.seam_tokens:
            continue
        if not check_seam_consumers(spec, repo_root):
            log_error(f"'{name}': seam consumers missing — see above")
            ok = False
    return ok


def check_worktree(paths: list[str], repo_root: Path) -> bool:
    """Audit uncommitted work, before it can reach mainline.

    Every check above reads a ref, so a breach is only visible once it is committed —
    which is exactly how a plugin name reached mainline inside the SDK verifier. This
    runs the two checks that make sense on an edit still in progress (is the path routed
    at all, does it carry a forbidden term) against the files on disk, so the answer
    arrives while the edit is cheap to fix.
    """
    log_step("worktree routing + forbidden terms")
    if not paths:
        status = run_git("status", "--porcelain", "--", ".", cwd=repo_root)
        if status.returncode != 0:
            log_error(status.stderr.strip())
            return False
        paths = [ln[3:].strip() for ln in status.stdout.splitlines() if ln[3:].strip()]

    covered = _mainline_covered_paths()
    ok = True
    checked = 0
    for path in paths:
        if not path or path.startswith("plugins/qdrive"):
            continue
        if path in _ROUTING_EXEMPT or path in _MAINLINE_ONLY or path in _DEFERRED_ROUTING:
            continue
        if not (repo_root / path).is_file():
            continue  # deleted, or a directory — nothing on disk to read
        checked += 1
        if not _is_covered(path, covered):
            log_error(f"{path} is covered by no PRSpec — route it, or record an exemption")
            ok = False
            continue
        text = (repo_root / path).read_text(encoding="utf-8", errors="replace")
        for name, spec in SPECS.items():
            if not _is_covered(path, spec.include_paths + spec.patch_paths):
                continue
            rewrites = rewrites_for(spec, path)
            body = apply_doc_rewrites(text, rewrites) if rewrites else text
            for term in spec.forbidden_terms:
                if term.lower() in body.lower():
                    log_error(f"'{name}': forbidden term '{term}' in worktree file: {path}")
                    ok = False

    if ok:
        log_ok(f"{checked} worktree path(s) routed, no forbidden terms")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--since",
        default=None,
        help="Ref to diff routing coverage from (default: last commit touching derive_pr_branch.py)",
    )
    parser.add_argument(
        "--mainline",
        default=None,
        help="Mainline ref to check (default: the mainline_ref shared by every PRSpec)",
    )
    parser.add_argument(
        "--worktree",
        action="store_true",
        help="Check uncommitted files on disk instead of mainline (routing + forbidden terms only)",
    )
    parser.add_argument(
        "--paths",
        nargs="*",
        default=None,
        help="With --worktree: limit the check to these paths (default: everything git reports dirty)",
    )
    args = parser.parse_args()

    repo_root = find_repo_root(Path(__file__).parent)
    if args.worktree:
        return 0 if check_worktree(args.paths or [], repo_root) else 1

    mainline_refs = {spec.mainline_ref for spec in SPECS.values()}
    if len(mainline_refs) != 1:
        raise SystemExit(f"PRSpecs disagree on mainline_ref: {mainline_refs}")
    mainline_ref = args.mainline or next(iter(mainline_refs))
    since = args.since or _default_since(mainline_ref, repo_root)

    log_info(f"Auditing PR routing against '{mainline_ref}'")
    results = [
        check_routing_coverage(mainline_ref, since, repo_root),
        check_forbidden_terms(mainline_ref, repo_root),
        check_doc_rewrites(mainline_ref, repo_root),
        check_seams(repo_root),
        check_style(mainline_ref, repo_root),
    ]

    if all(results):
        log_ok("PR routing is clean")
        return 0
    log_error("PR routing has open gaps — see above")
    return 1


if __name__ == "__main__":
    sys.exit(main())
