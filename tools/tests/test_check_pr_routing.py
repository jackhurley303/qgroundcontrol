#!/usr/bin/env python3
"""Tests for check_pr_routing.py — the read-only PR routing pre-flight."""

from __future__ import annotations

import dataclasses

import pytest
from check_pr_routing import (
    _ROUTING_EXEMPT,
    _is_covered,
    _mainline_covered_paths,
    apply_doc_rewrites,
    rewrites_for,
)
from derive_pr_branch import SPECS


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

        stripped = dataclasses.replace(
            spec,
            include_paths=tuple(
                p for p in spec.include_paths if "check_plugin_ui_contract" not in p
            ),
        )
        assert not _is_covered("tools/check_plugin_ui_contract.py", stripped.include_paths)


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
