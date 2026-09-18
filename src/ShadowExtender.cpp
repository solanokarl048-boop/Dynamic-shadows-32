// ShadowExtender.cpp
//
// GTA San Andreas (Android) plugin built against plugin-sdk conventions
// (https://github.com/DK22Pac/plugin-sdk), which is the toolchain most
// Android mod-loader plugins (Fire-Head/ThirteenAG-style loaders, CLEO
// Android front-ends, etc.) are compiled against. It hooks the game's
// existing per-frame shadow pass and feeds the built-in advanced shadow
// renderer (CShadows) extra "blob" shadows for static world objects
// (trees, vegetation, small props, optionally buildings) that the base
// game normally never shadows.
//
// IMPORTANT: plugin-sdk exposes slightly different member names/signatures
// between game builds/forks. The calls below use the commonly-seen SA
// signatures. If your SDK snapshot differs, adjust the marked TODOs to
// match your local CShadows.h / CTimeCycle.h / CObject.h headers -- the
// logic and structure will not need to change, just the exact symbols.

#include "plugin.h"
#include "CShadows.h"
#include "CTimeCycle.h"
#include "CWorld.h"
#include "CPools.h"
#include "CObject.h"
#include "CBuilding.h"
#include "CModelInfo.h"
#include "CCamera.h"
#include "CVector.h"
#include "IniConfig.h"

#include <cstring>
#include <vector>
#include <algorithm>

using namespace plugin;

namespace ShadowExtender {

// ---------------------------------------------------------------------
// Configuration (populated from ShadowExtender.ini at game init)
// ---------------------------------------------------------------------
struct Config {
    bool  enabled            = true;
    bool  shadowTrees        = true;
    bool  shadowSmallObjects = true;
    bool  shadowBuildings    = false;   // off by default: buildings are
                                         // expensive to shadow correctly
    float maxDistance        = 60.0f;   // world units from camera
    float opacity             = 180.0f; // 0-255, matches CShadows alpha
    float sizeScale            = 1.0f;  // scales the blob footprint
    int   maxShadowsPerFrame   = 40;    // perf safety valve
    int   updateIntervalFrames = 2;     // re-scan every N frames
    bool  followSunDirection   = true;  // use engine's live shadow vector
    std::vector<int> modelBlacklist;
    std::vector<int> modelWhitelist;    // if non-empty, ONLY these IDs qualify
};

static Config g_cfg;
static int    g_frameCounter = 0;

// Cache of world objects currently receiving a synthetic shadow, so we
// don't need to do a full CWorld scan every single frame.
struct ShadowCandidate {
    CEntity *entity;
    float    radius;
    bool     isTree;
};
static std::vector<ShadowCandidate> g_candidates;

// ---------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------
static const char *kIniPath = "/sdcard/Android/data/com.rockstargames.gtasa/files/ShadowExtender.ini";

static void LoadConfig() {
    IniConfig ini;
    if (!ini.Load(kIniPath)) {
        // No INI found -- fall back to defaults defined above and write
        // a fresh copy so the user has something to edit next time.
        return;
    }

    g_cfg.enabled              = ini.GetBool("General", "Enabled", g_cfg.enabled);
    g_cfg.shadowTrees          = ini.GetBool("Targets", "ShadowTrees", g_cfg.shadowTrees);
    g_cfg.shadowSmallObjects   = ini.GetBool("Targets", "ShadowSmallObjects", g_cfg.shadowSmallObjects);
    g_cfg.shadowBuildings      = ini.GetBool("Targets", "ShadowBuildings", g_cfg.shadowBuildings);

    g_cfg.maxDistance          = ini.GetFloat("Render", "MaxDistance", g_cfg.maxDistance);
    g_cfg.opacity              = ini.GetFloat("Render", "Opacity", g_cfg.opacity);
    g_cfg.sizeScale            = ini.GetFloat("Render", "SizeScale", g_cfg.sizeScale);
    g_cfg.maxShadowsPerFrame   = ini.GetInt("Render", "MaxShadowsPerFrame", g_cfg.maxShadowsPerFrame);
    g_cfg.updateIntervalFrames = ini.GetInt("Render", "UpdateIntervalFrames", g_cfg.updateIntervalFrames);
    g_cfg.followSunDirection   = ini.GetBool("Render", "FollowSunDirection", g_cfg.followSunDirection);

    g_cfg.modelBlacklist       = ini.GetIntList("Models", "Blacklist");
    g_cfg.modelWhitelist       = ini.GetIntList("Models", "Whitelist");

    // Clamp to sane ranges so a bad INI value can't tank performance or
    // blow past the shadow pool the engine allocates internally.
    g_cfg.maxDistance          = std::max(5.0f, std::min(g_cfg.maxDistance, 200.0f));
    g_cfg.opacity              = std::max(0.0f, std::min(g_cfg.opacity, 255.0f));
    g_cfg.sizeScale            = std::max(0.1f, std::min(g_cfg.sizeScale, 5.0f));
    g_cfg.maxShadowsPerFrame   = std::max(1, std::min(g_cfg.maxShadowsPerFrame, 200));
    g_cfg.updateIntervalFrames = std::max(1, g_cfg.updateIntervalFrames);
}

// ---------------------------------------------------------------------
// Classification helpers
// ---------------------------------------------------------------------

// Rough heuristic for "is this a tree/vegetation model": checks the
// model's name for common SA vegetation prefixes. Cheaper and more
// portable than maintaining a huge hardcoded ID table, though the
// whitelist/blacklist in the INI always takes precedence.
static bool IsVegetationModel(const char *modelName) {
    static const char *kPrefixes[] = {
        "veg_", "tree", "palm", "bush", "plant", "cane", "kb_bush", "cedar"
    };
    for (auto prefix : kPrefixes) {
        if (strstr(modelName, prefix) != nullptr)
            return true;
    }
    return false;
}

static bool ModelExplicitlyBlacklisted(int modelId) {
    return std::find(g_cfg.modelBlacklist.begin(), g_cfg.modelBlacklist.end(), modelId)
           != g_cfg.modelBlacklist.end();
}

static bool ModelExplicitlyWhitelisted(int modelId) {
    if (g_cfg.modelWhitelist.empty())
        return true; // empty whitelist = no restriction
    return std::find(g_cfg.modelWhitelist.begin(), g_cfg.modelWhitelist.end(), modelId)
           != g_cfg.modelWhitelist.end();
}

// Decide whether an entity qualifies for a synthetic shadow, and
// classify it (tree vs generic object) so the render step can pick an
// appropriate blob size/shape.
static bool ClassifyEntity(CEntity *entity, bool &outIsTree) {
    if (!entity || !entity->m_pModelInfo)
        return false;

    int modelId = entity->m_nModelIndex;
    if (ModelExplicitlyBlacklisted(modelId) || !ModelExplicitlyWhitelisted(modelId))
        return false;

    const char *name = CModelInfo::GetModelInfo(modelId)->GetModelName(); // TODO: verify accessor name in your SDK snapshot
    bool isTree = IsVegetationModel(name);

    if (isTree && !g_cfg.shadowTrees)
        return false;

    bool isBuilding = entity->m_nType == ENTITY_TYPE_BUILDING; // TODO: verify enum name
    if (isBuilding && !g_cfg.shadowBuildings)
        return false;

    if (!isTree && !isBuilding && !g_cfg.shadowSmallObjects)
        return false;

    outIsTree = isTree;
    return true;
}

// ---------------------------------------------------------------------
// World scan: refresh the candidate list around the camera
// ---------------------------------------------------------------------
static void RefreshCandidates() {
    g_candidates.clear();

    CVector camPos = TheCamera.GetPosition(); // TODO: verify accessor per SDK

    // Walk the object pool (trees/props) and building pool, filtering by
    // distance up front so classification only runs on nearby entities.
    CPools::GetObjectPool()->ForAllActive([&](CObject *obj) {
        float dist = (obj->GetPosition() - camPos).Magnitude();
        if (dist > g_cfg.maxDistance)
            return;

        bool isTree = false;
        if (ClassifyEntity(obj, isTree)) {
            float radius = obj->GetBoundRadius() * g_cfg.sizeScale; // TODO: verify accessor
            g_candidates.push_back({obj, radius, isTree});
        }
    });

    if (g_cfg.shadowBuildings) {
        CPools::GetBuildingPool()->ForAllActive([&](CBuilding *bld) {
            float dist = (bld->GetPosition() - camPos).Magnitude();
            if (dist > g_cfg.maxDistance)
                return;

            bool isTree = false;
            if (ClassifyEntity(bld, isTree)) {
                float radius = bld->GetBoundRadius() * g_cfg.sizeScale;
                g_candidates.push_back({bld, radius, false});
            }
        });
    }

    // Nearest-first, then cap to the configured per-frame budget so a
    // dense forest doesn't flood the shadow pool.
    std::sort(g_candidates.begin(), g_candidates.end(),
              [&](const ShadowCandidate &a, const ShadowCandidate &b) {
                  float da = (a.entity->GetPosition() - camPos).MagnitudeSqr();
                  float db = (b.entity->GetPosition() - camPos).MagnitudeSqr();
                  return da < db;
              });

    if ((int)g_candidates.size() > g_cfg.maxShadowsPerFrame)
        g_candidates.resize(g_cfg.maxShadowsPerFrame);
}

// ---------------------------------------------------------------------
// Render step: feed each candidate into the built-in shadow system
// ---------------------------------------------------------------------
static void RenderExtraShadows() {
    if (g_candidates.empty())
        return;

    // Reuse the same front/side shadow-direction vectors the engine
    // already computes for peds and vehicles each frame, so tree and
    // building shadows spread the same way (and rotate through the day)
    // as everything else the advanced shadow system draws.
    float frontX, frontY, sideX, sideY;
    if (g_cfg.followSunDirection) {
        frontX = CTimeCycle::m_fShadowFrontX; // TODO: verify field names
        frontY = CTimeCycle::m_fShadowFrontY;
        sideX  = CTimeCycle::m_fShadowSideX;
        sideY  = CTimeCycle::m_fShadowSideY;
    } else {
        // Static fallback direction (roughly NE), used if the engine
        // fields aren't available in your SDK build.
        frontX = 0.7f; frontY = 0.7f;
        sideX  = -0.7f; sideY  = 0.7f;
    }

    uint8_t alpha = (uint8_t)g_cfg.opacity;

    for (auto &c : g_candidates) {
        CVector pos = c.entity->GetPosition();

        float fx = frontX * c.radius;
        float fy = frontY * c.radius;
        float sx = sideX  * c.radius;
        float sy = sideY  * c.radius;

        // Trees get a slightly softer/darker round blob (mimics foliage
        // canopy shadow), other objects get the standard blob used for
        // small props. Swap SHADOW_ADDITIVE for a stencil/decal shadow
        // type if your SDK exposes one and you want harder edges.
        uint8_t shadowType = c.isTree ? SHADOW_TYPE_ADDITIVE : SHADOW_TYPE_DEFAULT; // TODO: verify enum names

        CShadows::StoreShadowToBeRendered(
            shadowType,
            &pos,
            &fx, &fy,
            &sx, &sy,
            0, 0, 0,      // RGB, 0/0/0 = standard dark blob shadow
            alpha,
            false,        // drawOnWater
            1.0f          // bilinear/softness factor
        );
    }
}

// ---------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------
class ShadowExtenderPlugin {
public:
    ShadowExtenderPlugin() {
        Events::initGameEvent += [] {
            LoadConfig();
        };

        // Re-read the INI on script reload if your loader supports a
        // "reload mods" hotkey/event; harmless no-op otherwise.
        Events::reloadScriptsEvent += [] {
            LoadConfig();
        };

        Events::gameProcessEvent += [] {
            if (!g_cfg.enabled)
                return;

            if ((g_frameCounter++ % g_cfg.updateIntervalFrames) == 0)
                RefreshCandidates();

            RenderExtraShadows();
        };
    }
} gShadowExtenderPlugin;

} // namespace ShadowExtender

