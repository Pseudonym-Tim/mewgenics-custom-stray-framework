#include "spawn.h"
#include "cat_customization.h"
#include "config.h"
#include "mod_state.h"
#include "string_utils.h"

static volatile LONG g_isSpawning = 0;

#define CSF_STRAY_PLACEMENT_X_SPACING 1.0

static uint64_t OffsetRawCoordinateX(uint64_t baseX, uint32_t placementIndex)
{
    double x;
    double absX;

    if (placementIndex == 0U)
    {
        return baseX;
    }

    /*
    * Uhh, just use memcpy to avoid surprises, fall back to raw integer
    * spacing if the value does not look like a plausible coordinate double...
    */
    memcpy(&x, &baseX, sizeof(x));
    absX = x < 0.0 ? -x : x;

    if (x == x && absX <= 100000000.0 && (baseX == 0ULL || absX >= 0.000001))
    {
        x += ((double)placementIndex * CSF_STRAY_PLACEMENT_X_SPACING);
        memcpy(&baseX, &x, sizeof(baseX));
        return baseX;
    }

    return baseX + ((uint64_t)placementIndex * (uint64_t)CSF_STRAY_PLACEMENT_X_SPACING);
}

static int SpawnGuardBegin(void)
{
    return InterlockedCompareExchange(&g_isSpawning, 1, 0) == 0;
}

static void SpawnGuardEnd(void)
{
    InterlockedExchange(&g_isSpawning, 0);
}

static MewDirector* GetMewDirector(void)
{
    UINT_PTR gameBase;
    MewDirector** singletonSlot;

    if (!g_mj.GetGameBase)
    {
        return NULL;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return NULL;
    }

    singletonSlot = (MewDirector**)(gameBase + (UINT_PTR)DATAOFF_MEWDIRECTOR_SINGLETON);
    return singletonSlot ? *singletonSlot : NULL;
}

Scene* FindConfiguredHouseScene(void)
{
    MewDirector* mewDirector;
    void** iterator;

    mewDirector = GetMewDirector();

    if (!mewDirector || !mewDirector->director)
    {
        return NULL;
    }

    for (iterator = mewDirector->director->scenes.begin; iterator && iterator < mewDirector->director->scenes.end; ++iterator)
    {
        Scene* candidate = (Scene*)(*iterator);

        if (candidate && StringEqualsLiteral(&candidate->name, g_frameworkConfig.sceneName))
        {
            return candidate;
        }
    }

    return NULL;
}

Component* FindConfiguredHouseComponent(Scene* houseScene)
{
    uint32_t index;

    if (!houseScene || !houseScene->componentLists || !houseScene->componentLists->data)
    {
        return NULL;
    }

    for (index = 0U; index < houseScene->componentLists->size; ++index)
    {
        Component* candidate;
        NarrowString typeName;

        candidate = (Component*)houseScene->componentLists->data[index];

        if (!candidate || !candidate->vtable || !candidate->vtable->GetObjectTypeSTR)
        {
            continue;
        }

        memset(&typeName, 0, sizeof(typeName));
        candidate->vtable->GetObjectTypeSTR(candidate, &typeName);

        if (StringEqualsLiteral(&typeName, g_frameworkConfig.houseComponentType))
        {
            return candidate;
        }
    }

    return NULL;
}

static void PositionHouseCatNearHouse(HouseCat* houseCat, Component* houseComponent, uint32_t placementIndex)
{
    void* houseCatTransform;
    uint64_t baseX;
    uint64_t baseY;
    uint64_t placedX;

    if (!houseCat || !houseComponent)
    {
        return;
    }

    houseCatTransform = *(void**)((uint8_t*)houseCat + 0x40U);

    if (!houseCatTransform)
    {
        return;
    }

    /*
    * Spread cats horizontally by placementIndex so our multi-spawns do not stack directly on
    * top of each other! Otherwise known as, covid-esque social distancing...
    */
    baseX = *(uint64_t*)((uint8_t*)houseComponent + 0x90U);
    baseY = *(uint64_t*)((uint8_t*)houseComponent + 0x98U);

    placedX = OffsetRawCoordinateX(baseX, placementIndex);

    *(uint64_t*)((uint8_t*)houseCatTransform + 0x80U) = placedX;
    *(uint64_t*)((uint8_t*)houseCatTransform + 0x88U) = baseY;
    *(uint64_t*)((uint8_t*)houseCatTransform + 0x90U) = 0ULL;
}

static int SpawnCustomStrayAtHouseInternal(Scene* houseScene, Component* houseComponent, const CustomStrayDefinition* def, uint32_t placementIndex)
{
    UINT_PTR gameBase;
    MewDirector* mewDirector;
    fn_create_stray_catdata createStrayCatData;
    fn_scene_create_entity sceneCreateEntity;
    fn_scene_create_housecat_i64 sceneCreateHouseCat;
    CatData* cat;
    Entity* entity;
    HouseCat* houseCat;

    if (!def || !g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();
    mewDirector = GetMewDirector();

    if (!gameBase || !mewDirector || !mewDirector->cats || !houseScene || houseScene->doingSceneDestruction || !houseComponent)
    {
        return 0;
    }

    createStrayCatData = (fn_create_stray_catdata)(gameBase + (UINT_PTR)RVA_CREATE_STRAY_CATDATA);
    sceneCreateEntity = (fn_scene_create_entity)(gameBase + (UINT_PTR)RVA_SCENE_CREATE_ENTITY);
    sceneCreateHouseCat = (fn_scene_create_housecat_i64)(gameBase + (UINT_PTR)RVA_SCENE_CREATE_HOUSECAT_I64);

    cat = createStrayCatData(mewDirector->cats, NULL, CSF_NATIVE_STRAY_SPAWN_SEX);

    if (!cat)
    {
        return 0;
    }

    if (!ApplyNativeCustomCatTemplate(cat, def))
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Failed to apply custom cat template for id=%s template=%s; not spawning fallback stray!", def->id, def->templateName);
        }

        return 0;
    }

    ApplyStrayMetadata(cat, def);

    if (!def->safeMode)
    {
        ApplyConfiguredStatsAbilitiesAndPassives(cat, def);
    }
    else if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "SafeMode active for id=%s: skipped optional stats/abilities/personality writes!", def->id);
    }

    entity = sceneCreateEntity(houseScene);

    if (!entity)
    {
        return 0;
    }

    houseCat = sceneCreateHouseCat(houseScene, entity, &cat->sqlKey);

    if (!houseCat)
    {
        return 0;
    }

    PositionHouseCatNearHouse(houseCat, houseComponent, placementIndex);

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Spawned custom stray id=%s template=%s sqlKey=%lld placementIndex=%u", def->id, def->templateName, (long long)cat->sqlKey, placementIndex);
    }

    return 1;
}

int SpawnCustomStrayAtHouse(const CustomStrayDefinition* def, uint32_t placementIndex)
{
    Scene* houseScene;
    Component* houseComponent;
    int result;

    if (!SpawnGuardBegin())
    {
        return 0;
    }

    houseScene = FindConfiguredHouseScene();
    houseComponent = FindConfiguredHouseComponent(houseScene);
    result = SpawnCustomStrayAtHouseInternal(houseScene, houseComponent, def, placementIndex);

    SpawnGuardEnd();
    return result;
}

static int SpawnNativeRandomStrayAtHouseInternal(Scene* houseScene, Component* houseComponent, uint32_t placementIndex)
{
    UINT_PTR gameBase;
    MewDirector* mewDirector;
    fn_create_stray_catdata createStrayCatData;
    fn_scene_create_entity sceneCreateEntity;
    fn_scene_create_housecat_i64 sceneCreateHouseCat;
    CatData* cat;
    Entity* entity;
    HouseCat* houseCat;

    if (!g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();
    mewDirector = GetMewDirector();

    if (!gameBase || !mewDirector || !mewDirector->cats || !houseScene || houseScene->doingSceneDestruction || !houseComponent)
    {
        return 0;
    }

    createStrayCatData = (fn_create_stray_catdata)(gameBase + (UINT_PTR)RVA_CREATE_STRAY_CATDATA);
    sceneCreateEntity = (fn_scene_create_entity)(gameBase + (UINT_PTR)RVA_SCENE_CREATE_ENTITY);
    sceneCreateHouseCat = (fn_scene_create_housecat_i64)(gameBase + (UINT_PTR)RVA_SCENE_CREATE_HOUSECAT_I64);

    // Create an ordinary game-generated stray CatData without applying a framework custom-cat template...
    cat = createStrayCatData(mewDirector->cats, NULL, 3);

    if (!cat)
    {
        return 0;
    }

    entity = sceneCreateEntity(houseScene);

    if (!entity)
    {
        return 0;
    }

    houseCat = sceneCreateHouseCat(houseScene, entity, &cat->sqlKey);

    if (!houseCat)
    {
        return 0;
    }

    PositionHouseCatNearHouse(houseCat, houseComponent, placementIndex);

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Spawned native random extra stray sqlKey=%lld placementIndex=%u", (long long)cat->sqlKey, placementIndex);
    }

    return 1;
}

int SpawnNativeRandomStrayAtHouse(uint32_t placementIndex)
{
    Scene* houseScene;
    Component* houseComponent;
    int result;

    if (!SpawnGuardBegin())
    {
        return 0;
    }

    houseScene = FindConfiguredHouseScene();
    houseComponent = FindConfiguredHouseComponent(houseScene);
    result = SpawnNativeRandomStrayAtHouseInternal(houseScene, houseComponent, placementIndex);

    SpawnGuardEnd();
    return result;
}

static int SpawnOneConfiguredPoolStrayAtHouse(uint32_t placementIndex, const char* contextLabel)
{
    Scene* houseScene;
    Component* houseComponent;
    int spawnNative;
    int canCustom;
    int canNative;
    int result;

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

    if (!canCustom && !canNative)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s stray spawn skipped: No eligible native/custom pool!", contextLabel ? contextLabel : "Configured");
        }

        return 0;
    }

    spawnNative = 0;

    if (canNative && !canCustom)
    {
        spawnNative = 1;
    }
    else if (canNative && canCustom)
    {
        int totalWeight = g_frameworkConfig.nativeExtraWeight + g_frameworkConfig.customExtraWeight;
        int roll;

        if (totalWeight <= 0)
        {
            totalWeight = 100;
        }

        roll = (rand() % totalWeight) + 1;
        spawnNative = (roll <= g_frameworkConfig.nativeExtraWeight);
    }

    if (!SpawnGuardBegin())
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s stray spawn skipped because another framework spawn is already active!", contextLabel ? contextLabel : "Configured");
        }

        return 0;
    }

    houseScene = FindConfiguredHouseScene();
    houseComponent = FindConfiguredHouseComponent(houseScene);

    if (!houseScene || !houseComponent)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s stray spawn skipped: House scene/component not found!", contextLabel ? contextLabel : "Configured");
        }

        SpawnGuardEnd();
        return 0;
    }

    if (spawnNative)
    {
        result = SpawnNativeRandomStrayAtHouseInternal(houseScene, houseComponent, placementIndex);
    }
    else
    {
        const CustomStrayDefinition* def = PickWeightedCustomStrayForSpawn(NULL, 0U);
        result = def ? SpawnCustomStrayAtHouseInternal(houseScene, houseComponent, def, placementIndex) : 0;

        if (!def && canNative)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "%s custom stray selection fell back to native random stray because no non-special custom fallback was available!", contextLabel ? contextLabel : "Configured");
            }

            result = SpawnNativeRandomStrayAtHouseInternal(houseScene, houseComponent, placementIndex);
        }
        else if (!def && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s custom stray spawn failed: No eligible weighted non-special custom definition after special gate!", contextLabel ? contextLabel : "Configured");
        }
        else if (result > 0 && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s stray spawned custom cat id=%s placementIndex=%u", contextLabel ? contextLabel : "Configured", def->id, placementIndex);
        }
        else if (def && result <= 0 && canNative)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "%s custom stray failed for id=%s, falling back to native random stray!", contextLabel ? contextLabel : "Configured", def->id);
            }

            result = SpawnNativeRandomStrayAtHouseInternal(houseScene, houseComponent, placementIndex);
        }
        else if (def && result <= 0 && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "%s custom stray failed for id=%s and CatPool does not allow native fallback!", contextLabel ? contextLabel : "Configured", def->id);
        }
    }

    SpawnGuardEnd();
    return result;
}

int SpawnOneConfiguredExtraStrayAtHouse(uint32_t placementIndex)
{
    return SpawnOneConfiguredPoolStrayAtHouse(placementIndex, "Extra");
}

int SpawnOneConfiguredReplacementStrayAtHouse(uint32_t placementIndex)
{
    return SpawnOneConfiguredPoolStrayAtHouse(placementIndex, "Replacement");
}

int SpawnWeightedFrameworkStraysAtHouse(Component* houseComponent, int32_t forceCount, int32_t ignoreChance)
{
    Scene* houseScene;
    const CustomStrayDefinition* spawned[CSF_MAX_STRAYS];
    const CustomStrayDefinition* const* excludedList;
    int32_t attempts;
    int32_t spawnedCount;
    uint32_t excludedCount;
    uint32_t i;

    if (g_strayRegistry.count == 0U)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Native stray event hook ran, but no custom stray definitions loaded!");
        }

        return 0;
    }

    if (!SpawnGuardBegin())
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Custom stray spawn skipped because another framework spawn is already active!");
        }

        return 0;
    }

    houseScene = FindConfiguredHouseScene();

    if (!houseComponent)
    {
        houseComponent = FindConfiguredHouseComponent(houseScene);
    }

    if (!houseScene || !houseComponent)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Custom stray spawn skipped: House scene/component not found!");
        }

        SpawnGuardEnd();
        return 0;
    }

    attempts = forceCount >= 0 ? forceCount : g_frameworkConfig.rollsPerNativeStrayEvent;

    if (attempts > g_frameworkConfig.maxExtraCats && forceCount < 0)
    {
        attempts = g_frameworkConfig.maxExtraCats;
    }

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Custom stray roll started: definitions=%u attempts=%d chance=%d minExtra=%d maxExtra=%d force=%d ignoreChance=%d", g_strayRegistry.count, attempts, g_frameworkConfig.chancePercent, g_frameworkConfig.minExtraCatsPerNativeStrayEvent, g_frameworkConfig.maxExtraCats, forceCount, ignoreChance);
    }

    spawnedCount = 0;
    excludedCount = 0U;

    for (i = 0U; i < CSF_MAX_STRAYS; ++i)
    {
        spawned[i] = NULL;
    }

    while (attempts > 0 && spawnedCount < (forceCount >= 0 ? forceCount : g_frameworkConfig.maxExtraCats))
    {
        const CustomStrayDefinition* def;

        --attempts;

        if (!ignoreChance && forceCount < 0 && spawnedCount >= g_frameworkConfig.minExtraCatsPerNativeStrayEvent && ((rand() % 100) + 1) > g_frameworkConfig.chancePercent)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Custom stray roll missed chance gate!");
            }

            continue;
        }
        else if (!ignoreChance && forceCount < 0 && spawnedCount < g_frameworkConfig.minExtraCatsPerNativeStrayEvent && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Custom stray roll is forced by MinExtraCatsPerNativeStrayEvent!");
        }

        excludedList = g_frameworkConfig.allowDuplicateCatsPerEvent ? NULL : spawned;
        def = ignoreChance ? PickWeightedCustomStray(excludedList, excludedCount) : PickWeightedCustomStrayForSpawn(excludedList, excludedCount);

        if (!def)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Custom stray roll failed: No eligible weighted definition or non-special fallback after special gate!");
            }

            break;
        }

        if (SpawnCustomStrayAtHouseInternal(houseScene, houseComponent, def, (uint32_t)spawnedCount))
        {
            if (excludedCount < CSF_MAX_STRAYS)
            {
                spawned[excludedCount++] = def;
            }

            ++spawnedCount;
        }
    }

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Custom stray roll finished: spawned=%d!", spawnedCount);
    }

    SpawnGuardEnd();
    return spawnedCount;
}

int TriggerNativeHouseStrayGeneration(void)
{
    Scene* houseScene;
    Component* houseComponent;

    if (!g_origHouseGenerateStrays)
    {
        return 0;
    }

    houseScene = FindConfiguredHouseScene();
    houseComponent = FindConfiguredHouseComponent(houseScene);

    if (!houseComponent)
    {
        return 0;
    }

    g_origHouseGenerateStrays(houseComponent);
    return 1;
}

void TriggerDebugStraySpawn(void)
{
    int32_t spawnedCount;
    uint32_t i;

    LoadFrameworkConfig();
    ReloadCustomStrayDefinitions();
    LoadDebugSpawnConfig();

    if (!g_debugSpawnConfig.enabled)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Debug spawn hotkey ignored because debug_spawns.ini has Enabled=0!");
        }

        return;
    }

    if (g_debugSpawnConfig.useNativeStraySpawn)
    {
        TriggerNativeHouseStrayGeneration();
    }

    if (g_debugSpawnConfig.mode == CSF_MODE_EXPLICIT_LIST)
    {
        spawnedCount = 0;

        for (i = 0U; i < g_debugSpawnConfig.explicitCount && spawnedCount < g_debugSpawnConfig.count; ++i)
        {
            const CustomStrayDefinition* def = FindCustomStrayDefinition(g_debugSpawnConfig.explicitIds[i]);

            if (def && SpawnCustomStrayAtHouse(def, (uint32_t)spawnedCount))
            {
                ++spawnedCount;
            }
            else if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Debug explicit cat id not found or failed: %s", g_debugSpawnConfig.explicitIds[i]);
            }
        }

        return;
    }

    if (g_debugSpawnConfig.mode == CSF_MODE_ALL)
    {
        spawnedCount = 0;

        for (i = 0U; i < g_strayRegistry.count && spawnedCount < g_debugSpawnConfig.count; ++i)
        {
            if (SpawnCustomStrayAtHouse(&g_strayRegistry.entries[i], (uint32_t)spawnedCount))
            {
                ++spawnedCount;
            }
        }

        return;
    }

    SpawnWeightedFrameworkStraysAtHouse(NULL, g_debugSpawnConfig.count, 1);
}