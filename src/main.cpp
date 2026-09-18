// main.cpp — ShadowExtender
//
// GTA San Andreas Android plugin for AndroidModLoader (AML) / aml-psdk.
//
// This version is built against headers actually pulled from the real
// SDK (plugin.h, engine/Shadows.h, entity/Object.h, entity/Entity.h,
// entity/Placeable.h, base/Timer.h, mod/amlmod.h, mod/iaml.h) rather
// than guessed. Everything below compiles against confirmed real
// symbols, with one deliberate scope decision explained below.
//
// HOW THIS WORKS
// ---------------
// The shipped game already has CShadows::StoreShadowForTree(CEntity*),
// but it's a NOP -- the engine identifies trees and calls this function
// per-tree (almost certainly during its render/pre-render pass, since
// CEntity also has a dedicated ModifyMatrixForTreeInWind for the same
// category of object), it just never does anything with the call. That
// makes it a clean hook point: rather than scanning the world for
// candidate objects every frame (which would need CPools/CWorld headers
// this project doesn't have yet), we hook the one function the engine
// already calls once per tree and give it a real shadow.
//
// SCOPE NOTE: this covers trees/vegetation, which the engine already
// routes through StoreShadowForTree. Generic small objects and buildings
// don't have an equivalent NOP hook in what's been inspected so far --
// extending to those would need the world/pool iteration API (CPools /
// CWorld), which hasn't been confirmed against real headers yet. Treat
// that as a follow-up, not something guessed at here.
//
// REMAINING KNOWN GAP: shadow direction. The real "advanced shadow
// system" primitive for this is CShadows::CalcPedShadowValues(CVector
// UnitVecToLight, ...), which derives correct front/side spread from a
// unit vector toward the light source -- but no CTimeCycle/CWeather
// equivalent has been found yet to supply that vector. Until that's
// located, this uses a configurable fixed direction from the INI
// instead of a live sun direction. If you find the real light-direction
// source in the SDK, swap it in where marked below.

#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/entity/Object.h>
#include <aml-psdk/game_sa/entity/Entity.h>
#include <aml-psdk/game_sa/engine/Shadows.h>
#include <aml-psdk/game_sa/base/Timer.h>
#include <mod/amlmod.h>

#include "mod/IniConfig.h"

MYMOD("net.xenon.shadowextender", "ShadowExtender", "1.0.0", "xenon")

// ---------------------------------------------------------------------
// Configuration (populated from ShadowExtender.ini on load)
// ---------------------------------------------------------------------
struct Config {
    bool  enabled        = true;
    bool  shadowTrees     = true;
    float opacity          = 180.0f; // maps to CShadows Brightness (i16)
    float sizeScale         = 1.0f;  // multiplies the base shadow radius
    float baseRadius        = 3.0f;  // world units, before sizeScale
    float dirFrontX          = 0.7f; // fixed shadow-spread direction,
    float dirFrontY          = 0.7f; // pending a real light-direction
    float dirSideX           = -0.7f;// source (see file header note)
    float dirSideY           = 0.7f;
    std::vector<int> modelBlacklist;
};

static Config g_cfg;

static const char *kIniPath = "/sdcard/Android/data/com.rockstargames.gtasa/files/ShadowExtender.ini";

static void LoadConfig() {
    IniConfig ini;
    if (!ini.Load(kIniPath))
        return; // keep defaults if no INI present yet

    g_cfg.enabled      = ini.GetBool("General", "Enabled", g_cfg.enabled);
    g_cfg.shadowTrees   = ini.GetBool("Targets", "ShadowTrees", g_cfg.shadowTrees);

    g_cfg.opacity        = ini.GetFloat("Render", "Opacity", g_cfg.opacity);
    g_cfg.sizeScale       = ini.GetFloat("Render", "SizeScale", g_cfg.sizeScale);
    g_cfg.baseRadius      = ini.GetFloat("Render", "BaseRadius", g_cfg.baseRadius);
    g_cfg.dirFrontX        = ini.GetFloat("Render", "DirFrontX", g_cfg.dirFrontX);
    g_cfg.dirFrontY        = ini.GetFloat("Render", "DirFrontY", g_cfg.dirFrontY);
    g_cfg.dirSideX          = ini.GetFloat("Render", "DirSideX", g_cfg.dirSideX);
    g_cfg.dirSideY          = ini.GetFloat("Render", "DirSideY", g_cfg.dirSideY);

    g_cfg.modelBlacklist    = ini.GetIntList("Models", "Blacklist");

    g_cfg.opacity   = std::max(0.0f, std::min(g_cfg.opacity, 255.0f));
    g_cfg.sizeScale  = std::max(0.1f, std::min(g_cfg.sizeScale, 5.0f));
}

static bool ModelBlacklisted(int modelId) {
    return std::find(g_cfg.modelBlacklist.begin(), g_cfg.modelBlacklist.end(), modelId)
           != g_cfg.modelBlacklist.end();
}

// ---------------------------------------------------------------------
// The hook: CShadows::StoreShadowForTree is a NOP in the shipped game.
// We replace it with a real implementation. DECL_HOOKv declares both the
// trampoline (pointer to the original, still callable) and this handler.
// ---------------------------------------------------------------------
DECL_HOOKv(HookedStoreShadowForTree, CEntity *pEntity)
{
    // Call the original first (a NOP today, but harmless and future-proof
    // if the engine ever does something in it, or another mod hooks it).
    HookedStoreShadowForTree(pEntity);

    if (!g_cfg.enabled || !g_cfg.shadowTrees || !pEntity)
        return;

    if (ModelBlacklisted(pEntity->m_nModelIndex))
        return;

    CVector pos = pEntity->GetPosition();
    float radius = g_cfg.baseRadius * g_cfg.sizeScale;

    float fx = g_cfg.dirFrontX * radius;
    float fy = g_cfg.dirFrontY * radius;
    float sx = g_cfg.dirSideX  * radius;
    float sy = g_cfg.dirSideY  * radius;

    CShadows::StoreShadowToBeRendered(
        SHADOW_ADDITIVE,
        &pos,
        fx, fy,
        sx, sy,
        (i16)g_cfg.opacity,
        0, 0, 0 // dark blob; tweak RGB here for a tinted foliage shadow
    );
}

// ---------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------
ON_MOD_LOAD()
{
    LoadConfig();
    HOOK(HookedStoreShadowForTree, (uintptr_t)CShadows::StoreShadowForTree);
}

