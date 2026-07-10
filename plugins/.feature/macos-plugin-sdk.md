# macOS Plugin SDK — feature-change tracker

**Profile:** `/feature-change` (additive) for Stage 1; Stages 2–3 switch to `/architecture-change` per the blueprint's Execution notes (§11 of the plan doc).
**Blueprint:** [plugins/.architecture/04-macos-implementation-plan.md](../.architecture/04-macos-implementation-plan.md) — grounded design, locked decisions D1–D11, spikes S1/S2m/S3/S4/S6m all PASS (2026-07-06).

## Status

### Stage 1 — The declared contract (manifest + loader gate)

- **U1.1 — `PluginManifest` value type + validation + tests** — DONE (2026-07-09).
  - New: `src/PluginSystem/PluginManifest.h/.cc` — value type (`id, name, version, vendor, description, tier, apiVersion, hostVersionMin/Max, hostBuildId, contributes`), `fromJson()`, `validateForHost()`.
  - Wired into `src/PluginSystem/CMakeLists.txt`.
  - Tests: `test/PluginSystem/PluginManifestTest.{h,cc}` (11 cases) registered in `test/CMakeLists.txt` + `test/PluginSystem/CMakeLists.txt`. All pass (`--unittest:PluginManifestTest`).
  - Pure logic, no libraries loaded — matches the plan's test description exactly.
  - Next: U1.2 (loader gate: metadata before code) in a fresh chat.

- U1.2–U1.6: not started.

### Stage 2–5

Not started — blocked on Stage 1 completing (U1.2–U1.6).

## Notes for the next unit (U1.2)

- `HostInfo{version, apiVersion, buildId}` already exists in `PluginManifest.h` — U1.2 constructs one from `QGC_APP_VERSION_STR`/`QGC_GIT_HASH` compile definitions (not yet wired as compile defs for PluginSystem sources; U1.2 must add that).
- `apiVersion` constant is `1` until Stage 2 bumps it (D6) — U1.2's loader gate should reference a single named constant, not a magic number, so U2.1 has one place to bump.
- `PluginManifest::fromJson` surfaces `errorOut` for malformed/invalid manifests; U1.2's `inspect()` phase should also surface `QPluginLoader::errorString()` when `metaData()` comes back empty (S1's wrong-arch finding, plan §11).
