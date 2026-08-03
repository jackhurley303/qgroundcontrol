/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCPluginLoader.h"

class PluginRecordStore;

/// @brief Decides whether a discovered plugin is trusted to activate (D10/D15)
///
/// Single owner (Pillar 2) of the trust/consent policy: crash-quarantine precedence,
/// the macOS quarantine-attribute check, and the user-dir consent digest. Stateless —
/// every decision reads the record under inspection plus whatever persisted state
/// PluginRecordStore already holds; nothing here is owned or cached.
class PluginTrustGate
{
public:
    PluginTrustGate() = delete;

    /// @brief Gate a freshly-discovered record before activation is attempted
    /// Only acts on PluginState::Discovered records; a no-op otherwise. Checks, in
    /// order: the crash sentinel (outranks consent — a plugin that crashed the host
    /// must not become runnable by mere approval), the macOS quarantine attribute, then
    /// user-dir consent (D10). Leaves the record Discovered if none apply.
    /// @param record The record to gate, mutated in place (state/errorString)
    /// @param store Source of the crashed-plugin id and any recorded consent digest
    static void applyTrustGate(PluginLoadInfo& record, PluginRecordStore& store);

    /// @brief Whether a record's container directory is the user plugins directory
    /// A record's container is the entry the search directory scan found: the package
    /// directory, or the bare dylib file itself. Its parent being the user plugins dir
    /// is what makes a plugin user-dir (untrusted until approved, D10).
    static bool isUserDirPlugin(const PluginLoadInfo& record);

    /// @brief Consent digest for a user-dir plugin (D10/D15)
    /// Manifest version + SHA-256 over the manifest file and the resolved binary —
    /// the same two files whose quarantine status matters. Empty on read failure,
    /// which callers must treat as "cannot consent".
    static QString consentDigest(const PluginLoadInfo& record);
};
