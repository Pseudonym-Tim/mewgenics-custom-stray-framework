#pragma once

#include "game_runtime_types.h"

#define CSF_MAX_STRAYS 128U
#define CSF_MAX_DEBUG_EXPLICIT 32U

#define CSF_MODE_WEIGHTED_RANDOM 0
#define CSF_MODE_EXPLICIT_LIST 1
#define CSF_MODE_ALL 2

#define CSF_CAT_POOL_CUSTOM_ONLY 0
#define CSF_CAT_POOL_NATIVE_ONLY 1
#define CSF_CAT_POOL_MIXED 2

#define CSF_EXTRA_POOL_CUSTOM_ONLY CSF_CAT_POOL_CUSTOM_ONLY
#define CSF_EXTRA_POOL_NATIVE_ONLY CSF_CAT_POOL_NATIVE_ONLY
#define CSF_EXTRA_POOL_MIXED CSF_CAT_POOL_MIXED

#define CSF_NATIVE_MODE_APPEND_EXTRA 0
#define CSF_NATIVE_MODE_REPLACE_EACH 1
#define CSF_NATIVE_MODE_REPLACE_SINGLE CSF_NATIVE_MODE_REPLACE_EACH

#define CSF_NATIVE_STRAY_SPAWN_SEX 3
#define CSF_NATIVE_CUSTOM_CAT_INIT_SEX 3
#define CSF_NATIVE_CUSTOM_CAT_INIT_FLAG 0

typedef struct FrameworkConfig
{
    int32_t spawnTriggerVk;
    int32_t reloadTriggerVk;
    int32_t logHotkeyVk;
    int32_t enableNativeHook;
    int32_t nativeStrayMode;
    int32_t chancePercent;
    int32_t maxExtraCats;
    int32_t minExtraCatsPerNativeStrayEvent;
    int32_t rollsPerNativeStrayEvent;
    int32_t allowDuplicateCatsPerEvent;
    int32_t nativeAppendDelayMs;
    int32_t nativeSpawnIntervalMs;
    int32_t catPool;
    int32_t nativeExtraWeight;
    int32_t customExtraWeight;
    char sceneName[CONFIG_TEXT_CAPACITY];
    char houseComponentType[CONFIG_TEXT_CAPACITY];
    char definitionFileName[CONFIG_TEXT_CAPACITY];
    char debugFileName[CONFIG_TEXT_CAPACITY];
} FrameworkConfig;

typedef struct CustomStrayDefinition
{
    char id[64];
    char sourcePath[MAX_PATH];
    int32_t enabled;
    int32_t weight;
    int32_t gender;
    int32_t noBreed;
    int32_t safeMode;
    int32_t specialStray;
    int32_t specialSpawnChancePercent;

    int32_t hasAge;
    int32_t age;

    int32_t hasLibido;
    int32_t hasSexuality;
    int32_t hasAggression;
    int32_t hasFertility;
    int32_t hasInbreeding;
    double libido;
    double sexuality;
    double aggression;
    double fertility;
    double inbreeding;
    char templateName[CONFIG_TEXT_CAPACITY];
    char localizationKey[CONFIG_TEXT_CAPACITY];
    char inlineNameFallback[CONFIG_TEXT_CAPACITY];
    CatStats heritableStats;
    CatStats levellingStats;
    char basicAbilities[CATDATA_BASIC_ACTIVE_COUNT][CONFIG_TEXT_CAPACITY];
    char accessibleAbilities[CATDATA_ACCESSIBLE_ACTIVE_COUNT][CONFIG_TEXT_CAPACITY];
    char passiveNames[CATDATA_PASSIVE_COUNT][CONFIG_TEXT_CAPACITY];
    int64_t passiveLevels[CATDATA_PASSIVE_COUNT];
} CustomStrayDefinition;

typedef struct CustomStrayRegistry
{
    CustomStrayDefinition entries[CSF_MAX_STRAYS];
    uint32_t count;
} CustomStrayRegistry;

typedef struct DebugSpawnConfig
{
    int32_t enabled;
    int32_t count;
    int32_t mode;
    int32_t useNativeStraySpawn;
    char explicitIds[CSF_MAX_DEBUG_EXPLICIT][64];
    uint32_t explicitCount;
} DebugSpawnConfig;

extern FrameworkConfig g_frameworkConfig;
extern CustomStrayRegistry g_strayRegistry;
extern DebugSpawnConfig g_debugSpawnConfig;

void LoadFrameworkConfig(void);
void ReloadCustomStrayDefinitions(void);
void LoadDebugSpawnConfig(void);

const CustomStrayDefinition* FindCustomStrayDefinition(const char* id);
const CustomStrayDefinition* PickWeightedCustomStray(const CustomStrayDefinition* const* excluded, uint32_t excludedCount);
const CustomStrayDefinition* PickWeightedNonSpecialCustomStray(const CustomStrayDefinition* const* excluded, uint32_t excludedCount);
const CustomStrayDefinition* PickWeightedCustomStrayForSpawn(const CustomStrayDefinition* const* excluded, uint32_t excludedCount);