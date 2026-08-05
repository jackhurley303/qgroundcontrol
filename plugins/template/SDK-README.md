# QGroundControl Plugin SDK

This package lets you build a QGroundControl **Tier B** plugin — a `.dylib`/`.so`/`.dll`
that loads into a QGC build you never compiled, without cloning the QGC source tree.
It contains:

```
include/QGCPluginAPI/...          # public headers (QGCPlugin, QGCPluginInterface,
                                   # QGCHostServices, and the host service interfaces)
lib/libQGCPluginAPI.*.dylib       # the versioned SDK shared library (macOS)
lib/cmake/QGCPluginAPI/...        # find_package(QGCPluginAPI) config
qml/QGroundControl/PluginUI/...   # the published QML vocabulary (see below)
template/                         # a copy-and-build starting point (see below)
SDK-README.md                     # this file
```

## The QML vocabulary

If your plugin contributes UI, its QML may use the types published by the
`QGroundControl.PluginUI` module in `qml/`. There is nothing to link or deploy: your
plugin's QML runs inside the host's QML engine, so the host supplies every
implementation. The package's copy exists so you can *check* your QML without a QGC
checkout:

```bash
qmllint --import error --missing-property error --unresolved-type error \
        -I "$SDK/qml" MyPanel.qml
```

`find_package(QGCPluginAPI)` sets `QGCPluginAPI_QML_IMPORT_PATH` to that directory.

**Pass those severity flags.** qmllint diagnoses an unresolved type, an unknown property
or a missing import and still exits 0 by default, so a check without them prints the
problem and passes anyway.

The module has two tiers, marked in its `qmldir`. The frozen tier (controls, `ScreenTools`,
`QGCPalette`) gains types but never loses or reshapes one without a major version bump.
The map and mission types are published for lint-time resolvability only and are
explicitly exempt from that freeze — use them knowing they can change. Anything not in the
`qmldir` is not part of the contract, even if it exists in a QGC build you happen to have.

## Compatibility contract

> A plugin built against SDK major N runs on any host with SDK major N and
> hostVersion in the declared range, on the same platform, same Qt major series
> (host Qt ≥ plugin's build Qt, per Qt's forward binary-compatibility guarantee),
> same compiler ABI (MSVC / libc++ / libstdc++).

Concretely, for this package:

- **SDK major:** `find_package(QGCPluginAPI 2 REQUIRED)` — reject anything else at
  configure time rather than discover an ABI mismatch at runtime.
- **`hostVersion`** in your manifest declares the QGC app-version range your plugin
  supports (`{"min": "5.0", "max": ""}` = "5.0 and later"). The host checks this before
  ever instantiating your plugin.
- **Platform + Qt kit:** build against **Qt 6.10.3** (the pinned kit this SDK was built
  with — see `.github/build-config.json` in the QGC source, or the SDK zip's file name).
  A host running an older Qt 6.x than your plugin's build Qt is unsupported: you'll see
  Qt's own version-mismatch error surfaced through the Plugins settings page, not a
  crash. A host running the same or newer Qt 6.x works.
- **Compiler ABI (macOS):** build with the same Xcode/Clang toolchain family QGC itself
  ships with (libc++). Mixed Debug/Release configs are accepted looseness — not
  guaranteed, but libc++ makes it mostly work in practice.

## ABI rules (why the contract holds)

The SDK's classes are frozen by convention, not by the compiler, so plugin authors and
QGC maintainers share one discipline:

- **d-pointers, no exported data members.** Every SDK class hides its state behind an
  opaque pointer; adding a field never shifts a plugin-visible layout.
- **Append-only evolution.** A shipped virtual is never added, removed, or reordered.
  New capability ships as a new host service id (e.g. `qgc.replay/2`), never as a change
  to an existing interface — see the service table in the QGC source's
  `plugins/README.md`.
- **`apiVersion` is the escape hatch, not the norm.** It only bumps on a genuine break
  (a new SDK major), and every plugin rebuilds against the new major in lockstep — there
  is no in-place "minor ABI break."
- **`apiVersion` is tier-scoped.** It gates the *C++* ABI, so it's required and checked
  exactly for `tier: "sdk"` and `tier: "internal"` manifests. A future `tier: "qml"`
  package (no compiled binary at all) doesn't touch the C++ ABI and isn't gated by it —
  see the QML API level note in `plugins/README.md` once that tier ships.
- **Cross-boundary payloads prefer `QVariantMap`/`QJsonObject`/`QString`** over new
  value types, matching the manifest/contribution style throughout the loader.

## Try it

```bash
cmake -B build -S template -DCMAKE_PREFIX_PATH=$(pwd)
cmake --build build
```

(`CMAKE_PREFIX_PATH` points `find_package(QGCPluginAPI)` at this unpacked SDK — set it
to wherever you extracted the zip.) The template already contributes a real fly-view
panel (`TemplatePanel.qml`, bundled via `MyPlugin.qrc`) built entirely against the
`QGroundControl.PluginUI` module documented above — lint it the same way:

```bash
qmllint --import error --missing-property error --unresolved-type error \
        -I "$SDK/qml" template/TemplatePanel.qml
```

Then copy the built library into QGC's per-user plugins directory and relaunch:

| Platform | Plugins directory |
|---|---|
| macOS | `~/Library/Application Support/QGroundControl/plugins/` |
| Linux | `~/.local/share/QGroundControl/plugins/` |
| Windows | `%APPDATA%/QGroundControl/plugins/` |

A newly-discovered plugin in this directory needs to be enabled once from
Application Settings → Plugins before it runs. If it doesn't appear or shows
"Incompatible"/"Failed to load", check the reason shown there — it's the manifest gate
(`hostVersion`, `apiVersion`, or a Qt/library-load error) telling you exactly why,
without having run any of your code.

## macOS signing note

Locally built plugins are automatically ad-hoc signed by the linker and load
into unsigned dev builds of QGC with no extra steps. Loading into a **notarized
release** build of QGC additionally requires the host to carry the
`disable-library-validation` entitlement (see the QGC source's macOS implementation
plan, §1.3/§1.4) — that's a QGC-side release-signing concern, not something a plugin
author configures. If you distribute your plugin outside this installer path, sign it
with a Developer ID certificate (and notarize it) so Gatekeeper doesn't quarantine it
for your users.

## Forward look: Windows and Android

Not relevant to this package (macOS-only today), but worth knowing if you're planning
ahead for a plugin you intend to keep working:

- **Windows** has no soname mechanism — a DLL's filename doesn't carry the SDK major the
  way `libQGCPluginAPI.2.dylib` does on macOS. When the Windows SDK ships, rely on the
  manifest's `apiVersion` gate (which rejects before any of your code runs) rather than
  assuming the DLL filename tells you anything. Also pin the **same MSVC toolset family
  and CRT** as the SDK was built with — mixing them is unsupported on Windows in a way it
  mostly isn't on macOS/Linux.
- **Android** can only ever support `tier: "qml"` packages installed to a user-writable
  directory — modern Android's W^X enforcement forbids `dlopen`-ing a native library from
  app-writable storage, full stop. A native (Tier B/C) plugin on Android ships as its own
  installed APK instead; that's a different, larger distribution model than this package
  covers.
