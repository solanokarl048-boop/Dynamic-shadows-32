// main.cpp — ShadowExtender
//
// GTA San Andreas Android plugin for AndroidModLoader (AML) / aml-psdk.
// Built against headers pulled from the real SDK (plugin.h,
// entity/Object.h, entity/Entity.h, entity/Placeable.h, engine/Shadows.h,
// base/Timer.h, mod/amlmod.h, mod/iaml.h).
//
// HOW THIS WORKS (confirmed via on-device checkpoint testing, not just
// theory -- see the checkpoint markers below)
// ---------------------------------------------------------------------
// First approach tried: hooking CShadows::StoreShadowForTree(CEntity*),
// which exists in the game as a NOP. Confirmed via CHECKPOINT_3 never
// appearing that this function is genuinely never called by the engine
// in this build -- not just an unused NOP, actually dead code.
//
// Working approach: hooking CEntity::Render() itself. Every entity type
// calls this when drawn unless it overrides it with its own version;
// CObject (trees, vegetation, small static props, decorations) doesn't
// appear to override it, so this fires for every visible object each
// frame it's rendered -- confirmed via CHECKPOINT_4 actually appearing
// on-device. Render() is virtual, so its address isn't exposed via the
// DECL_* header macros; it's resolved manually via dlsym using the same
// Itanium mangling pattern every other CEntity method follows.
//
// SCOPE: this covers CObject entities broadly (trees + small static
// props + decorations), not specifically "trees" as a distinct category
// -- there's no confirmed CModelInfo/model-name header available yet to
// filter by name. Buildings and peds/vehicles are excluded via
// IsObject().
//
// KNOWN GAP: shadow direction is a fixed value from the INI, not tied to
// live sun position. The real primitive for that,
// CShadows::CalcPedShadowValues(CVector UnitVecToLight, ...), is known,
// but no CTimeCycle/CWeather-equivalent header has been found yet to
// supply the light vector.
//
// SHADOW MECHANISM: GTA:SA Android doesn't use texture-blob shadows for
// peds/vehicles -- those are dynamic/real-time. Shadows.h already had the
// real primitive for this the whole time: CShadows::StoreRealTimeShadow
// (CPhysical *pPhysical, float ShadowDisplacementX, float
// ShadowDisplacementY, float ShadowFrontX, float ShadowFrontY, float
// ShadowSideX, float ShadowSideY) -> bool. CObject (what trees/props are)
// inherits CPhysical, so entities from the Render() hook can be passed in
// directly. This replaces an earlier, wrong detour into texture-blob
// lookups via CTxdStore -- removed below.

#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/entity/Object.h>
#include <aml-psdk/game_sa/entity/Entity.h>
#include <aml-psdk/game_sa/engine/Shadows.h>
#include <aml-psdk/game_sa/base/Timer.h>
#include <mod/amlmod.h>
#include <android/log.h>
#include <dlfcn.h>

#include "mod/IniConfig.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ShadowExtender", __VA_ARGS__)

// Non-destructive on-device diagnostic: write a marker file at each
// checkpoint, checkable with any file manager app. Left in place (cheap,
// capped) in case future changes need the same kind of verification.
static const char *kMarkerDir = "/sdcard/Android/data/com.rockstargames.gtasa/files/";

static void WriteMarker(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", kMarkerDir, name);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("checkpoint reached\n", f);
        fclose(f);
    }
}

MYMOD("net.psdk.samod.shadowextender", "ShadowExtender", "1.0.0", "YourName")

// ---------------------------------------------------------------------
// Configuration (populated from ShadowExtender.ini on load)
// ---------------------------------------------------------------------
struct Config {
    bool  enabled     = true;
    bool  shadowObjects = true; // was "shadowTrees" -- see SCOPE note above
    bool  useRealTimeShadow = false; // StoreRealTimeShadow confirmed rejecting static objects (trees) -- see CHECKPOINT_5_returned_false
    float opacity       = 180.0f; // maps to CShadows Brightness (i16), blob fallback only
    float sizeScale      = 1.0f;  // multiplies the base shadow radius
    float baseRadius     = 3.0f;  // world units, before sizeScale
    float dirFrontX       = 0.7f; // fixed shadow-spread direction,
    float dirFrontY       = 0.7f; // pending a real light-direction
    float dirSideX        = -0.7f;// source (see KNOWN GAP above)
    float dirSideY        = 0.7f;
    std::vector<int> modelBlacklist;
};

static Config g_cfg;

static const char *kIniPath = "/sdcard/Androidg_unprotected/data/com.rockstargames.gtasa/configs";

static void LoadConfig() {
    IniConfig ini;
    if (!ini.Load(kIniPath))
        return; // keep defaults if no INI present yet

    g_cfg.enabled       = ini.GetBool("General", "Enabled", g_cfg.enabled);
    g_cfg.shadowObjects  = ini.GetBool("Targets", "ShadowObjects", g_cfg.shadowObjects);
    g_cfg.useRealTimeShadow = ini.GetBool("Render", "UseRealTimeShadow", g_cfg.useRealTimeShadow);

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
// The hook: CEntity::Render(). Confirmed firing on-device (CHECKPOINT_4).
// ---------------------------------------------------------------------
DECL_HOOKv(HookedEntityRender, CEntity *pThis)
{
    HookedEntityRender(pThis); // MUST call original -- this is what actually draws the model

    static int renderCallCount = 0;
    if (renderCallCount < 5) {
        renderCallCount++;
        WriteMarker("CHECKPOINT_4_render_hook_fired.txt");
    }

    if (!g_cfg.enabled || !g_cfg.shadowObjects || !pThis)
        return;

    // See SCOPE note at top of file -- this is CObject broadly, not
    // specifically "trees".
    if (!pThis->IsObject())
        return;

    if (ModelBlacklisted(pThis->m_nModelIndex))
        return;

    float radius = g_cfg.baseRadius * g_cfg.sizeScale;
    float fx = g_cfg.dirFrontX * radius;
    float fy = g_cfg.dirFrontY * radius;
    float sx = g_cfg.dirSideX  * radius;
    float sy = g_cfg.dirSideY  * radius;

    if (g_cfg.useRealTimeShadow) {
        // Real dynamic shadow system -- same mechanism the game itself
        // uses for peds/vehicles, not a texture blob. CObject IS-A
        // CPhysical, so this cast is safe given IsObject() already
        // returned true above.
        CPhysical *pPhysical = static_cast<CPhysical*>(pThis);
        bool ok = CShadows::StoreRealTimeShadow(pPhysical, 0.0f, 0.0f, fx, fy, sx, sy);

        static int rtCallCount = 0;
        if (rtCallCount < 5) {
            rtCallCount++;
            WriteMarker(ok ? "CHECKPOINT_5_realtimeshadow_returned_true.txt"
                           : "CHECKPOINT_5_realtimeshadow_returned_false.txt");
        }
    } else {
        // Fallback: flat untextured polygon blob. SHADOW_DEFAULT rather
        // than SHADOW_ADDITIVE -- RenderStoredShadows(bool renderAdditive)
        // renders additive- and non-additive-type stored shadows in two
        // separate passes; if the engine's main loop only calls the
        // non-additive pass by default (plausible, given CJ's own shadow
        // always renders reliably), anything stored as SHADOW_ADDITIVE
        // would never actually get drawn regardless of everything else
        // being correct. SHADOW_DEFAULT should go through the same pass
        // that's already confirmed active.
        CVector pos = pThis->GetPosition();
        CShadows::StoreShadowToBeRendered(
            SHADOW_DEFAULT,
            &pos,
            fx, fy,
            sx, sy,
            (i16)g_cfg.opacity,
            0, 0, 0
        );
    }
}

// ---------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------
ON_MOD_LOAD()
{
    LOGI("ShadowExtender OnModLoad() reached");
    WriteMarker("CHECKPOINT_1_modload_reached.txt");

    LoadConfig();
    LOGI("Config loaded: enabled=%d shadowObjects=%d opacity=%.1f",
         g_cfg.enabled, g_cfg.shadowObjects, g_cfg.opacity);

    void *gameLib = dlopen("libGTASA.so", RTLD_NOLOAD);
    if (!gameLib) {
        LOGI("ERROR: dlopen(libGTASA.so, RTLD_NOLOAD) failed");
        WriteMarker("CHECKPOINT_ERROR_gamelib_not_found.txt");
        return;
    }

    uintptr_t renderAddr = (uintptr_t)dlsym(gameLib, "_ZN7CEntity6RenderEv");
    LOGI("CEntity::Render resolved address: 0x%lx", (unsigned long)renderAddr);

    if (renderAddr == 0) {
        LOGI("ERROR: CEntity::Render symbol did not resolve");
        WriteMarker("CHECKPOINT_ERROR_render_symbol_not_resolved.txt");
        return;
    }

    HOOK(HookedEntityRender, renderAddr);
    LOGI("Hook installed on CEntity::Render");
    WriteMarker("CHECKPOINT_2_hook_installed.txt");
}
