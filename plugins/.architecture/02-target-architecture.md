# Target Architecture — What Should Be Implemented

## 1. Lessons from the reference systems

### VS Code

- **Manifest first.** Every extension declares identity, version, publisher, **`engines.vscode` compatibility range**, and its contributions (`contributes:` menus, views, commands) in `package.json`. The host reads all of this — and renders UI for it — without running extension code. Code runs only when an activation event fires.
- **One versioned API, no internals.** Extensions import the `vscode` API module and nothing else. The API is append-only within a major version; anything experimental is gated behind `enabledApiProposals`. This single boundary is *the* reason a 2021 extension loads in a 2026 VS Code.
- **Portability comes from late binding.** JS extensions are portable because JavaScript is resolved by name at runtime, not by memory layout at compile time. The C++ analog is: minimize the compiled-ABI surface, push interaction through Qt's meta-object system (signals/slots/properties by name, `QVariant` payloads, QML) which is late-bound the same way.
- **Resilience.** Crashing extensions are identified, and the host can bisect/disable them rather than being taken down repeatedly.

### ATAK

- **Plugins are separately distributed packages** (APKs). The host discovers installed plugin packages, verifies their **signing signature against a trust list / user acceptance**, and loads them — no rebuild of the host, true field distribution.
- **Compatibility is declared and enforced, not assumed.** Each plugin declares the ATAK API version it targets; the host refuses mismatches at load. Notably, ATAK's contract is *strict* — plugins historically must be rebuilt per ATAK release. That is the honest ceiling for compiled plugins against a fast-moving native host, and it informs the tier model below: **binary compatibility across host versions is something you buy with a deliberately frozen SDK surface, and the price is paid by the host, not wished into existence.**
- **A published SDK** (ATAK CIV SDK) is the only supported way to build a plugin; plugin authors never compile against the host's source tree.

### The synthesis

Both systems agree on three things the current QGC infrastructure lacks: a **manifest read before code runs**, a **versioned API boundary instead of host internals**, and a **declared, enforced compatibility range**. They disagree on portability (VS Code: years; ATAK: one release) precisely because of language ABI — which is why QGC needs an explicit tier model rather than one promise.

## 2. Design principles

1. **The contract is declared, not discovered.** Everything the host needs to decide "should I run this code" — identity, version, tier, host range — lives in static metadata.
2. **Plugins compile against the SDK, never against `src/`.** The SDK is small, versioned, append-only, and ABI-disciplined. If a plugin needs something the SDK lacks, that's a request for a new SDK seam — or the plugin declares itself matched-build.
3. **Prefer late-bound surfaces.** QML URLs, `QVariantMap` contributions, named QObject properties/signals, `qobject_cast` capability discovery. Compiled C++ calls across the boundary are the scarce resource; spend them only where performance or type-safety demands.
4. **Compatibility promises are per-tier and explicit.** Never imply VS Code-grade portability for a compiled plugin.
5. **Fail closed, fail informatively.** Version/tier mismatch → refuse load with a user-visible reason in the Plugins settings page, not a crash or silent skip.
6. **Keep what works.** The extension points, settings integration, and dev loop from the current system carry over unchanged in behavior.

## 3. The tier model (the central recommendation)

| | Tier A — QML-only | Tier B — SDK-native | Tier C — Internals-native |
|---|---|---|---|
| **Contents** | Manifest + QML/JS + assets. **No native code.** | Manifest + native lib linking **only** `QGCPluginAPI` + Qt | Manifest + native lib compiled against QGC internals (today's model) |
| **Compatibility** | Any host version whose QML plugin API level ≥ declared minimum; **all platforms from one package** | Host range `[min, max)` per platform + Qt series + compiler ABI tag | The exact host build it was compiled with |
| **Windows distribution** | ✔ single package | ✔ DLL linked against SDK import lib | ✖ (not even linkable without exe import lib) |
| **macOS distribution** | ✔ single package | ✔ dylib linked against SDK (sign/notarize for distribution, §6) | ✖ for distribution (loads via dynamic lookup, but only into the exact matching build) |
| **Android distribution** | ✔ **installable at runtime** (QML is data, not native code — no W^X issue) | Via plugin-APK (ATAK model, §6) or compiled into the host APK | Compiled into the host APK only |
| **Analog** | VS Code JS extension | ATAK SDK plugin | ATAK "core hackers" / in-tree patch |
| **Who uses it** | UI panels, dashboards, simple tools using the `QGroundControl` QML API | Serious third-party plugins | QDrive today; deep integrations while the SDK grows |

Tier A is cheap to add (the panel/menu infrastructure already loads QML by URL — it only needs to load from a package directory instead of a linked-in `qrc`) and is the **only** tier that fully meets "distribute to users, works across versions, on every platform — Windows, macOS, and Android." Tier B is the workhorse for native plugins. Tier C is the honest name for what exists now — keep it, but make plugins declare it so the loader can refuse a mismatched build instead of undefined behavior.

The manifest declares the tier; the loader enforces per-tier rules. QDrive starts as Tier C and migrates toward B as SDK seams appear (its replay extension and panels are already B-shaped; its LinkManager/MAVLinkProtocol/PlanMasterController usage is what needs SDK seams).

## 4. The manifest

Use Qt's built-in mechanism — `Q_PLUGIN_METADATA(IID QGCPluginInterface_iid FILE "qgcplugin.json")` — so `QPluginLoader::metaData()` yields it **without executing plugin code**. Tier A plugins (no binary) ship the same JSON as a plain file in their package.

```json
{
    "name": "Example",
    "id": "org.example.qgc.example",
    "version": "1.2.0",
    "vendor": "Example Org",
    "description": "Demonstrates the QGC plugin system",
    "tier": "sdk",                      // "qml" | "sdk" | "internal"
    "apiVersion": 2,                    // QGCPluginAPI major version compiled against
    "hostVersion": { "min": "5.1", "max": "6.0" },
    "hostBuildId": null,                // required (git hash) when tier == "internal"
    "contributes": {                    // optional, mirrors what the code registers;
        "toolMenu": true,               // lets the settings page describe a plugin
        "flyViewPanel": true,           // without loading it
        "planViewPanel": true,
        "replay": false,
        "telemetryLogging": false
    }
}
```

Loader behavior: read metadata → check IID + tier rules (`apiVersion` supported? host version in range? `hostBuildId` matches for tier C?) → only then `instance()`. Failures surface in Application Settings → Plugins as "incompatible: needs QGC ≥ 5.1" rather than a log line. `id` (reverse-DNS) replaces display `name` as the settings key, freeing plugins to localize names.

## 5. The Plugin SDK (`QGCPluginAPI`)

A new top-level module, e.g. `src/PluginAPI/` (or promoted from `src/PluginSystem/`), built as a **shared library** that both the app and plugins link. It contains:

1. **The plugin-side contract** — `QGCPluginInterface`, `QGCPlugin`, `QGCReplayExtension`, and future extension interfaces. Moving `QGCPlugin`'s implementation into a real library (instead of the host exe + dynamic lookup) is what makes Windows work: plugins link the SDK **import library**, and the exe stops needing `-export_dynamic`.
2. **Host services** — the inversion that removes the need for internals. Instead of the plugin calling `LinkManager::instance()`, the host hands the plugin a services object:

```cpp
// SDK header — pure interface, host implements it inside the app
class QGCHostServices : public QObject {
    Q_OBJECT
public:
    virtual QObject* service(const QString &id) = 0;   // late-bound lookup
    // e.g. "qgc.links.replay/1", "qgc.telemetry.logging/1",
    //      "qgc.plan.controller/1", "qgc.vehicles/1", "qgc.settings/1"
};

class QGCPlugin : public QObject {
    ...
    virtual void init(QGCHostServices *host);   // replaces bare init()
};
```

Each service id names a small QObject-based interface defined in the SDK (versioned in the id, ATAK-style). The host implements it by *wrapping* the internal class (`LinkManager`, `MAVLinkProtocol`, …) — internals stay free to change; the wrapper is the frozen surface. Start with only the services QDrive and the example actually need: replay-link control, telemetry-logging takeover, plan/mission access, vehicle/parameter access (which can largely ride the existing Fact/QML API), and app paths/settings.

3. **ABI discipline rules** (enforced by review + a CI check that plugins' include paths contain only the SDK):
   - Interfaces are pure-virtual or `QObject`-derived with **d-pointers**; no exported data members; no inline functions that touch members.
   - **Append-only evolution.** Never add a virtual to a shipped class. New capability = new named service / new interface `…/2` discovered via `service()` or `qobject_cast`. Bump `apiVersion` (major) only on genuine breaks.
   - Cross-boundary payloads prefer `QVariantMap` / `QJsonObject` / `QString` — already the house style of the contribution points.
   - Compatibility contract stated plainly in the SDK README: *a plugin built against SDK major N runs on any host with SDK major N and hostVersion in the declared range, on the same platform, same Qt major series (host Qt ≥ plugin's build Qt, per Qt's forward binary-compatibility guarantee), same compiler ABI (MSVC / libc++ / libstdc++).* That is the realistic C++ ceiling — stronger than ATAK's per-release rebuild, weaker than VS Code, honest.

4. **The QML plugin API level** for Tier A: the existing `QGroundControl` QML singleton tree *is* the API; it needs a declared integer level (bumped when QML-visible surface changes incompatibly) that manifests reference. This costs almost nothing and formalizes what plugin QML may touch.

### What does *not* go in the SDK

No re-export of Vehicle/FactSystem/MissionManager headers "for convenience" — that recreates the coupling with extra steps. No stable-C-ABI layer yet: the roadmap already defers the C ABI until QDrive is feature-complete and the needed surface is known; the SDK's service seams are exactly the input that future C ABI (or out-of-process host, if ever wanted) would formalize.

## 6. Packaging & per-platform distribution

**Package format:** one zip, extension `.qgcplugin`:

```
MyPlugin.qgcplugin
├── qgcplugin.json              # manifest (§4)
├── qml/ …                      # Tier A content and/or panel QML
├── assets/ …
└── bin/                        # absent for Tier A
    ├── windows-x64/MyPlugin.dll
    ├── macos-universal/MyPlugin.dylib
    └── linux-x64/MyPlugin.so
```

Installed (unzipped) into the existing per-user plugins directory; the Plugins settings page gains "Install from file…" / "Remove". The current directory-scan of bare libraries keeps working for development.

- **Windows:** solved by the SDK — the plugin DLL links `QGCPluginAPI.lib` + Qt import libs, fully resolved at link time. Plugin authors build against an **SDK package** (headers + import libs + the Qt kit version pin) published per QGC release series, so they never clone/build QGC. (Tactical interim option for Tier C only: `ENABLE_EXPORTS ON` on the exe to make matched-build Windows plugins linkable — but it's superseded by the SDK and probably not worth upstreaming.)
- **macOS/Linux:** same SDK linkage; drop `-undefined dynamic_lookup` / `-export_dynamic` for Tier B. Note macOS Gatekeeper: distributed dylibs must be signed/notarized or users face quarantine friction — document it; hard enforcement is the host vendor's call.
- **Android:** two mechanisms, by tier.
  - **Tier A works today's-web-style:** QML/JS/assets are data, not native code — a `.qgcplugin` without `bin/` can be downloaded and installed at runtime with no OS restriction. This is the recommended v1 Android distribution story and it falls out of Tier A for free.
  - **Native plugins, ATAK model (v2):** plugin ships as a separate installed **APK**; the host enumerates installed packages carrying a `qgc-plugin` marker (meta-data + shared IID), **verifies the APK signature** against accepted keys / explicit user approval, then loads the plugin's native library from that APK's own (execute-allowed) native-lib directory. This is exactly how ATAK distributes field plugins and is the only OS-sanctioned way to load third-party native code. It is real work (package discovery, signature policy, Qt-version pinning between host APK and plugin APK) — schedule it as its own phase, after the SDK exists.
  - Until then, compiling plugins into the APK (current behavior) remains the Tier B/C Android answer.

## 7. Trust & resilience (needed for upstream acceptance)

Upstream QGC is used operationally; "auto-execute every library found in a user-writable folder" will draw review objections. Minimum viable posture:

1. **First-run consent:** a newly discovered plugin (by id+version+file hash) is listed in the Plugins page as "new — not yet enabled" and runs only after the user enables it. Default-enabled only for plugins shipped inside the app bundle/APK. (This also replaces the "Example disabled by default" special case with a general rule.)
2. **Crash quarantine:** persist "loading plugin X" before `instance()`, clear it after startup completes; if startup died mid-load, auto-disable X and tell the user (VS Code-bisect-lite).
3. **Signature hook:** manifest/loader leave room for package signature verification (mandatory on the Android plugin-APK path, optional elsewhere). Don't build PKI now; keep the seam.
4. Keep runtime **disable** (stop instantiating) but drop the pretense of true library **unload** (01-current-state §4.6): disable takes effect immediately for contributions, full unload on next restart.

## 8. Target component picture

```
                 ┌────────────────────────────────────────────────┐
                 │ QGroundControl app                             │
                 │  ┌──────────────┐   ┌────────────────────────┐ │
                 │  │QGCPluginMgr  │──▶│ Host service impls     │ │
                 │  │ (discovery,  │   │ (wrap LinkManager,     │ │
                 │  │  manifest    │   │  MAVLinkProtocol,      │ │
                 │  │  gate, tiers,│   │  PlanMasterController…)│ │
                 │  │  settings)   │   └───────────┬────────────┘ │
                 │  └──────┬───────┘               │ implements   │
                 └─────────┼───────────────────────┼──────────────┘
                    loads  │        links          │
                           ▼                       ▼
                 ┌────────────────────────────────────────────────┐
                 │ QGCPluginAPI (shared lib, versioned, frozen)   │
                 │  QGCPluginInterface · QGCPlugin ·              │
                 │  QGCHostServices + service interfaces ·        │
                 │  QGCReplayExtension · manifest schema          │
                 └────────────────────────────────────────────────┘
                           ▲ links (Tier B)            ▲ QML API level (Tier A)
              ┌────────────┴─────────────┐   ┌─────────┴─────────┐
              │ Native plugin (.dll/.so/ │   │ QML-only plugin   │
              │ .dylib in .qgcplugin or  │   │ (.qgcplugin, all  │
              │ plugin-APK on Android)   │   │ platforms)        │
              └──────────────────────────┘   └───────────────────┘
```

(Tier C plugins additionally compile against app internals and are gated by `hostBuildId`.)
