# ============================================================================
# TestPluginFixture — a real SDK-tier (Tier B) plugin MODULE, built only by the
# test tree. This is PluginLoaderGateTest's permanent regression fixture: it
# proves the manifest gate, tier rules, activation, and contribution synthesis
# against an actual dylib, not a hand-built PluginLoadInfo.
#
# Built through the same qgc_add_plugin() single entry point every real plugin
# uses (so it stays representative of TIER SDK's real shape), with NO_DEPLOY:
# this fixture must never be copied into the real runtime plugin directory,
# where it would load (Active, default-enabled) in ordinary app runs.
# ============================================================================

qgc_add_plugin(TestPluginFixture
    TIER SDK
    NO_DEPLOY
    MANIFEST qgcplugin.json.in
    SOURCES
        TestPluginFixture.h
        TestPluginFixture.cc
)
