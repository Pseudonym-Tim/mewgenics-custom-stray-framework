#include "config.h"
#include "mod_state.h"
#include "string_utils.h"

static wchar_t g_moduleDirW[MAX_PATH] = { 0 };
static wchar_t g_frameworkIniPathW[MAX_PATH] = { 0 };
static wchar_t g_debugIniPathW[MAX_PATH] = { 0 };

FrameworkConfig g_frameworkConfig =
{
    VK_F7,
    VK_F9,
    0,
    1,
    CSF_NATIVE_MODE_APPEND_EXTRA,
    100,
    3,
    1,
    3,
    1,
    750,
    1250,
    CSF_CAT_POOL_MIXED,
    50,
    50,
    "House",
    "House",
    "custom_strays.ini",
    "debug_spawns.ini"
};

CustomStrayRegistry g_strayRegistry = { 0 };
DebugSpawnConfig g_debugSpawnConfig = { 1, 1, CSF_MODE_WEIGHTED_RANDOM, 0, { { 0 } }, 0U };

static void CopyText(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0U)
    {
        return;
    }

    _snprintf_s(dst, dstSize, _TRUNCATE, "%s", src ? src : "");
}

static void BuildModulePaths(void)
{
    wchar_t modulePathW[MAX_PATH];
    wchar_t* slash;

    g_moduleDirW[0] = L'\0';
    g_frameworkIniPathW[0] = L'\0';

    if (!g_hModule || !GetModuleFileNameW(g_hModule, modulePathW, MAX_PATH))
    {
        return;
    }

    wcsncpy(g_moduleDirW, modulePathW, MAX_PATH - 1U);
    g_moduleDirW[MAX_PATH - 1U] = L'\0';
    slash = wcsrchr(g_moduleDirW, L'\\');

    if (slash)
    {
        *slash = L'\0';
    }

    _snwprintf_s(g_frameworkIniPathW, MAX_PATH, _TRUNCATE, L"%s\\CustomStrayFramework.ini", g_moduleDirW);
}

static int32_t ReadIniIntFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, int32_t fallbackValue)
{
    wchar_t fallbackText[32];
    wchar_t valueText[64];

    _snwprintf_s(fallbackText, sizeof(fallbackText) / sizeof(fallbackText[0]), _TRUNCATE, L"%d", fallbackValue);
    valueText[0] = L'\0';

    if (!path || path[0] == L'\0')
    {
        return fallbackValue;
    }

    GetPrivateProfileStringW(section, key, fallbackText, valueText, (DWORD)(sizeof(valueText) / sizeof(valueText[0])), path);
    return _wtoi(valueText);
}

static int ReadIniOptionalIntFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, int32_t* outValue)
{
    wchar_t valueText[64];

    if(!outValue || !path || path[0] == L'\0')
    {
        return 0;
    }

    valueText[0] = L'\0';

    GetPrivateProfileStringW(section,  key, L"", valueText, (DWORD)(sizeof(valueText) / sizeof(valueText[0])), path);

    if(valueText[0] == L'\0')
    {
        return 0;
    }

    *outValue = _wtoi(valueText);
    return 1;
}

static int32_t ReadIniNativeStrayModeFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, int32_t fallbackValue)
{
    wchar_t valueText[64];

    valueText[0] = L'\0';

    if (!path || path[0] == L'\0')
    {
        return fallbackValue;
    }

    GetPrivateProfileStringW(section, key, L"", valueText, (DWORD)(sizeof(valueText) / sizeof(valueText[0])), path);

    if (valueText[0] == L'\0')
    {
        return fallbackValue;
    }

    if (_wcsicmp(valueText, L"AppendExtra") == 0 || _wcsicmp(valueText, L"Append") == 0 || _wcsicmp(valueText, L"Extra") == 0)
    {
        return CSF_NATIVE_MODE_APPEND_EXTRA;
    }
    
    if (_wcsicmp(valueText, L"ReplaceEach") == 0 || _wcsicmp(valueText, L"ReplaceAll") == 0 || _wcsicmp(valueText, L"ReplaceSingle") == 0 || _wcsicmp(valueText, L"Replace") == 0 || _wcsicmp(valueText, L"Replacement") == 0)
    {
        return CSF_NATIVE_MODE_REPLACE_EACH;
    }

    return _wtoi(valueText);
}

static int32_t ReadIniPoolModeFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, int32_t fallbackValue)
{
    wchar_t valueText[64];

    valueText[0] = L'\0';

    if (!path || path[0] == L'\0')
    {
        return fallbackValue;
    }

    GetPrivateProfileStringW(section, key, L"", valueText, (DWORD)(sizeof(valueText) / sizeof(valueText[0])), path);

    if (valueText[0] == L'\0')
    {
        return fallbackValue;
    }

    if (_wcsicmp(valueText, L"CustomOnly") == 0 || _wcsicmp(valueText, L"Custom") == 0)
    {
        return CSF_CAT_POOL_CUSTOM_ONLY;
    }

    if (_wcsicmp(valueText, L"NativeOnly") == 0 || _wcsicmp(valueText, L"Native") == 0 || _wcsicmp(valueText, L"BaseGame") == 0)
    {
        return CSF_CAT_POOL_NATIVE_ONLY;
    }

    if (_wcsicmp(valueText, L"Mixed") == 0 || _wcsicmp(valueText, L"Any") == 0)
    {
        return CSF_CAT_POOL_MIXED;
    }

    return _wtoi(valueText);
}

static int ReadIniOptionalDoubleFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, double* outValue)
{
    wchar_t valueText[64];

    if (!outValue || !path || path[0] == L'\0')
    {
        return 0;
    }

    valueText[0] = L'\0';
    GetPrivateProfileStringW(section, key, L"", valueText, (DWORD)(sizeof(valueText) / sizeof(valueText[0])), path);

    if (valueText[0] == L'\0')
    {
        return 0;
    }

    *outValue = wcstod(valueText, NULL);
    return 1;
}

static void ReadIniTextFrom(const wchar_t* path, const wchar_t* section, const wchar_t* key, const char* fallbackValue, char* outValue, size_t outValueSize)
{
    wchar_t fallbackTextW[CONFIG_TEXT_CAPACITY];
    wchar_t valueTextW[CONFIG_TEXT_CAPACITY];
    int converted;

    if (!outValue || outValueSize == 0U)
    {
        return;
    }

    fallbackTextW[0] = L'\0';
    valueTextW[0] = L'\0';

    if (fallbackValue)
    {
        MultiByteToWideChar(CP_UTF8, 0, fallbackValue, -1, fallbackTextW, (int)(sizeof(fallbackTextW) / sizeof(fallbackTextW[0])));
    }

    if (!path || path[0] == L'\0')
    {
        CopyText(outValue, outValueSize, fallbackValue);
        return;
    }

    GetPrivateProfileStringW(section, key, fallbackTextW, valueTextW, (DWORD)(sizeof(valueTextW) / sizeof(valueTextW[0])), path);
    converted = WideCharToMultiByte(CP_UTF8, 0, valueTextW, -1, outValue, (int)outValueSize, NULL, NULL);

    if (converted <= 0)
    {
        CopyText(outValue, outValueSize, fallbackValue);
    }
}

static void WPathToUtf8(const wchar_t* pathW, char* outValue, size_t outSize)
{
    int converted;

    if (!outValue || outSize == 0U)
    {
        return;
    }

    outValue[0] = '\0';

    converted = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, outValue, (int)outSize, NULL, NULL);

    if (converted <= 0)
    {
        outValue[0] = '\0';
    }
}

static void Utf8ToWide(const char* text, wchar_t* outValue, size_t outCount)
{
    int converted;

    if (!outValue || outCount == 0U)
    {
        return;
    }

    outValue[0] = L'\0';

    if (!text)
    {
        return;
    }

    converted = MultiByteToWideChar(CP_UTF8, 0, text, -1, outValue, (int)outCount);

    if (converted <= 0)
    {
        outValue[0] = L'\0';
    }
}

static void NormalizeDefaultDefinition(CustomStrayDefinition* def)
{
    memset(def, 0, sizeof(*def));
    def->enabled = 1;
    def->weight = 10;
    def->gender = 0;
    def->noBreed = 1;
    def->safeMode = 0;
    def->specialStray = 0;
    def->specialSpawnChancePercent = 100;

    def->hasAge = 0;
    def->age = 2;

    CopyText(def->templateName, sizeof(def->templateName), "TrailerCat");
    CopyText(def->localizationKey, sizeof(def->localizationKey), "none");
    CopyText(def->inlineNameFallback, sizeof(def->inlineNameFallback), "Stray");

    def->heritableStats.strength = 5;
    def->heritableStats.dexterity = 5;
    def->heritableStats.constitution = 5;
    def->heritableStats.intelligence = 5;
    def->heritableStats.speed = 5;
    def->heritableStats.charisma = 5;
    def->heritableStats.luck = 5;

    CopyText(def->basicAbilities[0], sizeof(def->basicAbilities[0]), "DefaultMove");
    CopyText(def->basicAbilities[1], sizeof(def->basicAbilities[1]), "BasicMelee");
}

static int StartsWithCatSection(const wchar_t* sectionNameW)
{
    return sectionNameW && wcsncmp(sectionNameW, L"Cat:", 4U) == 0;
}

static void ReadCatDefinitionFromIni(const wchar_t* pathW, const wchar_t* sectionW)
{
    CustomStrayDefinition* def;
    char id[64];
    uint32_t i;

    if (g_strayRegistry.count >= CSF_MAX_STRAYS || !pathW || !sectionW || !StartsWithCatSection(sectionW))
    {
        return;
    }

    id[0] = '\0';

    WideCharToMultiByte(CP_UTF8, 0, sectionW + 4, -1, id, (int)sizeof(id), NULL, NULL);

    if (id[0] == '\0')
    {
        return;
    }

    def = &g_strayRegistry.entries[g_strayRegistry.count];
    NormalizeDefaultDefinition(def);
    CopyText(def->id, sizeof(def->id), id);
    WPathToUtf8(pathW, def->sourcePath, sizeof(def->sourcePath));

    def->enabled = ReadIniIntFrom(pathW, sectionW, L"Enabled", def->enabled);
    def->weight = ReadIniIntFrom(pathW, sectionW, L"Weight", def->weight);
    def->gender = ReadIniIntFrom(pathW, sectionW, L"Gender", def->gender);
    def->hasAge = ReadIniOptionalIntFrom(pathW, sectionW, L"Age", &def->age);
    def->noBreed = ReadIniIntFrom(pathW, sectionW, L"NoBreed", def->noBreed);
    def->safeMode = ReadIniIntFrom(pathW, sectionW, L"SafeMode", def->safeMode);
    def->specialStray = ReadIniIntFrom(pathW, sectionW, L"SpecialStray", def->specialStray);
    def->specialSpawnChancePercent = ReadIniIntFrom(pathW, sectionW, L"SpecialSpawnChancePercent", def->specialSpawnChancePercent);
    def->specialSpawnChancePercent = ReadIniIntFrom(pathW, sectionW, L"SpecialSpawnChance", def->specialSpawnChancePercent);

    // Sanity checks...
    if (def->hasAge && def->age < 1) def->age = 1;
    if (def->specialStray != 0) def->specialStray = 1;
    if (def->specialSpawnChancePercent < 0) def->specialSpawnChancePercent = 0;
    if (def->specialSpawnChancePercent > 100) def->specialSpawnChancePercent = 100;

    def->hasLibido = ReadIniOptionalDoubleFrom(pathW, sectionW, L"Libido", &def->libido);
    def->hasSexuality = ReadIniOptionalDoubleFrom(pathW, sectionW, L"Sexuality", &def->sexuality);
    def->hasAggression = ReadIniOptionalDoubleFrom(pathW, sectionW, L"Aggression", &def->aggression);
    def->hasFertility = ReadIniOptionalDoubleFrom(pathW, sectionW, L"Fertility", &def->fertility);
    def->hasInbreeding = ReadIniOptionalDoubleFrom(pathW, sectionW, L"Inbreeding", &def->inbreeding);

    ReadIniTextFrom(pathW, sectionW, L"CustomCatName", def->templateName, def->templateName, sizeof(def->templateName));
    ReadIniTextFrom(pathW, sectionW, L"LocalizationKey", def->localizationKey, def->localizationKey, sizeof(def->localizationKey));
    ReadIniTextFrom(pathW, sectionW, L"InlineNameFallback", def->inlineNameFallback, def->inlineNameFallback, sizeof(def->inlineNameFallback));

    def->heritableStats.strength = ReadIniIntFrom(pathW, sectionW, L"Strength", def->heritableStats.strength);
    def->heritableStats.dexterity = ReadIniIntFrom(pathW, sectionW, L"Dexterity", def->heritableStats.dexterity);
    def->heritableStats.constitution = ReadIniIntFrom(pathW, sectionW, L"Constitution", def->heritableStats.constitution);
    def->heritableStats.intelligence = ReadIniIntFrom(pathW, sectionW, L"Intelligence", def->heritableStats.intelligence);
    def->heritableStats.speed = ReadIniIntFrom(pathW, sectionW, L"Speed", def->heritableStats.speed);
    def->heritableStats.charisma = ReadIniIntFrom(pathW, sectionW, L"Charisma", def->heritableStats.charisma);
    def->heritableStats.luck = ReadIniIntFrom(pathW, sectionW, L"Luck", def->heritableStats.luck);

    def->levellingStats.strength = ReadIniIntFrom(pathW, sectionW, L"StrengthDelta", def->levellingStats.strength);
    def->levellingStats.dexterity = ReadIniIntFrom(pathW, sectionW, L"DexterityDelta", def->levellingStats.dexterity);
    def->levellingStats.constitution = ReadIniIntFrom(pathW, sectionW, L"ConstitutionDelta", def->levellingStats.constitution);
    def->levellingStats.intelligence = ReadIniIntFrom(pathW, sectionW, L"IntelligenceDelta", def->levellingStats.intelligence);
    def->levellingStats.speed = ReadIniIntFrom(pathW, sectionW, L"SpeedDelta", def->levellingStats.speed);
    def->levellingStats.charisma = ReadIniIntFrom(pathW, sectionW, L"CharismaDelta", def->levellingStats.charisma);
    def->levellingStats.luck = ReadIniIntFrom(pathW, sectionW, L"LuckDelta", def->levellingStats.luck);

    for (i = 0U; i < CATDATA_BASIC_ACTIVE_COUNT; ++i)
    {
        wchar_t keyW[32];
        _snwprintf_s(keyW, sizeof(keyW) / sizeof(keyW[0]), _TRUNCATE, L"Basic%u", i);
        ReadIniTextFrom(pathW, sectionW, keyW, def->basicAbilities[i], def->basicAbilities[i], sizeof(def->basicAbilities[i]));
    }

    for (i = 0U; i < CATDATA_ACCESSIBLE_ACTIVE_COUNT; ++i)
    {
        wchar_t keyW[32];
        _snwprintf_s(keyW, sizeof(keyW) / sizeof(keyW[0]), _TRUNCATE, L"Accessible%u", i);
        ReadIniTextFrom(pathW, sectionW, keyW, def->accessibleAbilities[i], def->accessibleAbilities[i], sizeof(def->accessibleAbilities[i]));
    }

    for (i = 0U; i < CATDATA_PASSIVE_COUNT; ++i)
    {
        wchar_t keyW[32];
        wchar_t levelKeyW[32];
        _snwprintf_s(keyW, sizeof(keyW) / sizeof(keyW[0]), _TRUNCATE, L"Passive%u", i);
        _snwprintf_s(levelKeyW, sizeof(levelKeyW) / sizeof(levelKeyW[0]), _TRUNCATE, L"Passive%uLevel", i);
        ReadIniTextFrom(pathW, sectionW, keyW, def->passiveNames[i], def->passiveNames[i], sizeof(def->passiveNames[i]));
        def->passiveLevels[i] = (int64_t)ReadIniIntFrom(pathW, sectionW, levelKeyW, (int32_t)def->passiveLevels[i]);
    }

    if (def->enabled && def->weight > 0 && def->templateName[0] != '\0')
    {
        ++g_strayRegistry.count;
    }
}

static void LoadDefinitionFile(const wchar_t* pathW)
{
    wchar_t sections[8192];
    wchar_t* section;
    DWORD charsRead;

    if (!pathW || GetFileAttributesW(pathW) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    sections[0] = L'\0';
    charsRead = GetPrivateProfileSectionNamesW(sections, (DWORD)(sizeof(sections) / sizeof(sections[0])), pathW);

    if (charsRead == 0U)
    {
        return;
    }

    section = sections;

    while (*section)
    {
        if (StartsWithCatSection(section))
        {
            ReadCatDefinitionFromIni(pathW, section);
        }

        section += wcslen(section) + 1U;
    }
}

static void ScanDirectoryForDefinitionFile(const wchar_t* directoryW)
{
    wchar_t pathW[MAX_PATH];
    wchar_t definitionFileW[CONFIG_TEXT_CAPACITY];

    if (!directoryW || directoryW[0] == L'\0')
    {
        return;
    }

    Utf8ToWide(g_frameworkConfig.definitionFileName, definitionFileW, sizeof(definitionFileW) / sizeof(definitionFileW[0]));
    _snwprintf_s(pathW, MAX_PATH, _TRUNCATE, L"%s\\%s", directoryW, definitionFileW);
    LoadDefinitionFile(pathW);
}

static void ScanSiblingModFolders(void)
{
    wchar_t parentW[MAX_PATH];
    wchar_t searchW[MAX_PATH];
    WIN32_FIND_DATAW findData;
    HANDLE findHandle;
    wchar_t* slash;

    if (g_moduleDirW[0] == L'\0')
    {
        return;
    }

    ScanDirectoryForDefinitionFile(g_moduleDirW);

    wcsncpy(parentW, g_moduleDirW, MAX_PATH - 1U);
    parentW[MAX_PATH - 1U] = L'\0';
    slash = wcsrchr(parentW, L'\\');

    if (!slash)
    {
        return;
    }

    *slash = L'\0';

    _snwprintf_s(searchW, MAX_PATH, _TRUNCATE, L"%s\\*", parentW);
    findHandle = FindFirstFileW(searchW, &findData);

    if (findHandle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0)
        {
            wchar_t childW[MAX_PATH];
            _snwprintf_s(childW, MAX_PATH, _TRUNCATE, L"%s\\%s", parentW, findData.cFileName);
            ScanDirectoryForDefinitionFile(childW);
        }
    } 
    while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);
}

void LoadFrameworkConfig(void)
{
    BuildModulePaths();

    g_frameworkConfig.spawnTriggerVk = ReadIniIntFrom(g_frameworkIniPathW, L"Framework", L"HotkeyVk", g_frameworkConfig.spawnTriggerVk);
    g_frameworkConfig.reloadTriggerVk = ReadIniIntFrom(g_frameworkIniPathW, L"Framework", L"ReloadHotkeyVk", g_frameworkConfig.reloadTriggerVk);
    g_frameworkConfig.logHotkeyVk = ReadIniIntFrom(g_frameworkIniPathW, L"Framework", L"LogHotkeyVk", g_frameworkConfig.logHotkeyVk);
    g_frameworkConfig.enableNativeHook = ReadIniIntFrom(g_frameworkIniPathW, L"Framework", L"EnableNativeStrayHook", g_frameworkConfig.enableNativeHook);
    g_frameworkConfig.nativeStrayMode = ReadIniNativeStrayModeFrom(g_frameworkIniPathW, L"Framework", L"NativeStrayMode", g_frameworkConfig.nativeStrayMode);
    ReadIniTextFrom(g_frameworkIniPathW, L"Framework", L"SceneName", g_frameworkConfig.sceneName, g_frameworkConfig.sceneName, sizeof(g_frameworkConfig.sceneName));
    ReadIniTextFrom(g_frameworkIniPathW, L"Framework", L"HouseComponentType", g_frameworkConfig.houseComponentType, g_frameworkConfig.houseComponentType, sizeof(g_frameworkConfig.houseComponentType));
    ReadIniTextFrom(g_frameworkIniPathW, L"Framework", L"DefinitionFileName", g_frameworkConfig.definitionFileName, g_frameworkConfig.definitionFileName, sizeof(g_frameworkConfig.definitionFileName));
    ReadIniTextFrom(g_frameworkIniPathW, L"Framework", L"DebugFileName", g_frameworkConfig.debugFileName, g_frameworkConfig.debugFileName, sizeof(g_frameworkConfig.debugFileName));

    g_frameworkConfig.chancePercent = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"ChancePercent", g_frameworkConfig.chancePercent);
    g_frameworkConfig.maxExtraCats = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"MaxExtraCats", g_frameworkConfig.maxExtraCats);
    g_frameworkConfig.minExtraCatsPerNativeStrayEvent = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"MinExtraCatsPerNativeStrayEvent", g_frameworkConfig.minExtraCatsPerNativeStrayEvent);
    g_frameworkConfig.rollsPerNativeStrayEvent = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"RollsPerNativeStrayEvent", g_frameworkConfig.rollsPerNativeStrayEvent);
    g_frameworkConfig.allowDuplicateCatsPerEvent = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"AllowDuplicateCatsPerEvent", g_frameworkConfig.allowDuplicateCatsPerEvent);
    g_frameworkConfig.nativeAppendDelayMs = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"NativeAppendDelayMs", g_frameworkConfig.nativeAppendDelayMs);
    g_frameworkConfig.nativeSpawnIntervalMs = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"NativeSpawnIntervalMs", g_frameworkConfig.nativeSpawnIntervalMs);
    g_frameworkConfig.catPool = ReadIniPoolModeFrom(g_frameworkIniPathW, L"Spawn", L"ExtraCatPool", g_frameworkConfig.catPool);
    g_frameworkConfig.catPool = ReadIniPoolModeFrom(g_frameworkIniPathW, L"Spawn", L"CatPool", g_frameworkConfig.catPool);
    g_frameworkConfig.nativeExtraWeight = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"NativeExtraWeight", g_frameworkConfig.nativeExtraWeight);
    g_frameworkConfig.customExtraWeight = ReadIniIntFrom(g_frameworkIniPathW, L"Spawn", L"CustomExtraWeight", g_frameworkConfig.customExtraWeight);

    if (g_frameworkConfig.nativeStrayMode < CSF_NATIVE_MODE_APPEND_EXTRA || g_frameworkConfig.nativeStrayMode > CSF_NATIVE_MODE_REPLACE_EACH) g_frameworkConfig.nativeStrayMode = CSF_NATIVE_MODE_APPEND_EXTRA;
    if (g_frameworkConfig.chancePercent < 0) g_frameworkConfig.chancePercent = 0;
    if (g_frameworkConfig.chancePercent > 100) g_frameworkConfig.chancePercent = 100;
    if (g_frameworkConfig.maxExtraCats < 0) g_frameworkConfig.maxExtraCats = 0;
    if (g_frameworkConfig.minExtraCatsPerNativeStrayEvent < 0) g_frameworkConfig.minExtraCatsPerNativeStrayEvent = 0;
    if (g_frameworkConfig.minExtraCatsPerNativeStrayEvent > g_frameworkConfig.maxExtraCats) g_frameworkConfig.minExtraCatsPerNativeStrayEvent = g_frameworkConfig.maxExtraCats;
    if (g_frameworkConfig.rollsPerNativeStrayEvent < 0) g_frameworkConfig.rollsPerNativeStrayEvent = 0;
    if (g_frameworkConfig.rollsPerNativeStrayEvent < g_frameworkConfig.minExtraCatsPerNativeStrayEvent) g_frameworkConfig.rollsPerNativeStrayEvent = g_frameworkConfig.minExtraCatsPerNativeStrayEvent;
    if (g_frameworkConfig.nativeAppendDelayMs < 0) g_frameworkConfig.nativeAppendDelayMs = 0;
    if (g_frameworkConfig.nativeAppendDelayMs > 10000) g_frameworkConfig.nativeAppendDelayMs = 10000;
    if (g_frameworkConfig.nativeSpawnIntervalMs < 0) g_frameworkConfig.nativeSpawnIntervalMs = 0;
    if (g_frameworkConfig.nativeSpawnIntervalMs > 10000) g_frameworkConfig.nativeSpawnIntervalMs = 10000;
    if (g_frameworkConfig.catPool < CSF_CAT_POOL_CUSTOM_ONLY || g_frameworkConfig.catPool > CSF_CAT_POOL_MIXED) g_frameworkConfig.catPool = CSF_CAT_POOL_MIXED;
    if (g_frameworkConfig.nativeExtraWeight < 0) g_frameworkConfig.nativeExtraWeight = 0;
    if (g_frameworkConfig.customExtraWeight < 0) g_frameworkConfig.customExtraWeight = 0;

    if (g_frameworkConfig.nativeExtraWeight == 0 && g_frameworkConfig.customExtraWeight == 0)
    {
        g_frameworkConfig.nativeExtraWeight = 50;
        g_frameworkConfig.customExtraWeight = 50;
    }

    wchar_t debugFileW[CONFIG_TEXT_CAPACITY];
    Utf8ToWide(g_frameworkConfig.debugFileName, debugFileW, sizeof(debugFileW) / sizeof(debugFileW[0]));
    _snwprintf_s(g_debugIniPathW, MAX_PATH, _TRUNCATE, L"%s\\%s", g_moduleDirW, debugFileW);

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Loaded framework config: hook=%d nativeMode=%d chance=%d minExtra=%d maxExtra=%d rolls=%d delayMs=%d intervalMs=%d pool=%d nativeWeight=%d customWeight=%d definition=%s", g_frameworkConfig.enableNativeHook, g_frameworkConfig.nativeStrayMode, g_frameworkConfig.chancePercent, g_frameworkConfig.minExtraCatsPerNativeStrayEvent, g_frameworkConfig.maxExtraCats, g_frameworkConfig.rollsPerNativeStrayEvent, g_frameworkConfig.nativeAppendDelayMs, g_frameworkConfig.nativeSpawnIntervalMs, g_frameworkConfig.catPool, g_frameworkConfig.nativeExtraWeight, g_frameworkConfig.customExtraWeight, g_frameworkConfig.definitionFileName);
    }
}

void ReloadCustomStrayDefinitions(void)
{
    g_strayRegistry.count = 0U;
    ScanSiblingModFolders();

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Loaded %u custom stray definition(s)!", g_strayRegistry.count);
    }
}

void LoadDebugSpawnConfig(void)
{
    uint32_t i;
    char modeText[CONFIG_TEXT_CAPACITY];

    g_debugSpawnConfig.enabled = ReadIniIntFrom(g_debugIniPathW, L"Debug", L"Enabled", g_debugSpawnConfig.enabled);
    g_debugSpawnConfig.count = ReadIniIntFrom(g_debugIniPathW, L"Debug", L"Count", g_debugSpawnConfig.count);
    g_debugSpawnConfig.useNativeStraySpawn = ReadIniIntFrom(g_debugIniPathW, L"Debug", L"UseNativeStraySpawn", g_debugSpawnConfig.useNativeStraySpawn);
    ReadIniTextFrom(g_debugIniPathW, L"Debug", L"Mode", "WeightedRandom", modeText, sizeof(modeText));

    if (_stricmp(modeText, "ExplicitList") == 0)
    {
        g_debugSpawnConfig.mode = CSF_MODE_EXPLICIT_LIST;
    }
    else if (_stricmp(modeText, "All") == 0)
    {
        g_debugSpawnConfig.mode = CSF_MODE_ALL;
    }
    else
    {
        g_debugSpawnConfig.mode = CSF_MODE_WEIGHTED_RANDOM;
    }

    if (g_debugSpawnConfig.count < 0) g_debugSpawnConfig.count = 0;
    if (g_debugSpawnConfig.count > (int32_t)CSF_MAX_DEBUG_EXPLICIT) g_debugSpawnConfig.count = (int32_t)CSF_MAX_DEBUG_EXPLICIT;

    g_debugSpawnConfig.explicitCount = 0U;

    for (i = 0U; i < CSF_MAX_DEBUG_EXPLICIT; ++i)
    {
        wchar_t keyW[32];
        char value[64];
        _snwprintf_s(keyW, sizeof(keyW) / sizeof(keyW[0]), _TRUNCATE, L"Cat%u", i);
        ReadIniTextFrom(g_debugIniPathW, L"ExplicitList", keyW, "", value, sizeof(value));

        if (value[0] != '\0')
        {
            CopyText(g_debugSpawnConfig.explicitIds[g_debugSpawnConfig.explicitCount], sizeof(g_debugSpawnConfig.explicitIds[0]), value);
            ++g_debugSpawnConfig.explicitCount;
        }
    }
}

const CustomStrayDefinition* FindCustomStrayDefinition(const char* id)
{
    uint32_t i;

    if (!id || id[0] == '\0')
    {
        return NULL;
    }

    for (i = 0U; i < g_strayRegistry.count; ++i)
    {
        if (_stricmp(g_strayRegistry.entries[i].id, id) == 0)
        {
            return &g_strayRegistry.entries[i];
        }
    }

    return NULL;
}

static int IsExcluded(const CustomStrayDefinition* def, const CustomStrayDefinition* const* excluded, uint32_t excludedCount)
{
    uint32_t i;

    if (!def || !excluded || excludedCount == 0U)
    {
        return 0;
    }

    for (i = 0U; i < excludedCount; ++i)
    {
        if (excluded[i] == def)
        {
            return 1;
        }
    }

    return 0;
}

static const CustomStrayDefinition* PickWeightedCustomStrayBySpecialMode(const CustomStrayDefinition* const* excluded, uint32_t excludedCount, int32_t specialMode)
{
    int totalWeight;
    int roll;
    uint32_t i;

    totalWeight = 0;

    for (i = 0U; i < g_strayRegistry.count; ++i)
    {
        const CustomStrayDefinition* def = &g_strayRegistry.entries[i];

        if (!def->enabled || def->weight <= 0 || IsExcluded(def, excluded, excludedCount))
        {
            continue;
        }

        if (specialMode == 0 && def->specialStray)
        {
            continue;
        }

        if (specialMode == 1 && !def->specialStray)
        {
            continue;
        }

        totalWeight += def->weight;
    }

    if (totalWeight <= 0)
    {
        return NULL;
    }

    roll = (rand() % totalWeight) + 1;

    for (i = 0U; i < g_strayRegistry.count; ++i)
    {
        const CustomStrayDefinition* def = &g_strayRegistry.entries[i];

        if (!def->enabled || def->weight <= 0 || IsExcluded(def, excluded, excludedCount))
        {
            continue;
        }

        if (specialMode == 0 && def->specialStray)
        {
            continue;
        }

        if (specialMode == 1 && !def->specialStray)
        {
            continue;
        }

        roll -= def->weight;

        if (roll <= 0)
        {
            return def;
        }
    }

    return NULL;
}

const CustomStrayDefinition* PickWeightedCustomStray(const CustomStrayDefinition* const* excluded, uint32_t excludedCount)
{
    return PickWeightedCustomStrayBySpecialMode(excluded, excludedCount, 2);
}

const CustomStrayDefinition* PickWeightedNonSpecialCustomStray(const CustomStrayDefinition* const* excluded, uint32_t excludedCount)
{
    return PickWeightedCustomStrayBySpecialMode(excluded, excludedCount, 0);
}

const CustomStrayDefinition* PickWeightedCustomStrayForSpawn(const CustomStrayDefinition* const* excluded, uint32_t excludedCount)
{
    const CustomStrayDefinition* def;
    int roll;

    def = PickWeightedCustomStray(excluded, excludedCount);

    if (!def || !def->specialStray)
    {
        return def;
    }

    if (def->specialSpawnChancePercent >= 100)
    {
        return def;
    }

    roll = (rand() % 100) + 1;

    if (roll <= def->specialSpawnChancePercent)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Special stray chance passed: id=%s chance=%d roll=%d", def->id, def->specialSpawnChancePercent, roll);
        }

        return def;
    }

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Special stray chance failed: id=%s chance=%d roll=%d, trying non-special custom fallback!", def->id, def->specialSpawnChancePercent, roll);
    }

    return PickWeightedNonSpecialCustomStray(excluded, excludedCount);
}