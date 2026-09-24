#include <gtest/gtest.h>
#include "engine/PluginManager.h"

// The *FX shadow-edition gate (handoff 2026-09-23) — PURE function tests: no
// scan cache, no engine, fully deterministic (the machine-local
// plugin_cache.xml must never matter here). Gate 9: the family is an explicit
// four-name list, never a generic "*FX" suffix filter, so the negatives pin
// BOTH the instrument editions of the same family and adversarial near-misses
// with the token glued to a name on either side.

TEST(ShadowFxEditionPredicate, MatchesBareFamilyNames)
{
    // The bare cache names exactly as the scan records them.
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("OsirusFX"));
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("OsTIrusFX"));
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("VavraFX"));
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("XeniaFX"));
}

TEST(ShadowFxEditionPredicate, MatchesQualifiedIdsAndInstallPaths)
{
    // Format-qualified ids as observed in the wild (guide 2026-09-22 measured
    // "CLAP-VavraFX-a405fdaa-0" through add_fx): the family token sits between
    // dashes, so prefix/qualifier tolerance must not require an exact string.
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("CLAP-VavraFX-a405fdaa-0"));
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("VST3-OsirusFX-1a2b3c4d-0"));

    // Raw install paths — add_fx accepts .clap/.vst3 paths without a cache
    // entry (ProjectModel::resolvePluginFormat extension fallback), so a path
    // naming a shadow edition must be caught too.
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition(
        "C:\\Program Files\\Common Files\\CLAP\\XeniaFX.clap"));
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition(
        "/Library/Audio/Plug-Ins/CLAP/OsTIrusFX.clap"));

    // Case-insensitive: cache casing is vendor-defined.
    EXPECT_TRUE(HDAW::PluginManager::isShadowFxEdition("clap-vavrafx-a405fdaa-0"));
}

TEST(ShadowFxEditionPredicate, RejectsUnrelatedPlugins)
{
    // The instrument editions from the guide's device matrix — same gearmulator
    // family, NOT shadow builds (isSynth=TRUE): they must stay pickable.
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("Vavra"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("Osirus"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("OsTIrus"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("Xenia"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("Vital"));

    // Adversarial near-misses: the token glued to other characters names a
    // DIFFERENT plugin (boundary rule — this is what stops the check from
    // degenerating into a suffix sweep).
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("SuperVavraFX"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("VavraFXy"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("VavraFX2"));

    // Unrelated plugins that merely end in "FX": a generic *FX filter would
    // wrongly swallow these (Gate 9).
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("SomeFX"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition("FX"));
    EXPECT_FALSE(HDAW::PluginManager::isShadowFxEdition(""));
}
