# QGC Plugin System — Architecture Review & Roadmap

**Date:** 2026-07-06
**Scope:** The plugin infrastructure added on this fork (`src/PluginSystem/`, `plugins/`, `cmake/modules/PluginHelpers.cmake`, `src/Settings/PluginSettings.*`, and the QML consumers in FlyView/PlanView/AppSettings).
**Goal under review:** A true runtime plugin system — plugins built and distributed as standalone binaries (including Windows, macOS, and Android), loadable by multiple versions of the QGroundControl base app, without building the base app and the plugin together. Modeled on mature plugin hosts such as VS Code and ATAK. Intended for upstream contribution, plugin-agnostic, usable by any third-party developer.

## Verdict (executive summary)

The infrastructure built so far is a solid **extension-point layer** — the *what plugins can do* half of a plugin system is real, plugin-agnostic, and close to upstream-ready. What is missing is the *contract* half: today a plugin binary is bound to the **exact build** of QGC it was compiled with, because plugins compile against the full QGC source tree and resolve arbitrary internal C++ symbols from the executable at load time. That makes the current system a runtime *loader* for compile-time-coupled artifacts — the hybrid the goal statement describes.

Three changes convert it into a true runtime plugin system:

1. **A declared contract: the manifest.** Every plugin ships JSON metadata (read by Qt *before* any plugin code runs) declaring its identity, version, API tier, and compatible host range. This is the VS Code `package.json` / ATAK plugin-descriptor equivalent, and it is the cheapest, most upstreamable first step.
2. **A boundary: the Plugin SDK.** Plugins stop compiling against `src/` and instead compile against a small, versioned, ABI-disciplined `QGCPluginAPI` library — interfaces plus host-service accessors. This is what makes a Windows plugin DLL linkable at all, and what makes "works on host versions N through M" a promise instead of luck.
3. **An honest tiered compatibility model.** Not all plugins can get VS Code-grade portability from C++ — even ATAK requires plugins to target a matching API version. The system should offer three explicit tiers: **QML-only** plugins (portable across host versions and platforms, and the only tier installable at runtime on Android), **SDK-native** plugins (compatible across a declared host range per platform/Qt series), and **internals-native** plugins (matched-build only — today's model, kept as an explicit, declared escape hatch so QDrive keeps working while the SDK grows).

## Documents

| Doc | Contents |
|-----|----------|
| [01-current-state.md](01-current-state.md) | Inventory of everything implemented: components, extension points, build/link model, load flow, per-platform status — plus specific defects found during review |
| [02-target-architecture.md](02-target-architecture.md) | What should be implemented: lessons taken from VS Code and ATAK, design principles, the tier model, manifest schema, SDK design rules, per-platform distribution |
| [03-implementation-plan.md](03-implementation-plan.md) | How to implement it: five phases, concrete file-level steps, the upstream PR series, migration path for the example and QDrive plugins, risks and spikes |

## How to read this if you only have five minutes

Read the verdict above, then the "Why the current system cannot meet the goal" section of [01-current-state.md](01-current-state.md), then the tier table in [02-target-architecture.md](02-target-architecture.md), then the phase list at the top of [03-implementation-plan.md](03-implementation-plan.md).
