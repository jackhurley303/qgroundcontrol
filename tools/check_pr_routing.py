#!/usr/bin/env python3
"""Audit PLUGIN_SDK routing coverage against mainline HEAD, read-only.

`tools/derive_pr_branch.py` only surfaces a routing gap when someone actually attempts
a derivation — by then several `git checkout`s have already run, and fixing the spec
means committing before the retry can even start (it switches branches, which a dirty
tree blocks). This script checks the same failure classes without touching the working
tree or switching branches at all, so it is safe to run anytime, including with an
uncommitted edit to `derive_pr_branch.py` itself sitting in the tree.

Four checks, each independently actionable:

  1. **Routing coverage** — base-app paths changed since `--since` (default: the last
     commit that touched `derive_pr_branch.py`) that no spec's `include_paths` or
     `patch_paths` covers. Excludes paths no longer present on mainline (already
     resolved) and `plugins/qdrive` (never routed, by design).
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

Not covered here: `check_source_reverts` (staleness vs `upstream/master`). That one is
inherently about the derived branch's base, is expected to be dirty mid-stream, and is
already gated at submission by `--submission-check`.

Exit 0 if all four are clean; exit 1 and print every finding otherwise. Advisory by
design — like `check_source_reverts`, this is a pre-flight, not a submission gate.

Usage:
    ./tools/check_pr_routing.py
    ./tools/check_pr_routing.py --since HEAD~50
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from _bootstrap import ensure_tools_dir

ensure_tools_dir(__file__)

from common.file_traversal import find_repo_root
from common.git import run_git
from common.logging import log_error, log_info, log_ok, log_step
from derive_pr_branch import SPECS, PRSpec, check_seam_consumers


def _default_since(repo_root: Path) -> str:
    log = run_git("log", "-1", "--format=%H", "--", "tools/derive_pr_branch.py", cwd=repo_root)
    if log.returncode != 0 or not log.stdout.strip():
        raise SystemExit("Could not find a commit touching tools/derive_pr_branch.py for --since")
    return log.stdout.strip()


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


def check_routing_coverage(mainline_ref: str, since: str, repo_root: Path) -> bool:
    log_step(f"routing coverage since {since}")
    diff = run_git("diff", "--name-only", since, mainline_ref, "--", ".", cwd=repo_root)
    if diff.returncode != 0:
        log_error(diff.stderr.strip())
        return False

    covered = _mainline_covered_paths()
    uncovered: list[str] = []
    for path in diff.stdout.splitlines():
        if not path or path.startswith("plugins/qdrive") or path in _ROUTING_EXEMPT:
            continue
        exists = run_git("cat-file", "-e", f"{mainline_ref}:{path}", cwd=repo_root)
        if exists.returncode != 0:
            continue  # deleted since `since` — nothing to route
        if _is_covered(path, covered):
            continue
        upstream_diff = run_git("diff", "upstream/master", mainline_ref, "--", path, cwd=repo_root)
        if upstream_diff.returncode == 0 and not upstream_diff.stdout.strip():
            continue  # converged back to upstream's content — nothing left to route
        uncovered.append(path)

    if uncovered:
        log_error(f"{len(uncovered)} changed path(s) not covered by any PRSpec:")
        for path in uncovered:
            log_error(f"    {path}")
        return False

    log_ok("every changed base-app path is routed")
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
    args = parser.parse_args()

    repo_root = find_repo_root(Path(__file__).parent)
    mainline_refs = {spec.mainline_ref for spec in SPECS.values()}
    if len(mainline_refs) != 1:
        raise SystemExit(f"PRSpecs disagree on mainline_ref: {mainline_refs}")
    mainline_ref = args.mainline or next(iter(mainline_refs))
    since = args.since or _default_since(repo_root)

    log_info(f"Auditing PR routing against '{mainline_ref}'")
    results = [
        check_routing_coverage(mainline_ref, since, repo_root),
        check_forbidden_terms(mainline_ref, repo_root),
        check_doc_rewrites(mainline_ref, repo_root),
        check_seams(repo_root),
    ]

    if all(results):
        log_ok("PR routing is clean")
        return 0
    log_error("PR routing has open gaps — see above")
    return 1


if __name__ == "__main__":
    sys.exit(main())
