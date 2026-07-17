# macOS Plugin SDK — Completion Plan (Stages 3R–5 + QDrive burn-down)

**Date:** 2026-07-16
**Profile:** `/architecture-change` (carried over from Stages 2–3 — clean-cutover: QDrive's internal-tier coupling is deleted by the end, not kept beside the SDK path).
**Supersedes:** the *remaining-work* portions of [04-macos-implementation-plan.md](04-macos-implementation-plan.md) (its §6 U3.3–U3.5, §7, §8). 04 stays the as-built record for Stages 1–3.2, the locked decisions D1–D11, the platform mechanics (§1), and the spike ledger (§11) — units below cite it rather than duplicating it. Per-unit history for everything already shipped: [../.feature/macos-plugin-sdk.md](../.feature/macos-plugin-sdk.md).

## Status — current position / next step

**Shipped before this plan:** Stage 1 (manifest + loader gate, U1.1–U1.6), Stage 2 (SDK boundary, U2.1–U2.7 + ABI hardening), Stage 3's U3.1 (package discovery + Tier A) and U3.2 (install/remove UX). Definition-of-done #2 (Tier A runtime install) is proven; #6 (`export_dynamic` gated) is done.

**Next:** fresh chat → `/implement-unit plugins/.architecture/05-completion-plan.md U3.4` — recommended: **Fable / medium / thinking on** while the 2026-07-19 access window lasts (rides along after U3.3's state precedence), else **Opus / medium / thinking on**.

**⚠ Fable access window (through ~2026-07-19):** while it lasts, front-load the two pieces that genuinely benefit: (1) the [qdrive plan](../qdrive/docs/sdk-burndown-migration.md)'s **R1 seek-apply spike** — order-free, pull it forward now (it settles the last permanently-frozen ABI decision of the change); (2) run **U3.4** on **Fable** per its brief. After the window, its fallback settings apply and this note is dead — delete it.

- [x] **U3.6** — Stage 3 hardening residue (installer validation + loader dedup) — shipped 2026-07-17
- [x] **U3.3** — Consent model (D10) + widened quarantine gate — shipped 2026-07-17
- [ ] **U3.4** — Crash sentinel
- [ ] **U3.5** — Release signing carries the plugin entitlement (D9)
- [ ] **U5.1** — Golden-plugin CI (ABI watchdog)
- [ ] **Stage 4** — QDrive burn-down Q1–Q6 — own plan + ledger in the qdrive repo: [../qdrive/docs/sdk-burndown-migration.md](../qdrive/docs/sdk-burndown-migration.md)
- [ ] **U5.2** — SDK docs + change close-out

This repo's plan has no standalone spikes to ledger: R2 rides U3.5's verify and R3 was decided inside U3.3 as-built (consent digest = manifest version + SHA-256 over manifest + resolved binary only; QML/asset trees are deliberately excluded — a residual gap, not an oversight, flagged in `consentDigest`'s comment). Stage 4's R1 spike is an own-chat task ledgered in the qdrive plan. Host-side companion units the qdrive plan requests get their own rows here when added.

## Goal & summary

Finish the macOS plugin SDK: the four remaining definition-of-done items from 04 —

- **#3** a hardened-runtime signed QGC loads differently-signed plugins (U3.5),
- **#4**'s consent half: user-dir plugins never execute code before explicit approval (U3.3, backed by U3.4's crash sentinel),
- **#5** QDrive burns down to pure Tier B and its `internal`-tier coupling is deleted (Q1–Q6, planned in the [qdrive repo's own plan](../qdrive/docs/sdk-burndown-migration.md) — the clean-cutover core of this change),
- **#1**'s residue: the different-commit-host proof becomes a permanent CI check (U5.1).

Plus the Stage 3 review residue this plan folds in as first-class work (U3.6, and the gate widening inside U3.3) — closing the gap between what 04 promised and what U3.1/U3.2 shipped, deliberately this time.

## Architecture / approach

The target architecture is [02-target-architecture.md](02-target-architecture.md); the platform mechanics and locked decisions D1–D11 are 04 §1–§2. **New locked decisions from the Stage 3 as-built + the 2026-07-16 design review** (numbering continues):

**D12 — Packages are `sdk`/`qml` only; tier `internal` is dev-loop/bare-dylib only.**
A package's sidecar manifest and its `bin/` binary are independent artifacts; nothing re-validates the binary against the sidecar that granted `hostBuildId` trust, so packaging tier `internal` would allow a post-gate binary swap. (As-built U3.1; U3.6 extends the same rejection to install time.)

**D13 — The sidecar `qgcplugin.json` is the sole identity source for packages.** `inspectPackage()` never reads a binary's embedded metadata; a lying sdk-tier binary still fails safely at `activate()` via the IID/`qobject_cast` check.

**D14 — Install over an existing id is a silent full replace.** Deliberate (04's U3.2 test line said "prompts"): the user just picked the file in a dialog — intent is unambiguous; matches `.vsix` convention. A version-aware confirm is post-ship polish (Open questions).

**D15 — Quarantine startup gate checks the manifest + the resolved package binary; approval-strip walks the full tree.** Manifest-only (U3.2 as-built) misses the realistic partial case — a fresh quarantined dylib swapped into a clean package — which Gatekeeper still blocks, but cryptically. Two `getxattr` calls cover the only file whose quarantine status matters (QML/assets are read as data, never Gatekeeper-gated). Widened in U3.3, where the gate is being reworked anyway.

**D16 — miniz is the zip implementation for both production extraction and test-fixture writing.** The libarchive path was built and reverted (QGC's vendored xz-utils is decoder-only; libarchive's zip *writer* needs the LZMA encoder even for plain zips) — rationale in [libs/miniz/README.md](../../libs/miniz/README.md); don't retread. One library with a tested zip-slip guard beats two zip codepaths with different security properties.

**Cutover discipline (the architecture-change end state):** when Q6 lands, QDrive's manifest is `tier: "sdk"`, its `-undefined dynamic_lookup` linkage and every `src/`-internal include are **gone** — not gated, gone from the qdrive tree. `QGC_ENABLE_INTERNAL_PLUGINS` (D7) stays in base QGC as generic Tier C infrastructure, but the fork no longer needs it ON for QDrive. Carrying internal-tier residue in qdrive past Q6 is failure.

## Repos & key anchors

**`qgroundcontrol` (this repo, branch `macos-plugin-sdk`)** — U3.x, U5.x:
- [src/PluginSystem/QGCPluginManager.cc](../../src/PluginSystem/QGCPluginManager.cc) — `_processInspected()` (the interim "Example disabled by default" rule at its center is what U3.3 deletes), `_applyQuarantineGate()` (the narrow gate U3.3 generalizes), `_activateIfEnabled()`, `approvePlugin()`.
- [src/PluginSystem/PluginInstaller.cc](../../src/PluginSystem/PluginInstaller.cc) — `installFromFile()` validates schema only today; U3.6 adds tier + bundled-runtime rejection between manifest parse and extraction. `isQuarantined()`/`stripQuarantine()` are U3.3's D15 anchors.
- [src/PluginSystem/QGCPluginLoader.cc](../../src/PluginSystem/QGCPluginLoader.cc) — `inspect()`/`inspectPackage()` share ~23 duplicated lines of gate flow (U3.6 dedup); `defaultPluginPaths()` ordering (bundle dirs first, user dir last) is the trust-class input for U3.3.
- [cmake/install/SignMacBundle.cmake](../../cmake/install/SignMacBundle.cmake) — final app codesign (line ~110) carries no `--entitlements`; [deploy/macos/qgroundcontrol.entitlements](../../deploy/macos/qgroundcontrol.entitlements) has the sandbox landmine — U3.5 adds a separate minimal file, never merges.
- [.github/workflows/macos.yml](../../.github/workflows/macos.yml) — SDK package/attest steps (~lines 163–182) are where U5.1's golden job slots.
- Tests to extend: `test/PluginSystem/` (`PluginInstallerTest`, `PluginLoaderGateTest`, `QGCPluginManagerTest`), fixture plugin `test/PluginSystem/TestPlugin/`.

**`plugins/qdrive` (nested repo)** — Stage 4's Q1–Q6 land there and are planned there: [plugins/qdrive/docs/sdk-burndown-migration.md](../qdrive/docs/sdk-burndown-migration.md) (own Status ledger, risks, unit briefs; follows that repo's workflow). This plan contains **no qdrive units** — one plan per repo, mutually referencing. The two touchpoints back into this repo: gitlink bumps ride each qdrive commit, and if Q3's R1 spike (defined in that plan) lands on the host-side seek-apply hoist — or Q4 demands a new seam — the host-side work is added **here** as a companion unit that the qdrive unit then depends on.

## Risks & spikes

All five original spikes passed (04 §11). Remaining genuinely-uncertain mechanics:

| # | Risk | Handling |
|---|---|---|
| R2 | S6m's unmeasured cell: same-team plugin under a **no-entitlement** host (needs real Developer ID certs). | Folded into U3.5's verify. Doesn't change the design either way — the entitlement ships regardless; this only calibrates the docs. |
| R3 | Consent-hash cost/scope (U3.3): hashing a large package tree at every startup. | Decided in-unit, default = hash manifest + resolved binary only (same file set as D15, same rationale). Not a spike — a bounded decision. |

(Stage 4's risk — R1, the Q3 seek-apply hoist — lives with its units in the [qdrive plan](../qdrive/docs/sdk-burndown-migration.md); its host-side fallout lands here as a companion unit if the spike says so.)

Everything else below is work, not risk.

## Units

### U3.6 — Stage 3 hardening residue
- **Scope:** (a) `installFromFile()` gains install-time validation between manifest parse and extraction: reject tier `internal` (D12, matching `inspectPackage()`'s rejection but before anything hits disk) and reject archives containing a bundled runtime — any `libQGCPluginAPI*.dylib` or `Qt*` framework/dylib entry (04 §9's promised mitigation; scan entry names against the already-open zip). (b) Extract the shared helper for `inspect()`/`inspectPackage()`'s duplicated id-check → `validateForHost` → contributions → logging flow (U3.1 review deferral, landing before U3.3 builds nearby).
- **Not in scope:** consent/quarantine changes (U3.3); id-collision prompt (declined, D14); any behavior change to the gate flow the dedup extracts.
- **Files:** [PluginInstaller.cc](../../src/PluginSystem/PluginInstaller.cc) (after the `fromJson` check, ~line 186), [QGCPluginLoader.cc](../../src/PluginSystem/QGCPluginLoader.cc) (helper mirrors the existing two call sites verbatim), `PluginInstallerTest` (+3: internal-tier zip rejected pre-extract with nothing written; bundled-SDK-dylib zip rejected; bundled-Qt zip rejected), `PluginLoaderGateTest` stays green unchanged (the dedup's no-behavior-change proof).
- **Depends on:** nothing.
- **Done means:** new tests green in the same commit; full Unit suite green (minus the 3 known keychain-timeout tests); `code-reviewer` (sonnet) clean.
- **Run settings:** Sonnet / medium / thinking on.

### U3.3 — Consent model (D10) + widened quarantine gate (D15)
- **Scope:** source-dir ⇒ trust class in `_processInspected()`: bundle dirs (`Contents/PlugIns`, exe-adjacent `plugins/` — the non-user entries of `defaultPluginPaths()`) = trusted, default-enabled; user dir = `NeedsApproval` on first sight, remembered as id+version+content hash in `PluginSettings` (hash scope per R3: manifest + resolved binary); changed hash re-prompts. Deletes the interim "Example disabled by default" rule ([QGCPluginManager.cc:267-270](../../src/PluginSystem/QGCPluginManager.cc#L267-L270)). Reworks `_applyQuarantineGate` into this flow and widens it per D15 (manifest + resolved binary `getxattr`). `approvePlugin()` = record consent + strip quarantine + activate — one approval flow, not two.
- **Not in scope:** crash sentinel (U3.4); any installer change.
- **Files:** `QGCPluginManager.cc/.h`, `PluginSettings.h/.cc` (consent storage beside the enabled Facts), [PluginSettings.qml](../../src/UI/AppSettings/PluginSettings.qml) ("New — not yet enabled [Enable]" row reuses U3.2's NeedsApproval UI), `QGCPluginManagerTest`.
- **Depends on:** U3.6 (adjacent code, cleaner base).
- **Done means:** manager tests prove a new user-dir plugin never activates pre-approval, approval persists across restarts, changed hash re-prompts, and a clean-manifest/quarantined-binary package is gated (the D15 case); bundle-dir plugins unaffected; `code-reviewer` (fable while the access window lasts, else opus) clean.
- **Run settings:** **Fable** / high / thinking on while the access window lasts (see Status — this is the security-critical trust surface); Opus / high / thinking on after.

### U3.4 — Crash sentinel
- **Scope:** per 04 §6 U3.4 verbatim: persist `PluginSystem/loadingPluginId` (QSettings, synced) before each `activate()`, clear after the load loop; a lingering id at startup ⇒ `Quarantined` + settings-page banner; re-enable clears. Define precedence explicitly: sentinel-quarantine outranks `NeedsApproval` (a plugin that crashed the host must not be re-runnable by mere consent).
- **Not in scope:** any bisect/auto-disable beyond the single-id sentinel.
- **Files:** `QGCPluginManager.cc` (`_activateRecord`/`_loadPlugins`), `PluginSettings.qml` (banner), `QGCPluginManagerTest` (lingering key ⇒ Quarantined, not activated; re-enable clears).
- **Depends on:** U3.3 (state precedence interplay).
- **Done means:** unit tests green; manual verify: `qFatal` in TestPluginFixture's constructor ⇒ next boot quarantines it with the banner; `code-reviewer` (opus) clean.
- **Run settings:** Fable / medium / thinking on if still inside the access window (rides along after U3.3); Opus / medium / thinking on otherwise (small diff, but trust-state semantics).

### U3.5 — Release signing carries the plugin entitlement (D9)
- **Scope:** new `deploy/macos/qgroundcontrol-release.entitlements` containing **only** `com.apple.security.cs.disable-library-validation`; the final app-bundle codesign in [SignMacBundle.cmake](../../cmake/install/SignMacBundle.cmake) (~line 110) gains `--entitlements` (that invocation only — dylib signatures don't carry entitlements); comment states why and points at 04 §11's S6m matrix. The sandbox-bearing Xcode-path file is left untouched, flagged in a comment. `plugins/README.md` gains the plugin-author signing guide (ad-hoc = dev; Developer ID + optional notarize = distribution; universal-build advice per 04 §1.5).
- **Not in scope:** the sandbox question; notarization flow changes.
- **Files:** `SignMacBundle.cmake`, `deploy/macos/qgroundcontrol-release.entitlements` (new), `plugins/README.md`.
- **Depends on:** nothing (can run any time).
- **Done means:** re-run the S6m matrix against a self-signed hardened-runtime bundle of the real app — differently-signed plugin loads *with* the entitlement, blocked *without* (definition-of-done #3); measure R2's cell if Developer ID certs are on hand; `code-reviewer` (sonnet) clean.
- **Run settings:** Sonnet / medium / thinking on (CMake + docs; the verify is manual signing work).

### U5.1 — Golden-plugin CI (the ABI watchdog)
- **Scope:** archive the built Example plugin from the next Release CI run as a versioned "golden" artifact; new `macos.yml` job downloads it and runs `PluginLoaderGateTest --golden <path>` against the current host (S3 automated forever — any red = an ABI rule broke; fix by reverting the break, never by rebuilding the golden). Second leg: CI builds `plugins/template/` against the *published* SDK zip and loads that too — the permanent different-commit-host proof (definition-of-done #1's residue from U2.7).
- **Not in scope:** Windows/Android CI; doxygen (U5.2).
- **Files:** `.github/workflows/macos.yml` (after the SDK steps, ~lines 163–182), `test/PluginSystem/PluginLoaderGateTest.cc` (`--golden` path argument), possibly a small composite-action reuse per AGENTS.md CI conventions.
- **Depends on:** U3.6 (loader gate flow settles before the harness pins it). Deliberately **before** Stage 4: the watchdog should be live while QDrive churn tempts ABI edits.
- **Done means:** CI job green on an unmodified host; a deliberate local negative control (add a virtual to `QGCPlugin` in a scratch build) makes the gate test fail loudly; `code-reviewer` (sonnet) clean.
- **Run settings:** Sonnet / medium / thinking on.

### Stage 4 — QDrive burn-down (cross-repo reference, no units here)
Q1–Q6 land in the `plugins/qdrive` nested repo and are planned, briefed, and ticked in **[plugins/qdrive/docs/sdk-burndown-migration.md](../qdrive/docs/sdk-burndown-migration.md)** — one plan per repo. What this plan owns about Stage 4: the gitlink bumps riding each qdrive commit, and any host-side companion units the qdrive plan's Q3 (R1 seek-apply hoist / `qgc.replay/2` fallback) or Q4 (possible mission-preview seam) turn out to need — those get added here as normal units when the qdrive plan asks for them, and the qdrive unit depends on them by reference.

### U5.2 — SDK docs + change close-out
- **Scope:** doxygen group for `src/PluginAPI/`; "Writing your first plugin (macOS)" tutorial (SDK zip + `plugins/template/` from U2.7); 04 §8's defect-ledger check (all closed); walk the full definition-of-done 1–6 and record the evidence; resolve Open questions below (notably qmlApiVersion); mark this doc COMPLETE; compress the memory entry to outcome + gotchas + pointer; then `/lc-branch-cleanup --onto plugin-infrastructure-with-qdrive`.
- **Not in scope:** upstream PR submission (its own effort, tracked in [[project_plugin_infrastructure]] / 03's PR map).
- **Files:** docs only + this doc + memory.
- **Depends on:** U3.6–U5.1 here, **plus the [qdrive plan](../qdrive/docs/sdk-burndown-migration.md) complete through Q6** (its ledger, not this one, is the authority on that).
- **Done means:** DoD 1–6 each have recorded evidence; docs build; `code-reviewer` (haiku) clean on the docs diff.
- **Run settings:** Sonnet / low / thinking off.

## Execution order

**U3.6 → U3.3 → U3.4 → U3.5 → U5.1**, then **Stage 4 runs in the [qdrive plan](../qdrive/docs/sdk-burndown-migration.md)** (Q1–Q6, strictly sequential there), then **U5.2**.

Constraints: U3.3 needs U3.6; U3.4 needs U3.3; U5.1 deliberately precedes Stage 4 (ABI watchdog live before QDrive churn). U3.5 is order-free — slot it anywhere if a release build is needed sooner. Host-side companion units requested by the qdrive plan (its Q3/Q4) slot between U5.1 and U5.2 as they arise.

## Open questions

| Question | Decide by |
|---|---|
| `qmlApiVersion`: flip from optional to **required** for tier `qml`? Free while zero external Tier A packages exist; breaking after. Current lean: keep optional (consistent with the manifest's permissive posture), flip only if external adoption starts. | U5.2 close-out, or immediately if any external Tier A package appears |
| Version-aware confirm on id-collision install (D14 keeps silent replace)? | Post-ship polish; revisit with the first real second user |
| Windows port tripwires (carried from 04 §10, plus two new: `isSafeEntryName()` splits on `/` only — add a `\` guard before any Windows zip extraction; `$<TARGET_FILE:TestPluginFixture>` backslash-escaping in compile definitions) | Windows port, not this change |

(Stage 4's open questions — the Q3 hoist and Q4 seam decisions — live in the [qdrive plan](../qdrive/docs/sdk-burndown-migration.md).)
