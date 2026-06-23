#include "config.h"
#include "spawn_coordinator.h"
#include "mod_state.h"
#include "game_runtime_types.h"
#include "spawn.h"
#include "cat_customization.h"

MewjectorAPI g_mj;
HMODULE g_hModule = NULL;
fn_scene_create_entity g_origSceneCreateEntity = NULL;
fn_house_generate_strays g_origHouseGenerateStrays = NULL;
fn_create_stray_catdata g_origCreateStrayCatData = NULL;

static __declspec(thread) int g_insideNativeHouseStrayGeneration = 0;


static int ShouldKeepNativeStrayForReplaceEach(void)
{
    int canCustom;
    int canNative;

    canCustom = (g_strayRegistry.count > 0U);
    canNative = 1;

    if (g_frameworkConfig.catPool == CSF_CAT_POOL_CUSTOM_ONLY)
    {
        canNative = 0;
    }
    else if (g_frameworkConfig.catPool == CSF_CAT_POOL_NATIVE_ONLY)
    {
        canCustom = 0;
    }

    if (!canCustom)
    {
        return 1;
    }

    if (!canNative)
    {
        return 0;
    }

    {
        int totalWeight;
        int roll;

        totalWeight = g_frameworkConfig.nativeExtraWeight + g_frameworkConfig.customExtraWeight;

        if (totalWeight <= 0)
        {
            totalWeight = 100;
        }

        roll = (rand() % totalWeight) + 1;
        return (roll <= g_frameworkConfig.nativeExtraWeight);
    }
}

static void TryCustomizeNativeStrayCatData(CatData* cat)
{
    const CustomStrayDefinition* def;

    if (!cat || !g_frameworkConfig.enableNativeHook || g_frameworkConfig.nativeStrayMode != CSF_NATIVE_MODE_REPLACE_EACH)
    {
        return;
    }

    if (ShouldKeepNativeStrayForReplaceEach())
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "ReplaceEach native stray candidate kept as ordinary base-game stray.");
        }

        return;
    }

    def = PickWeightedCustomStrayForSpawn(NULL, 0U);

    if (!def)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "ReplaceEach native stray candidate kept as base-game stray because no eligible custom definition was available.");
        }

        return;
    }

    if (!ApplyNativeCustomCatTemplate(cat, def))
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "ReplaceEach failed to apply custom cat template for id=%s template=%s; keeping native stray.", def->id, def->templateName);
        }

        return;
    }

    ApplyStrayMetadata(cat, def);

    if (!def->safeMode)
    {
        ApplyConfiguredStatsAbilitiesAndPassives(cat, def);
    }
    else if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "SafeMode active for id=%s during ReplaceEach: skipped optional stats/abilities/personality writes!", def->id);
    }

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "ReplaceEach converted native stray candidate to custom stray id=%s template=%s sqlKey=%lld!", def->id, def->templateName, (long long)cat->sqlKey);
    }
}

static CatData* __cdecl HookCreateStrayCatData(CatDatabase* self, void* unused1, int32_t sex)
{
    CatData* cat;

    if (!g_origCreateStrayCatData)
    {
        return NULL;
    }

    cat = g_origCreateStrayCatData(self, unused1, sex);

    if (g_insideNativeHouseStrayGeneration > 0)
    {
        TryCustomizeNativeStrayCatData(cat);
    }

    return cat;
}

static Entity* __fastcall HookSceneCreateEntity(Scene* scene)
{
    Entity* result;

    if (!g_origSceneCreateEntity)
    {
        return NULL;
    }

    result = g_origSceneCreateEntity(scene);
    StartSpawnCoordinator();
    return result;
}

static void __fastcall HookHouseGenerateStrays(Component* houseComponent)
{
    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Native House stray generation hook fired.");
    }

    if (g_origHouseGenerateStrays)
    {
        if (g_frameworkConfig.enableNativeHook && g_frameworkConfig.nativeStrayMode == CSF_NATIVE_MODE_REPLACE_EACH)
        {
            ++g_insideNativeHouseStrayGeneration;
            g_origHouseGenerateStrays(houseComponent);
            --g_insideNativeHouseStrayGeneration;
        }
        else
        {
            g_origHouseGenerateStrays(houseComponent);
        }
    }

    if (g_frameworkConfig.enableNativeHook && g_frameworkConfig.nativeStrayMode == CSF_NATIVE_MODE_APPEND_EXTRA)
    {
        /*
        * Do NOT create HouseCats from inside the native generation hook...
        * The game is still updating house_strays/generated-mercy state and may
        * iterate the same containers after this function returns... so, we just queue the
        * framework spawn for the coordinator thread instead...
        */
        QueueNativeStrayAppend();
    }
}

static void Initialize(void)
{
    void* trampoline;

    if (!MJ_Resolve(&g_mj))
    {
        return;
    }

    srand((unsigned int)(GetTickCount() ^ (DWORD)(uintptr_t)g_hModule));

    LoadFrameworkConfig();
    ReloadCustomStrayDefinitions();
    LoadDebugSpawnConfig();

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_SCENE_CREATE_ENTITY, SCENE_CREATE_ENTITY_STOLEN_BYTES, (void*)HookSceneCreateEntity, &trampoline, 32, MOD_NAME))
    {
        g_origSceneCreateEntity = (fn_scene_create_entity)trampoline;

        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Scene CreateEntity hook installed, coordinator will start once scenes are active!");
        }
    }
    else if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Failed to hook Scene CreateEntity!");
    }

    trampoline = NULL;

    if (g_frameworkConfig.enableNativeHook && g_mj.InstallHook(RVA_HOUSE_GENERATE_STRAYS, HOUSE_GENERATE_STRAYS_STOLEN_BYTES, (void*)HookHouseGenerateStrays, &trampoline, 32, MOD_NAME))
    {
        g_origHouseGenerateStrays = (fn_house_generate_strays)trampoline;

        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Native House stray generation hook installed!");
        }
    }
    else if (g_frameworkConfig.enableNativeHook && g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Failed to hook native House stray generation!");
    }

    trampoline = NULL;

    if (g_frameworkConfig.enableNativeHook && g_mj.InstallHook(RVA_CREATE_STRAY_CATDATA, CREATE_STRAY_CATDATA_STOLEN_BYTES, (void*)HookCreateStrayCatData, &trampoline, 32, MOD_NAME))
    {
        g_origCreateStrayCatData = (fn_create_stray_catdata)trampoline;

        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Native stray CatData creation hook installed for per-candidate replacement!");
        }
    }
    else if (g_frameworkConfig.enableNativeHook && g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Failed to hook native stray CatData creation!");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        Initialize();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        StopSpawnCoordinator();
    }

    return true;
}