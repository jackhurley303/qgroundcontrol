# Implementation Plan — How to Get There

Five phases, ordered so each ships standalone value, keeps QDrive working throughout, and maps onto a reviewable upstream PR series. Risk is front-loaded into spikes (phase 0). Phases 1–2 are the substance; 3–5 extend distribution and trust.

When execution starts, run it through the **`/architecture-change`** workflow (phases 2's SDK cutover reshapes the existing linkage model and deletes the old one); phase 1 alone could ship as a `/feature-change`.

## Phase 0 — Spikes (de-risk before committing the design)

| Spike | Question to answer | Done when |
|---|---|---|
| S1 — Metadata without load | `QPluginLoader::metaData()` returns the embedded JSON on all three desktop platforms without executing plugin code | A throwaway harness prints the example plugin's manifest pre-`instance()` on macOS + Linux + Windows CI |
| S2 — Windows SDK link | A plugin DLL linking only a trial `QGCPluginAPI.dll` import lib + Qt loads into a QGC build and its panel appears | Example plugin loads on Windows |
| S3 — Cross-build survival (Tier B claim) | Build plugin against SDK at commit A; run against host at commit B (with unrelated internal churn); exercise every extension point | No crash, all contributions work |
| S4 — Tier A package | A directory with only `qgcplugin.json` + QML loads as a plugin: panel + tool menu appear, QML uses the `QGroundControl` singleton | Works on desktop; then verify the same package loads from app storage on an Android build |
| S5 — Android plugin-APK (only before Phase 5) | Host can enumerate an installed marker-bearing APK, verify its signature, and `dlopen` a Qt plugin from its native-lib dir | Toy plugin APK loads into a QGC APK on-device |

S1/S2/S4 are days, not weeks. S3 is the honesty test for the whole Tier B promise — if it fails, the failure mode tells you which SDK rule was violated. S5 can wait.

## Phase 1 — Manifest + loader gate (cheap, ships alone, upstream-friendly)

Goal: the contract becomes declared; nothing about linkage changes yet.

1. Define the manifest schema (02 §4) and add `qgcplugin.json` to `example` and `qdrive` (`"tier": "internal"`, `hostBuildId` = git hash baked at build time).
2. Unify the IID: one constant in `QGCPluginInterface.h` (`org.qgroundcontrol.QGCPluginInterface/1.0`), used by `Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")` everywhere; fix example/README/QDrive (01 §4.1).
3. `QGCPluginLoader`: read `metaData()` first; validate IID, manifest presence, tier rules, host-version range; **only then** `instance()`. Store manifest fields in `PluginLoadInfo`. Key becomes manifest `id`, not runtime `name()`.
4. Disabled plugins: decide from metadata alone — never instantiate (fixes 01 §3.5's "disabled still executes" and makes the settings page independent of load success).
5. Settings page: show version/vendor/description from the manifest; show *why* an incompatible plugin won't load.
6. Loader hygiene from 01 §4: warn on second replay-extension registration; simplify `reloadPlugin()` (targeted load by stored path only — the scan-everything fallback goes away since discovery now reads metadata cheaply); reframe unload as "disable" (01 §4.6).
7. Docs: rewrite `plugins/README.md` + `src/PluginSystem/README.md` to match reality (manifest, tiers as roadmap, actual linkage).

Tests: manifest parsing/validation unit tests (malformed JSON, missing fields, version-range logic, tier gating) — the loader logic is pure enough to test without loading real libraries.

## Phase 2 — The SDK boundary (`QGCPluginAPI`)

Goal: Tier B exists; Windows works; `-export_dynamic` dies.

1. Create the `QGCPluginAPI` shared-library target containing `QGCPluginInterface`, `QGCPlugin` (d-pointer, no inline member access), `QGCReplayExtension`, `QGCHostServices`, and the first service interfaces. App links it; `Q_PLUGIN_METADATA` IID moves to `/2.0`; `apiVersion: 2`.
2. `QGCPlugin::init(QGCHostServices*)` replaces `init()`. Manager passes the host-services implementation at load.
3. Implement the initial services **by need, not speculation** — derive the list from what `example` + `qdrive` actually import from `src/` (01 §1 "Reference plugins"): replay-link control, telemetry-logging takeover (`controlsTelemetryLogging` moves from a virtual to a service the plugin acquires — removing one vtable landmine), plan/mission access, app paths/settings. Each service = one small interface header in the SDK + one wrapper impl in the app.
4. Convert `example` to pure Tier B: include path restricted to SDK + Qt, link `QGCPluginAPI`, drop `-undefined dynamic_lookup`. This is spike S2/S3 productized. Add a CI check that `plugins/example` includes nothing from `src/` outside `src/PluginAPI`.
5. Make `qgc_add_plugin()` the single canonical plugin CMake path (tier parameter: `QML`/`SDK`/`INTERNAL`), and use it in both plugins. Tier C keeps dynamic lookup (macOS/Linux only) + `hostBuildId` gate.
6. Remove `-Wl,-export_dynamic` from the executable once QDrive's remaining internals usage is inventoried — if QDrive still needs it, keep it fork-only and out of the upstream PR (upstream ships no Tier C plugin).
7. Publish the SDK as a build artifact (headers + libs + version pin) so plugin authors never build QGC — a `cmake --install` component plus a CI-produced zip per platform.

Tests: an SDK-API compile-time contract test (a tiny plugin in `test/` built against the SDK only), service wrapper unit tests, plus the existing extension-point QML paths exercised with the converted example plugin.

## Phase 3 — Package format + install UX

1. `.qgcplugin` zip layout (02 §6); loader learns to scan installed package directories (manifest at root, binary selected by platform key, QML resolved from the package's `qml/` dir instead of compiled-in `qrc`).
2. Tier A support falls out here: a package with no `bin/` skips `QPluginLoader` entirely — the manager synthesizes the contribution list from the manifest's `contributes` + QML URLs. Declare the QML API level (02 §5.4) and check it.
3. Plugins settings page: Install from file… / Remove / per-plugin details.
4. First-run consent + crash quarantine (02 §7.1–7.2) — small, high-credibility features for the upstream conversation.

## Phase 4 — Android, tiered

1. **v1 (with Phase 3):** Tier A packages installable at runtime on Android (they're pure data). Verify QML-from-file loading + app-storage paths on-device (spike S4's Android leg).
2. Keep compiled-in plugins as the Android Tier B/C mechanism; the enable/disable behavior already exists.
3. **v2 (own effort, after SDK stabilizes):** ATAK-style plugin APKs — package discovery via meta-data marker, signature verification policy, host/plugin Qt pinning, load native lib from the plugin APK (spike S5 first). Do not start this before Tier B has proven stable on desktop for a release cycle.

## Phase 5 — Hardening + upstream polish

- Signature-verification seam on desktop packages (02 §7.3), macOS notarization guidance.
- SDK ABI-stability CI: build yesterday's example-plugin binary artifact against today's host in CI and run a smoke test (automates spike S3 forever).
- API reference docs generated from the SDK headers; a "writing your first plugin" tutorial replacing the current template-copy instructions.

## Upstream PR series

Order matters: each PR must stand alone, be plugin-agnostic, and be reviewable in one sitting. Suggested slicing of the existing branch + new work:

| PR | Contents | Depends on |
|---|---|---|
| 1 | `PluginSystem` core: interface, base class, loader **with manifest gate from day one** (land Phase 1 folded in — don't upstream the gate-less loader and then fix it), manager, `PluginSettings` + settings page, example plugin (Tier B form), docs | — |
| 2 | `QGCPluginAPI` SDK library + host services + build helpers + SDK packaging | 1 |
| 3 | Fly-view panel extension point (strip + floating panel + pop-out state) | 1 |
| 4 | Plan-view panel extension point | 3 |
| 5 | Replay extension interface + the plugin-agnostic log-replay hooks (plan loading via registry, param seek resolution, open-tlog/params from main window) — the replay hooks are independently valuable upstream even without plugins | 1 (interface part) |
| 6 | Telemetry-logging takeover service | 2, 5 |
| 7 | Package format + install UX + consent/quarantine | 1–2 |
| 8 | Android Tier A; later, plugin-APK | 7 |

Notes for the upstream conversation:
- Lead PR 1's description with the **manifest + versioned-interface story** — reviewers' first questions will be ABI stability and security; the manifest gate and consent model are the answers.
- Do **not** include `-export_dynamic`/dynamic-lookup in any upstream PR (it exists only to serve Tier C, which upstream doesn't need). This keeps the most objectionable mechanism out of review entirely.
- The video-backend commits on the branch (FFmpeg/native backend forcing) are unrelated to plugins — keep them out of the series.

## QDrive migration (fork-side, parallel track)

1. Phase 1: add manifest (`tier: "internal"`) — QDrive keeps working unchanged, now honestly gated by `hostBuildId`.
2. Phase 2: adopt `init(QGCHostServices*)`; move replay-link, tlog-logging, and plan-controller access onto services as each seam lands. Track remaining `src/` includes as a burn-down list (the grep in 01 §1 is the baseline).
3. When the list hits zero, flip the manifest to `tier: "sdk"` — QDrive becomes distributable per the Tier B contract, which is the original goal for it.

## Risks

| Risk | Mitigation |
|---|---|
| SDK surface grows until it's "internals with extra steps" | Services added only against a concrete plugin need; every addition names the requesting use case; append-only review rule |
| Tier B promise broken by accidental ABI change in SDK | d-pointer/no-inline rules + the Phase 5 cross-build CI test |
| Upstream rejects the whole direction | PR 5's replay hooks and PR 1's settings page have standalone value; the series is sliced so partial acceptance still helps; engage maintainers with the tier/manifest design *before* PR 2 |
| Qt version skew between plugin author and host release | SDK artifact pins the Qt kit; manifest could later grow a `qt` field; rely on Qt's forward binary compatibility within Qt 6 |
| Android plugin-APK complexity blows up | It's isolated in Phase 4 v2 behind spike S5; Tier A covers Android distribution meanwhile |
