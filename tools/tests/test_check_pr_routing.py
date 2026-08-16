#!/usr/bin/env python3
"""Tests for check_pr_routing.py — the read-only PR routing pre-flight."""

from __future__ import annotations

import dataclasses
import subprocess

import pytest
from check_pr_routing import (
    _DEFERRED_ROUTING,
    _MAINLINE_ONLY,
    _ROUTING_EXEMPT,
    _is_covered,
    _mainline_covered_paths,
    apply_doc_rewrites,
    rewrites_for,
)
from derive_pr_branch import (
    CONVENTIONAL_SUBJECT,
    MAX_SUBJECT_LENGTH,
    SPECS,
    PRSpec,
    _commit_in_groups,
)


class TestIsCovered:
    def test_exact_path_is_covered(self):
        assert _is_covered("src/Vehicle/Vehicle.h", ("src/Vehicle/Vehicle.h",))

    def test_file_under_a_covered_directory_is_covered(self):
        assert _is_covered("src/PluginSystem/QGCPluginManager.cc", ("src/PluginSystem",))

    def test_unrelated_path_is_not_covered(self):
        assert not _is_covered("src/FlyView/FlyView.qml", ("src/PluginSystem",))

    def test_sibling_sharing_a_name_prefix_is_not_covered(self):
        """`src/PluginSystemExtra` must not be swallowed by `src/PluginSystem`.

        The guard is the trailing slash in the startswith; drop it and an unrouted
        sibling directory silently reports as covered.
        """
        assert not _is_covered("src/PluginSystemExtra/Thing.cc", ("src/PluginSystem",))

    def test_covered_directory_itself_is_covered(self):
        assert _is_covered("src/PluginSystem", ("src/PluginSystem",))


class TestCoveredPaths:
    def test_union_spans_every_spec(self):
        covered = _mainline_covered_paths()
        for spec in SPECS.values():
            for path in spec.include_paths + spec.patch_paths:
                assert path in covered

    def test_harness_itself_is_exempt_not_covered(self):
        """The derivation tooling is fork-only; no spec should ever carry it."""
        covered = _mainline_covered_paths()
        for path in _ROUTING_EXEMPT:
            assert path not in covered

    def test_this_test_file_is_seam_exempt(self):
        """It uses a real base-app path as fixture data, so it names a seam token.

        Without the exemption `check_seam_consumers` reads that mention as an unrouted
        base-app consumer — a false positive this file provably triggers.
        """
        assert "QGCPluginManager" in __import__("pathlib").Path(__file__).read_text()
        assert "tools/tests/test_check_pr_routing.py" in SPECS["plugin-sdk"].seam_exempt


class TestDocRewrites:
    REWRITES = (("names qdrive here", "names nothing here"), ("second qdrive", "second plugin"))

    def test_applies_every_rewrite_in_order(self):
        text = "line names qdrive here\nline second qdrive\n"
        assert (
            apply_doc_rewrites(text, self.REWRITES)
            == "line names nothing here\nline second plugin\n"
        )

    def test_no_rewrites_leaves_text_untouched(self):
        assert apply_doc_rewrites("unchanged", ()) == "unchanged"

    def test_rewrites_for_returns_empty_when_path_has_none(self):
        spec = next(iter(SPECS.values()))
        assert rewrites_for(spec, "path/that/has/no/rewrite.md") == ()

    def test_rewrites_for_finds_a_real_configured_path(self):
        spec = SPECS["plugin-sdk"]
        path, expected = spec.doc_rewrites[0]
        assert rewrites_for(spec, path) == expected


class TestSpecInvariants:
    def test_every_spec_shares_one_mainline_ref(self):
        """main() refuses to guess when specs disagree — keep them agreeing."""
        assert len({spec.mainline_ref for spec in SPECS.values()}) == 1

    def test_every_doc_rewrite_target_is_also_included(self):
        """Mirrors PRSpec.__post_init__ — a rewrite outside include_paths is skipped."""
        for spec in SPECS.values():
            for path, _ in spec.doc_rewrites:
                assert _is_covered(path, spec.include_paths)


class TestRegressionRoutingGap:
    def test_dropping_a_ci_invoked_script_leaves_it_uncovered(self):
        """The real 2026-08-15 defect: a script macos.yml calls, absent from the spec.

        Guards the specific hole that shipped a derived branch whose CI step invoked
        tools/check_plugin_ui_contract.py with no such file on the branch.
        """
        spec = SPECS["plugin-sdk"]
        assert _is_covered("tools/check_plugin_ui_contract.py", spec.include_paths)

        # commit_groups must be stripped in step with include_paths — PRSpec asserts the two
        # agree, which is the same discipline this test is describing: dropping a path means
        # dropping it from the commit that tells its story too.
        stripped = dataclasses.replace(
            spec,
            include_paths=tuple(
                p for p in spec.include_paths if "check_plugin_ui_contract" not in p
            ),
            commit_groups=tuple(
                (subject, tuple(p for p in paths if "check_plugin_ui_contract" not in p))
                for subject, paths in spec.commit_groups
            ),
        )
        assert not _is_covered("tools/check_plugin_ui_contract.py", stripped.include_paths)


class TestExemptionLists:
    def test_no_path_is_both_exempt_and_covered(self):
        """A path in two places has two answers; the audit would report the wrong one."""
        covered = _mainline_covered_paths()
        for path in _MAINLINE_ONLY + tuple(_DEFERRED_ROUTING):
            assert not _is_covered(path, covered), f"{path} is both exempt and routed"

    def test_exemption_lists_are_disjoint(self):
        assert not set(_ROUTING_EXEMPT) & set(_MAINLINE_ONLY)
        assert not set(_ROUTING_EXEMPT) & set(_DEFERRED_ROUTING)
        assert not set(_MAINLINE_ONLY) & set(_DEFERRED_ROUTING)

    def test_every_deferred_path_records_a_reason(self):
        """Deferred means 'decided, not yet specced' — without the why it is just a hole."""
        for path, reason in _DEFERRED_ROUTING.items():
            assert reason.strip(), f"{path} is deferred with no recorded reason"


class TestConventionalPattern:
    @pytest.mark.parametrize(
        "subject",
        [
            "feat(PluginSDK): add a thing",
            "fix: guard a null",
            "chore(tools): stack PR specs",
            "refactor(Comms)!: drop the old seam",
        ],
    )
    def test_matches_conventional_subjects(self, subject):
        assert CONVENTIONAL_SUBJECT.match(subject)

    @pytest.mark.parametrize(
        "subject",
        [
            "Add runtime plugin infrastructure",
            "Bump qdrive: archive enable/disable correctness plan",
            "feature: not a real type",
            "feat missing the colon",
        ],
    )
    def test_rejects_plain_subjects(self, subject):
        assert not CONVENTIONAL_SUBJECT.match(subject)

    def test_bump_prefix_is_not_mistaken_for_a_scope(self):
        """'Bump qdrive: ...' is the fork's commonest mainline subject and ends in a colon.

        A looser pattern (anything before a colon) reads it as conventional, so a spec
        could declare one and ship it to a PR branch that CONTRIBUTING then rejects.
        """
        assert not CONVENTIONAL_SUBJECT.match("Bump qdrive: QL1 teardown contract implemented")


class TestUpstreamRefAgreement:
    def test_every_spec_agrees_on_upstream_ref(self):
        """`check_style` picks one upstream_ref off the spec set; disagreement would make
        which one it gets depend on dict ordering."""
        assert len({spec.upstream_ref for spec in SPECS.values()}) == 1


class TestSubjectsAreUpstreamReady:
    """.github/CONTRIBUTING.md requires Conventional Commits on every commit in a PR.

    Everything the fork submits is derived from a spec, so these asserts are the whole of
    its compliance: they stand between a spec and a PR that breaches CONTRIBUTING on
    arrival. Mainline subjects are unconstrained because those commits are not in a PR —
    only their content is, re-committed under the spec's subject. The old `Derive <branch>
    from <ref>` subject shipped on every derived branch and satisfied no requirement
    upstream states.
    """

    def test_every_spec_emits_only_conventional_subjects(self):
        for name, spec in SPECS.items():
            subjects = (
                [spec.commit_subject] if spec.commit_subject else [s for s, _ in spec.commit_groups]
            )
            assert subjects, f"{name} declares no commit message at all"
            for subject in subjects:
                assert CONVENTIONAL_SUBJECT.match(subject), f"{name}: {subject!r}"

    def test_every_subject_fits_the_length_cap(self):
        for name, spec in SPECS.items():
            for subject in [spec.commit_subject, *(s for s, _ in spec.commit_groups)]:
                if subject:
                    assert len(subject) <= MAX_SUBJECT_LENGTH, f"{name}: {len(subject)}"

    def test_a_plain_subject_is_rejected(self):
        with pytest.raises(AssertionError, match="Conventional Commits"):
            dataclasses.replace(
                SPECS["gstreamer-rpath"], commit_subject="Stop shadowing Qt's FFmpeg"
            )

    def test_an_overlong_subject_is_rejected(self):
        with pytest.raises(AssertionError, match="over the"):
            dataclasses.replace(
                SPECS["gstreamer-rpath"], commit_subject="fix(GStreamer): " + "x" * 80
            )

    def test_declaring_neither_subject_nor_groups_is_rejected(self):
        with pytest.raises(AssertionError, match="exactly one"):
            dataclasses.replace(SPECS["gstreamer-rpath"], commit_subject="")

    def test_declaring_both_is_rejected(self):
        with pytest.raises(AssertionError, match="exactly one"):
            dataclasses.replace(SPECS["plugin-sdk"], commit_subject="feat: both")


class TestCommitGroups:
    def test_plugin_sdk_partitions_every_declared_path(self):
        spec = SPECS["plugin-sdk"]
        grouped = [p for _subject, paths in spec.commit_groups for p in paths]
        assert sorted(grouped) == sorted(set(spec.include_paths) | set(spec.patch_paths))

    def test_small_specs_stay_single_commit(self):
        """One synthetic commit is right for a 1-3 file spec; groups would be ceremony."""
        for name in ("gstreamer-rpath", "media-backend", "mocklink-bytessent"):
            assert SPECS[name].commit_groups == ()

    def test_omitting_a_path_from_every_group_is_rejected(self):
        spec = SPECS["plugin-sdk"]
        with pytest.raises(AssertionError, match="omit"):
            dataclasses.replace(spec, commit_groups=spec.commit_groups[:-1])

    def test_listing_a_path_twice_is_rejected(self):
        spec = SPECS["plugin-sdk"]
        first_subject, first_paths = spec.commit_groups[0]
        with pytest.raises(AssertionError, match="more than once"):
            dataclasses.replace(
                spec,
                commit_groups=(*spec.commit_groups, (f"{first_subject} (again)", first_paths)),
            )

    def test_grouping_an_undeclared_path_is_rejected(self):
        spec = SPECS["plugin-sdk"]
        with pytest.raises(AssertionError, match="absent from include_paths"):
            dataclasses.replace(
                spec,
                commit_groups=(
                    *spec.commit_groups,
                    ("chore: stray", ("src/Nonexistent/Thing.cc",)),
                ),
            )


class TestCommitInGroups:
    """Exercises the real git behaviour of `_commit_in_groups` in a scratch repo.

    `derive()` cannot be run against this repo while the spec edit is uncommitted (its
    internal checkout refuses to clobber the dirty tooling file), so without this the
    grouped-commit path would ship unexecuted.
    """

    # Subjects here must be Conventional Commits like any real spec's — PRSpec asserts it.
    S = "chore: "

    @staticmethod
    def _git(repo, *args):
        subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True)

    @pytest.fixture
    def repo(self, tmp_path):
        self._git(tmp_path, "init", "-q", "-b", "main")
        self._git(tmp_path, "config", "user.email", "t@example.com")
        self._git(tmp_path, "config", "user.name", "T")
        (tmp_path / "seed.txt").write_text("seed\n")
        self._git(tmp_path, "add", "seed.txt")
        self._git(tmp_path, "commit", "-qm", "seed")
        return tmp_path

    def _spec(self, groups, paths):
        return PRSpec(
            branch="upstream-pr-scratch",
            source_ref="main",
            mainline_ref="mainline",
            include_paths=paths,
            commit_groups=groups,
        )

    def test_each_group_becomes_its_own_commit_in_order(self, repo):
        for name in ("a.txt", "b.txt", "c.txt"):
            (repo / name).write_text(f"{name}\n")
        self._git(repo, "add", "a.txt", "b.txt", "c.txt")

        spec = self._spec(
            ((f"{self.S}first group", ("a.txt", "b.txt")), (f"{self.S}second group", ("c.txt",))),
            ("a.txt", "b.txt", "c.txt"),
        )
        _commit_in_groups(spec, repo)

        log = subprocess.run(
            ["git", "log", "--format=%s"], cwd=repo, capture_output=True, text=True, check=True
        )
        assert log.stdout.split("\n")[:3] == [
            f"{self.S}second group",
            f"{self.S}first group",
            "seed",
        ]

        files = subprocess.run(
            ["git", "show", "--name-only", "--format=", "HEAD~1"],
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        )
        assert sorted(files.stdout.split()) == ["a.txt", "b.txt"]

    def test_index_is_empty_afterwards(self, repo):
        (repo / "a.txt").write_text("a\n")
        self._git(repo, "add", "a.txt")
        _commit_in_groups(self._spec(((f"{self.S}only group", ("a.txt",)),), ("a.txt",)), repo)

        staged = subprocess.run(
            ["git", "diff", "--cached", "--name-only"],
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        )
        assert not staged.stdout.strip()

    def test_empty_group_is_skipped_not_committed(self, repo):
        """A path whose mainline content already matches the base stages nothing.

        Committing it anyway would put an empty commit in front of a reviewer; `git commit`
        would also fail outright rather than skip, so this is the difference between a
        derivation that completes and one that aborts partway.
        """
        (repo / "a.txt").write_text("a\n")
        self._git(repo, "add", "a.txt")

        spec = self._spec(
            ((f"{self.S}has content", ("a.txt",)), (f"{self.S}nothing staged", ("b.txt",))),
            ("a.txt", "b.txt"),
        )
        _commit_in_groups(spec, repo)

        log = subprocess.run(
            ["git", "log", "--format=%s"], cwd=repo, capture_output=True, text=True, check=True
        )
        assert log.stdout.split("\n")[:2] == [f"{self.S}has content", "seed"]


class TestRegressionInertRelease:
    def test_macos_entitlement_ships_with_the_loader(self):
        """The 2026-08-15 gap: PR 1 carried the plugin loader but not the entitlement.

        Under the hardened runtime dyld refuses to load any library not signed by the app's
        own Team ID, so without this pair the SDK ships and loads nothing on the one
        configuration users install. seam_tokens cannot see it — there is no symbol here.
        """
        spec = SPECS["plugin-sdk"]
        assert _is_covered("deploy/macos/qgroundcontrol-release.entitlements", spec.include_paths)
        assert _is_covered("cmake/install/SignMacBundle.cmake", spec.include_paths)

    def test_settings_page_ships_with_its_registration(self):
        """PluginSettings.qml is unreachable without the JSON entry naming it."""
        spec = SPECS["plugin-sdk"]
        assert _is_covered("src/AppSettings/PluginSettings.qml", spec.include_paths)
        assert _is_covered("src/AppSettings/pages/SettingsPages.json", spec.include_paths)
        assert _is_covered("resources/InstrumentValueIcons/plugins.svg", spec.include_paths)


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
