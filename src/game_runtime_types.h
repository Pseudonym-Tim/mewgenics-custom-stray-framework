#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mewjector.h"

#define MOD_NAME "Custom Stray Framework"

// Function RVAs (Relative to game module base from MJ_GetGameBase)...
#define RVA_CREATE_STRAY_CATDATA 0x0D68C0u // CatDatabase helper: alloc/init a new stray CatData using chosen sex...
#define CREATE_STRAY_CATDATA_STOLEN_BYTES 16u // Conservative hook prologue size for CatDatabase stray CatData creation...
#define RVA_SCENE_CREATE_ENTITY 0x962FB0u // Scene primitive entity constructor used before HouseCat bind...
#define RVA_SCENE_CREATE_HOUSECAT_I64 0x1F5420u // House Scene: Attach CatData to spawned HouseCat entity...
#define RVA_CATDATA_UNK_INIT 0x0B6A90u // CatData init routine used by custom-cat application path (reverse engineered)...
#define RVA_CUSTOM_CAT_LOOKUP 0x93EB10u // Lookup by key in custom_cats tree/root...
#define RVA_APPLY_CUSTOM_CATDATA 0x7378F0u // Copy template data from custom cat into CatData custom compartment...
#define RVA_INIT_NARROW_STRING 0x052AA0u // Construct NarrowString from ASCII literal/key text...
#define RVA_DESTROY_NARROW_STRING 0x052730u // NarrowString destructor for temporary key buffers...
#define RVA_LOCALIZE_NARROW_KEY 0x95E150u // Localization manager: Key -> localized WideString...
#define RVA_ASSIGN_WIDE_STRING 0x0636D0u // WideString assignment helper used after localization/name generation...
#define RVA_DESTROY_WIDE_STRING 0x052240u // WideString destructor for localized temporaries...
#define RVA_CHARACTER_GET_CURRENT_HP_FALLBACK 0x0D2950u // Safe HP accessor used when computing spawn-time derived stats...
#define RVA_HOUSE_GENERATE_STRAYS 0x1EDEC0u // Native House stray generation routine...

// Global/static data offsets (Absolute pointer at gameBase + offset)...
#define DATAOFF_LOCALIZATION_MANAGER 0x13BC290u // Singleton localization manager used for key->text resolution...
#define DATAOFF_MEWDIRECTOR_SINGLETON 0x13D1970u // MewDirector singleton used to find scenes/cat database...
#define DATAOFF_CUSTOM_CATS_ROOT 0x13BE710u // Root object that owns the custom-cat lookup container...

#define CUSTOM_CATS_OFFSET 0x7C8u // Offset from custom-cats root to backing collection/container...
#define CUSTOM_CAT_NODE_COUNT_OFFSET 0xA8u // Node-count field (Used for sanity checks when enumerating custom cats)...

// CatData in-struct offsets inferred from reversed constructors/apply routines...
#define CATDATA_CUSTOM_COMPARTMENT_OFFSET 0x60u // Sub-struct target passed to RVA_APPLY_CUSTOM_CATDATA...
#define CATDATA_GENDER_OFFSET 0x58u // Runtime/active sex value used by gameplay systems...
#define CATDATA_GENDER_ORIGINAL_OFFSET 0x5Cu // Original/default sex copy preserved by game logic...
#define CATDATA_LIBIDO_OFFSET 0xBB8u // House info displays low/mid/high libido from this value...
#define CATDATA_SEXUALITY_OFFSET 0xBC0u // Sexual orientation (0.0 straight, around 0.5 bi, 1.0 gay)...
#define CATDATA_LOVER_UID_OFFSET 0xBC8u // Relationship target uid...
#define CATDATA_LOVER_STRENGTH_OFFSET 0xBD0u // Lover relationship modifier...
#define CATDATA_RIVAL_UID_OFFSET 0xBD8u // Relationship target uid...
#define CATDATA_RIVAL_STRENGTH_OFFSET 0xBE0u // Rival relationship modifier...
#define CATDATA_AGGRESSION_OFFSET 0xBE8u // House info displays low/mid/high aggression from this value...
#define CATDATA_FERTILITY_OFFSET 0xBF0u // (Native breeding routine multiplies both parents' values for litter chance)...
#define CATDATA_FLAGS_OFFSET 0xBF8u // Carries special-stray style behavior flags...

#define CATDATA_BIRTH_DAY_OFFSET 0xC38u // Birth date of the cat...
#define CATDATA_DEATH_DAY_OFFSET 0xC40u // Death date of the cat...
#define MEWDIRECTOR_CURRENT_DAY_OFFSET 0x580u // Self explanatory, the current day count...

#define CATDATA_INBREEDING_OFFSET 0xC50u // House info inbreeding meter uses this value...
#define CATDATA_FLAG_NO_BREED 0x200000ULL // Disables breeding for configured/special-like strays...

// CatData payload layout for stats and abilities written by this mod...
#define CATDATA_STATS_HERITABLE_OFFSET 0x6F0u // Base inheritable stats block (STR/DEX/CON/INT/SPD/CHA/LCK)...
#define CATDATA_STATS_DELTA_LEVELLING_OFFSET 0x70Cu // Level-up delta stats applied on top of inheritable base...
#define CATDATA_STATS_DELTA_INJURIES_OFFSET 0x728u // Injury modifier delta stats (can reduce effective values)...
#define CATDATA_ACTIVES_BASIC_OFFSET 0x7D0u // Two core active-skill string slots...
#define CATDATA_ACTIVES_ACCESSIBLE_OFFSET 0x810u // Four accessible/learned active-skill string slots...
#define CATDATA_PASSIVE_0_OFFSET 0x910u // Passive slot #0 string key identifier...
#define CATDATA_PASSIVE_0_LEVEL_OFFSET 0x930u // Passive slot #0 upgrade level value...
#define CATDATA_PASSIVE_1_OFFSET 0x938u // Passive slot #1 string key identifier...
#define CATDATA_PASSIVE_1_LEVEL_OFFSET 0x958u // Passive slot #1 upgrade level value...
#define CATDATA_STRING_SIZE 0x20u
#define CATDATA_BASIC_ACTIVE_COUNT 2U
#define CATDATA_ACCESSIBLE_ACTIVE_COUNT 4U
#define CATDATA_PASSIVE_COUNT 2U

#define SCENE_CREATE_ENTITY_STOLEN_BYTES 16U
#define HOUSE_GENERATE_STRAYS_STOLEN_BYTES 17U
#define CONFIG_TEXT_CAPACITY 128U

/*
* 32 bytes...
* Small string capacity threshold: 15 chars...
*/
typedef struct NarrowString
{
    union
    {
        char inline_buf[16];
        char* heap_ptr;
    } storage;
    uint64_t size;
    uint64_t capacity;
} NarrowString;

/*
* Game wide string type used for selector labels...
* 32 bytes...
* Small string capacity threshold: 7 wchar_t code units...
*/
typedef struct WideString
{
    union
    {
        wchar_t inline_buf[8];
        wchar_t* heap_ptr;
    } storage;
    uint64_t size;
    uint64_t capacity;
} WideString;

typedef struct CatStats
{
    int32_t strength;
    int32_t dexterity;
    int32_t constitution;
    int32_t intelligence;
    int32_t speed;
    int32_t charisma;
    int32_t luck;
} CatStats;

typedef struct VectorPtr
{
    void** begin;
    void** end;
    void** capacity_end;
} VectorPtr;

typedef struct PodVectorPtr
{
    uint32_t capacity;
    uint32_t size;
    void** data;
} PodVectorPtr;

typedef struct ComponentVTable ComponentVTable;
typedef struct Component Component;
typedef struct Scene Scene;
typedef struct Director Director;
typedef struct MewDirector MewDirector;
typedef struct CatDatabase CatDatabase;
typedef struct CatData CatData;
typedef struct Entity Entity;
typedef struct HouseCat HouseCat;

struct ComponentVTable
{
    NarrowString* (__cdecl* GetObjectTypeSTR)(const Component* self, NarrowString* returnValue);
    int32_t (__cdecl* GetObjectType)(const Component* self);
};

struct Component
{
    const ComponentVTable* vtable;
};

struct Scene
{
    Director* ownerDirector;
    PodVectorPtr entities;
    PodVectorPtr* componentLists;
    uint8_t opaque0[1168];
    uint8_t doingSceneDestruction;
    uint8_t opaque1[7];
    NarrowString name;
};

struct Director
{
    VectorPtr scenes;
};

struct MewDirector
{
    uint8_t opaque0[0x28];
    Director* director;
    uint8_t opaque1[0x598 - 0x30];
    CatDatabase* cats;
};

struct CatData
{
    uint8_t opaque0[0x18];
    WideString name;
    uint8_t opaque1[0xC48 - 0x38];
    int64_t sqlKey;
};

typedef CatData* (__cdecl* fn_create_stray_catdata)(CatDatabase* self, void* unused1, int32_t sex);
typedef Entity* (__cdecl* fn_scene_create_entity)(Scene* self);
typedef HouseCat* (__cdecl* fn_scene_create_housecat_i64)(Scene* self, Entity* owner, int64_t* sqlKey);
typedef void (__fastcall* fn_catdata_unk_init)(CatData* self, NarrowString* customCatName, int32_t sex, int32_t flag);
typedef void* (__fastcall* fn_custom_cat_lookup)(void* customCatsRoot, NarrowString* key);
typedef void (__fastcall* fn_apply_custom_catdata)(void* catDataCustomCompartment, void* customCatNode);
typedef NarrowString* (__fastcall* fn_init_narrow_string_from_literal)(NarrowString* outString, const char* text);
typedef void (__fastcall* fn_destroy_narrow_string)(NarrowString* value);
typedef WideString* (__fastcall* fn_localize_narrow_key)(void* localizationManager, WideString* outString, NarrowString* key);
typedef WideString* (__fastcall* fn_assign_wide_string)(WideString* destination, const WideString* source);
typedef void (__fastcall* fn_destroy_wide_string)(WideString* value);
typedef int32_t (__cdecl* fn_character_get_current_hp_simple)(void* characterComponent);
typedef void (__fastcall* fn_house_generate_strays)(Component* houseComponent);