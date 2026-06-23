#include "cat_customization.h"
#include "mod_state.h"
#include "string_utils.h"

static int64_t GetCurrentHouseDay()
{
    UINT_PTR gameBase;
    MewDirector* mewDirector;

    if (!g_mj.GetGameBase)
    {
        return -1LL;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return -1LL;
    }

    mewDirector = *(MewDirector**)(gameBase + (UINT_PTR)DATAOFF_MEWDIRECTOR_SINGLETON);

    if (!mewDirector)
    {
        return -1LL;
    }

    return *(int64_t*)((uint8_t*)mewDirector + MEWDIRECTOR_CURRENT_DAY_OFFSET);
}

static void ApplyConfiguredAge(CatData* cat, const CustomStrayDefinition* def)
{
    int64_t currentDay;
    int64_t configuredAge;

    if (!cat || !def || !def->hasAge)
    {
        return;
    }

    currentDay = GetCurrentHouseDay();

    if (currentDay < 0LL)
    {
        return;
    }

    configuredAge = (int64_t)def->age;

    if (configuredAge < 1LL)
    {
        configuredAge = 1LL;
    }

    // CatData:GetAge func basically does this: 
    // age = (death_day != -1 ? death_day : current_house_day) - birth_day;
    // So, for example, if we set Age=8, we can make it look like they were born 8 days ago from now...
    *(int64_t*)((uint8_t*)cat + CATDATA_BIRTH_DAY_OFFSET) = currentDay - configuredAge;
    *(int64_t*)((uint8_t*)cat + CATDATA_DEATH_DAY_OFFSET) = -1LL; // This should already be the set default value, but just in case...
}

static void SetCatNameInline(CatData* cat, const char* name)
{
    wchar_t nameW[CONFIG_TEXT_CAPACITY];
    size_t nameLength;
    size_t copyLength;
    int converted;

    if (!cat || !name)
    {
        return;
    }

    nameW[0] = L'\0';
    converted = MultiByteToWideChar(CP_UTF8, 0, name, -1, nameW, (int)(sizeof(nameW) / sizeof(nameW[0])));

    if (converted <= 0)
    {
        return;
    }

    nameLength = wcslen(nameW);
    copyLength = nameLength;

    /*
    * 7 chars payload + 1 null terminator before heap allocation...
    * Keep fallback names within inline capacity to avoid allocator coupling!
    */
    if (copyLength > 7U)
    {
        copyLength = 7U;
    }

    memset(&cat->name, 0, sizeof(cat->name));
    memcpy(cat->name.storage.inline_buf, nameW, copyLength * sizeof(wchar_t));
    cat->name.storage.inline_buf[copyLength] = L'\0';
    cat->name.size = (uint64_t)copyLength;
    cat->name.capacity = 7ULL;
}

static int SetCatNameFromLocalizationKey(CatData* cat, const char* localizationKey)
{
    UINT_PTR gameBase;
    void* localizationManager;
    NarrowString keyString;
    WideString localizedName;
    fn_init_narrow_string_from_literal initNarrowString;
    fn_destroy_narrow_string destroyNarrowString;
    fn_localize_narrow_key localizeNarrowKey;
    fn_assign_wide_string assignWideString;
    fn_destroy_wide_string destroyWideString;

    if (!cat || IsUnsetText(localizationKey) || !g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return 0;
    }

    /*
    * Native localization flow:
    * (Key literal -> NarrowString -> localized WideString -> assign to CatData)
    */
    localizationManager = (void*)(gameBase + (UINT_PTR)DATAOFF_LOCALIZATION_MANAGER);
    initNarrowString = (fn_init_narrow_string_from_literal)(gameBase + (UINT_PTR)RVA_INIT_NARROW_STRING);
    destroyNarrowString = (fn_destroy_narrow_string)(gameBase + (UINT_PTR)RVA_DESTROY_NARROW_STRING);
    localizeNarrowKey = (fn_localize_narrow_key)(gameBase + (UINT_PTR)RVA_LOCALIZE_NARROW_KEY);
    assignWideString = (fn_assign_wide_string)(gameBase + (UINT_PTR)RVA_ASSIGN_WIDE_STRING);
    destroyWideString = (fn_destroy_wide_string)(gameBase + (UINT_PTR)RVA_DESTROY_WIDE_STRING);

    memset(&keyString, 0, sizeof(keyString));
    memset(&localizedName, 0, sizeof(localizedName));

    initNarrowString(&keyString, localizationKey);
    localizeNarrowKey(localizationManager, &localizedName, &keyString);

    if (localizedName.size == 0ULL)
    {
        destroyNarrowString(&keyString);
        return 0;
    }

    assignWideString(&cat->name, &localizedName);
    destroyWideString(&localizedName);
    destroyNarrowString(&keyString);
    return 1;
}

int ApplyNativeCustomCatTemplate(CatData* cat, const CustomStrayDefinition* def)
{
    UINT_PTR gameBase;
    UINT_PTR customRootSlot;
    void* customCatsRoot;
    void* customCatNode;
    NarrowString customCatName;
    fn_custom_cat_lookup lookupCustomCat;
    fn_catdata_unk_init initCatData;
    fn_apply_custom_catdata applyCustomCatData;

    if (!cat || !def || IsUnsetText(def->templateName) || !g_mj.GetGameBase)
    {
        return 0;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return 0;
    }

    /*
    * DATAOFF_CUSTOM_CATS_ROOT points to global slot that stores root object...
    * Actual searchable custom cat container is at CUSTOM_CATS_OFFSET inside it...
    */
    customRootSlot = *(UINT_PTR*)(gameBase + (UINT_PTR)DATAOFF_CUSTOM_CATS_ROOT);

    if (!customRootSlot)
    {
        return 0;
    }

    customCatsRoot = (void*)(customRootSlot + (UINT_PTR)CUSTOM_CATS_OFFSET);
    lookupCustomCat = (fn_custom_cat_lookup)(gameBase + (UINT_PTR)RVA_CUSTOM_CAT_LOOKUP);
    initCatData = (fn_catdata_unk_init)(gameBase + (UINT_PTR)RVA_CATDATA_UNK_INIT);
    applyCustomCatData = (fn_apply_custom_catdata)(gameBase + (UINT_PTR)RVA_APPLY_CUSTOM_CATDATA);

    InitSmallNarrowString(&customCatName, def->templateName);
    customCatNode = lookupCustomCat(customCatsRoot, &customCatName);

    if (!customCatNode)
    {
        return 0;
    }

    // Empty means key exists but has no usable data...
    if (*(int32_t*)((uint8_t*)customCatNode + CUSTOM_CAT_NODE_COUNT_OFFSET) == 0)
    {
        return 0;
    }

    initCatData(cat, &customCatName, CSF_NATIVE_CUSTOM_CAT_INIT_SEX, CSF_NATIVE_CUSTOM_CAT_INIT_FLAG);
    applyCustomCatData((void*)((uint8_t*)cat + CATDATA_CUSTOM_COMPARTMENT_OFFSET), customCatNode);
    return 1;
}

void ApplyStrayMetadata(CatData* cat, const CustomStrayDefinition* def)
{
    if (!cat || !def)
    {
        return;
    }

    if (def->safeMode)
    {
        SetCatNameInline(cat, def->inlineNameFallback);
        return;
    }
    
    ApplyConfiguredAge(cat, def);

    if (!SetCatNameFromLocalizationKey(cat, def->localizationKey))
    {
        SetCatNameInline(cat, def->inlineNameFallback);
    }

    // Keep both runtime and original gender fields aligned with configured value...
    *(int32_t*)((uint8_t*)cat + CATDATA_GENDER_OFFSET) = def->gender;
    *(int32_t*)((uint8_t*)cat + CATDATA_GENDER_ORIGINAL_OFFSET) = def->gender;

    if (def->noBreed)
    {
        *(uint64_t*)((uint8_t*)cat + CATDATA_FLAGS_OFFSET) |= CATDATA_FLAG_NO_BREED;
    }
    else
    {
        *(uint64_t*)((uint8_t*)cat + CATDATA_FLAGS_OFFSET) &= ~CATDATA_FLAG_NO_BREED;
    }

    if (def->hasLibido)
    {
        *(double*)((uint8_t*)cat + CATDATA_LIBIDO_OFFSET) = def->libido;
    }

    if (def->hasSexuality)
    {
        *(double*)((uint8_t*)cat + CATDATA_SEXUALITY_OFFSET) = def->sexuality;
    }

    if (def->hasAggression)
    {
        *(double*)((uint8_t*)cat + CATDATA_AGGRESSION_OFFSET) = def->aggression;
    }

    if (def->hasFertility)
    {
        *(double*)((uint8_t*)cat + CATDATA_FERTILITY_OFFSET) = def->fertility;
    }

    if (def->hasInbreeding)
    {
        *(double*)((uint8_t*)cat + CATDATA_INBREEDING_OFFSET) = def->inbreeding;
    }
}

void ApplyConfiguredStatsAbilitiesAndPassives(CatData* cat, const CustomStrayDefinition* def)
{
    uint8_t* catBase;
    uint32_t index;

    if (!cat || !def)
    {
        return;
    }

    catBase = (uint8_t*)cat;
    *(CatStats*)(catBase + CATDATA_STATS_HERITABLE_OFFSET) = def->heritableStats;
    *(CatStats*)(catBase + CATDATA_STATS_DELTA_LEVELLING_OFFSET) = def->levellingStats;
    memset(catBase + CATDATA_STATS_DELTA_INJURIES_OFFSET, 0, sizeof(CatStats));

    for (index = 0U; index < CATDATA_BASIC_ACTIVE_COUNT; ++index)
    {
        NarrowString* slot;

        slot = (NarrowString*)(catBase + CATDATA_ACTIVES_BASIC_OFFSET + ((UINT_PTR)index * CATDATA_STRING_SIZE));

        if (def->basicAbilities[index][0] != '\0')
        {
            SetCatDataNarrowSlot(slot, def->basicAbilities[index]);
        }
    }

    for (index = 0U; index < CATDATA_ACCESSIBLE_ACTIVE_COUNT; ++index)
    {
        NarrowString* slot;

        slot = (NarrowString*)(catBase + CATDATA_ACTIVES_ACCESSIBLE_OFFSET + ((UINT_PTR)index * CATDATA_STRING_SIZE));

        if (def->accessibleAbilities[index][0] != '\0')
        {
            SetCatDataNarrowSlot(slot, def->accessibleAbilities[index]);
        }
    }

    if (def->passiveNames[0][0] != '\0')
    {
        SetCatDataNarrowSlot((NarrowString*)(catBase + CATDATA_PASSIVE_0_OFFSET), def->passiveNames[0]);
        *(int64_t*)(catBase + CATDATA_PASSIVE_0_LEVEL_OFFSET) = def->passiveLevels[0];
    }

    if (def->passiveNames[1][0] != '\0')
    {
        SetCatDataNarrowSlot((NarrowString*)(catBase + CATDATA_PASSIVE_1_OFFSET), def->passiveNames[1]);
        *(int64_t*)(catBase + CATDATA_PASSIVE_1_LEVEL_OFFSET) = def->passiveLevels[1];
    }
}