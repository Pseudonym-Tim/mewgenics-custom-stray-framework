#include "spawn_coordinator.h"
#include "config.h"
#include "mod_state.h"
#include "spawn.h"

/*
* The worker thread is intentionally limited to hotkey polling and waking the
* game's UI/main thread. CatData/Entity/HouseCat creation mutates game-owned
* containers and must never happen on this worker thread...
*/
static HANDLE g_spawnCoordinatorThread = NULL;
static HANDLE g_spawnCoordinatorStopEvent = NULL;
static HHOOK g_mainThreadMessageHook = NULL;
static volatile LONG g_spawnCoordinatorStarted = 0;
static volatile LONG g_mainThreadTickActive = 0;
static volatile LONG g_pendingDebugSpawnRequests = 0;
static volatile LONG g_pendingReloadRequests = 0;
static volatile LONG g_pendingNativeAppendRequests = 0;
static volatile LONG g_nativeAppendDueTick = 0;
static volatile LONG g_pendingStaggeredSpawnUnits = 0;
static volatile LONG g_nextStaggeredSpawnDueTick = 0;
static volatile LONG g_nextStaggeredPlacementIndex = 0;
static volatile LONG g_debugSequenceActive = 0;
static volatile LONG g_spawnTriggerVk = VK_F7;
static volatile LONG g_reloadTriggerVk = VK_F9;
static DWORD g_mainThreadId = 0U;

#define CSF_COORDINATOR_WAKE_MESSAGE WM_NULL

static char g_debugSequenceIds[CSF_MAX_DEBUG_SPAWNS][64];
static uint32_t g_debugSequenceCount = 0U;
static uint32_t g_debugSequenceCursor = 0U;
static uint32_t g_debugSequenceSpawnedCount = 0U;
static DWORD g_debugSequenceNextDueTick = 0U;

static BOOL CALLBACK FindProcessWindowProc(HWND window, LPARAM parameter)
{
    DWORD processId;
    DWORD* threadId;

    processId = 0U;
    threadId = (DWORD*)parameter;

    if (!threadId || !IsWindow(window))
    {
        return TRUE;
    }

    GetWindowThreadProcessId(window, &processId);

    if (processId != GetCurrentProcessId())
    {
        return TRUE;
    }

    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != NULL)
    {
        return TRUE;
    }

    *threadId = GetWindowThreadProcessId(window, NULL);
    return FALSE;
}

static DWORD FindProcessWindowThreadId(void)
{
    DWORD threadId;

    threadId = 0U;
    EnumWindows(FindProcessWindowProc, (LPARAM)&threadId);
    return threadId;
}

static void UpdateHotkeySnapshot(void)
{
    InterlockedExchange(&g_spawnTriggerVk, (LONG)g_frameworkConfig.spawnTriggerVk);
    InterlockedExchange(&g_reloadTriggerVk, (LONG)g_frameworkConfig.reloadTriggerVk);
}

static void WakeMainThread(void)
{
    DWORD threadId;

    threadId = g_mainThreadId;

    if (threadId != 0U)
    {
        PostThreadMessageW(threadId, CSF_COORDINATOR_WAKE_MESSAGE, 0U, 0);
    }
}

void QueueDebugStraySpawn(void)
{
    InterlockedIncrement(&g_pendingDebugSpawnRequests);
    WakeMainThread();
}

void QueueConfigReload(void)
{
    InterlockedExchange(&g_pendingReloadRequests, 1);
    WakeMainThread();
}

static int32_t PlanCustomSpawnCountForNativeEvent(void)
{
    int32_t attempts;
    int32_t planned;

    if (g_frameworkConfig.catPool == CSF_CAT_POOL_CUSTOM_ONLY && g_strayRegistry.count == 0U)
    {
        return 0;
    }

    attempts = g_frameworkConfig.rollsPerNativeStrayEvent;

    if (attempts > g_frameworkConfig.maxExtraCats)
    {
        attempts = g_frameworkConfig.maxExtraCats;
    }

    planned = 0;

    while (attempts > 0 && planned < g_frameworkConfig.maxExtraCats)
    {
        --attempts;

        if (planned < g_frameworkConfig.minExtraCatsPerNativeStrayEvent)
        {
            ++planned;
            continue;
        }

        if (((rand() % 100) + 1) <= g_frameworkConfig.chancePercent)
        {
            ++planned;
        }
    }

    return planned;
}

void QueueNativeStrayAppend(void)
{
    LONG dueTick;

    InterlockedIncrement(&g_pendingNativeAppendRequests);
    dueTick = (LONG)(GetTickCount() + (DWORD)g_frameworkConfig.nativeAppendDelayMs);
    InterlockedExchange(&g_nativeAppendDueTick, dueTick);
    WakeMainThread();

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Queued deferred custom stray append after native event: pendingEvents=%ld delayMs=%d intervalMs=%d", g_pendingNativeAppendRequests, g_frameworkConfig.nativeAppendDelayMs, g_frameworkConfig.nativeSpawnIntervalMs);
    }
}

static void ConvertDueNativeEventsToStaggeredSpawnUnits(void)
{
    LONG dueTick;
    DWORD nowTick;
    LONG pendingEvents;
    LONG totalPlanned;
    LONG eventIndex;

    if (g_pendingNativeAppendRequests <= 0)
    {
        return;
    }

    dueTick = g_nativeAppendDueTick;
    nowTick = GetTickCount();

    if ((LONG)(nowTick - (DWORD)dueTick) < 0)
    {
        return;
    }

    pendingEvents = InterlockedExchange(&g_pendingNativeAppendRequests, 0);

    if (pendingEvents <= 0)
    {
        return;
    }

    totalPlanned = 0;

    for (eventIndex = 0; eventIndex < pendingEvents; ++eventIndex)
    {
        totalPlanned += PlanCustomSpawnCountForNativeEvent();
    }

    if (totalPlanned > 0)
    {
        LONG previousPending;

        previousPending = InterlockedExchangeAdd(&g_pendingStaggeredSpawnUnits, totalPlanned);

        if (previousPending <= 0)
        {
            InterlockedExchange(&g_nextStaggeredPlacementIndex, 0);
        }

        InterlockedExchange(&g_nextStaggeredSpawnDueTick, (LONG)GetTickCount());
    }

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Converted %ld native stray event request(s) into %ld staggered extra spawn(s)!", pendingEvents, totalPlanned);
    }
}

static int ProcessOneStaggeredSpawnIfDue(void)
{
    LONG dueTick;
    DWORD nowTick;

    if (g_pendingStaggeredSpawnUnits <= 0)
    {
        return 0;
    }

    dueTick = g_nextStaggeredSpawnDueTick;
    nowTick = GetTickCount();

    if ((LONG)(nowTick - (DWORD)dueTick) < 0)
    {
        return 0;
    }

    if (InterlockedCompareExchange(&g_pendingStaggeredSpawnUnits, 0, 0) > 0)
    {
        int32_t spawnedCount;
        LONG remaining;
        uint32_t placementIndex;

        placementIndex = (uint32_t)InterlockedIncrement(&g_nextStaggeredPlacementIndex) - 1U;
        spawnedCount = SpawnOneConfiguredExtraStrayAtHouse(placementIndex);

        if (spawnedCount > 0)
        {
            remaining = InterlockedDecrement(&g_pendingStaggeredSpawnUnits);
        }
        else
        {
            // Retry after the interval...
            remaining = InterlockedCompareExchange(&g_pendingStaggeredSpawnUnits, 0, 0);
        }

        InterlockedExchange(&g_nextStaggeredSpawnDueTick, (LONG)(GetTickCount() + (DWORD)g_frameworkConfig.nativeSpawnIntervalMs));

        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Processed main-thread staggered extra stray spawn unit: spawned=%d placementIndex=%u remaining=%ld nextDelayMs=%d.", spawnedCount, placementIndex, remaining, g_frameworkConfig.nativeSpawnIntervalMs);
        }

        return 1;
    }

    return 0;
}

static void ResetDebugSequence(void)
{
    uint32_t i;

    InterlockedExchange(&g_debugSequenceActive, 0);
    g_debugSequenceCount = 0U;
    g_debugSequenceCursor = 0U;
    g_debugSequenceSpawnedCount = 0U;
    g_debugSequenceNextDueTick = 0U;

    for (i = 0U; i < CSF_MAX_DEBUG_SPAWNS; ++i)
    {
        g_debugSequenceIds[i][0] = '\0';
    }
}

static void AddDebugSequenceDefinition(const CustomStrayDefinition* def)
{
    if (!def || g_debugSequenceCount >= CSF_MAX_DEBUG_SPAWNS)
    {
        return;
    }

    _snprintf_s(g_debugSequenceIds[g_debugSequenceCount], sizeof(g_debugSequenceIds[0]), _TRUNCATE, "%s", def->id);
    ++g_debugSequenceCount;
}

static void BeginDebugSpawnSequence(void)
{
    uint32_t i;

    ResetDebugSequence();

    // Reload only on the game thread so registry/config writes cannot race spawns...
    LoadFrameworkConfig();
    UpdateHotkeySnapshot();
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
        for (i = 0U; i < g_debugSpawnConfig.explicitCount && g_debugSequenceCount < (uint32_t)g_debugSpawnConfig.count; ++i)
        {
            const CustomStrayDefinition* def;

            def = FindCustomStrayDefinition(g_debugSpawnConfig.explicitIds[i]);

            if (def)
            {
                AddDebugSequenceDefinition(def);
            }
            else if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Debug explicit cat id not found: %s", g_debugSpawnConfig.explicitIds[i]);
            }
        }
    }
    else if (g_debugSpawnConfig.mode == CSF_MODE_ALL)
    {
        for (i = 0U; i < g_strayRegistry.count && g_debugSequenceCount < (uint32_t)g_debugSpawnConfig.count; ++i)
        {
            AddDebugSequenceDefinition(&g_strayRegistry.entries[i]);
        }
    }
    else
    {
        const CustomStrayDefinition* selected[CSF_MAX_STRAYS];
        uint32_t selectedCount;
        int32_t attempts;

        selectedCount = 0U;
        attempts = g_debugSpawnConfig.count;

        for (i = 0U; i < CSF_MAX_STRAYS; ++i)
        {
            selected[i] = NULL;
        }

        while (attempts > 0 && g_debugSequenceCount < CSF_MAX_DEBUG_SPAWNS)
        {
            const CustomStrayDefinition* const* excluded;
            const CustomStrayDefinition* def;

            --attempts;
            excluded = g_frameworkConfig.allowDuplicateCatsPerEvent ? NULL : selected;
            def = PickWeightedCustomStray(excluded, selectedCount);

            if (!def)
            {
                break;
            }

            AddDebugSequenceDefinition(def);

            if (!g_frameworkConfig.allowDuplicateCatsPerEvent && selectedCount < CSF_MAX_STRAYS)
            {
                selected[selectedCount++] = def;
            }
        }
    }

    if (g_debugSequenceCount == 0U)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Debug spawn sequence has no eligible cats to spawn!");
        }

        return;
    }

    g_debugSequenceNextDueTick = GetTickCount();
    InterlockedExchange(&g_debugSequenceActive, 1);

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Queued %u debug stray spawn(s) for serialized main-thread creation at %d ms intervals.", g_debugSequenceCount, g_frameworkConfig.nativeSpawnIntervalMs);
    }
}

static int ProcessOneDebugSpawnIfDue(void)
{
    DWORD nowTick;
    const CustomStrayDefinition* def;
    int spawned;

    if (InterlockedCompareExchange(&g_debugSequenceActive, 0, 0) == 0)
    {
        return 0;
    }

    nowTick = GetTickCount();

    if ((LONG)(nowTick - g_debugSequenceNextDueTick) < 0)
    {
        return 0;
    }

    if (g_debugSequenceCursor >= g_debugSequenceCount)
    {
        InterlockedExchange(&g_debugSequenceActive, 0);
        return 0;
    }

    def = FindCustomStrayDefinition(g_debugSequenceIds[g_debugSequenceCursor]);
    spawned = def ? SpawnCustomStrayAtHouse(def, g_debugSequenceSpawnedCount) : 0;

    if (spawned)
    {
        ++g_debugSequenceSpawnedCount;
    }
    else if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Debug serialized spawn failed for id=%s", g_debugSequenceIds[g_debugSequenceCursor]);
    }

    ++g_debugSequenceCursor;
    g_debugSequenceNextDueTick = GetTickCount() + (DWORD)g_frameworkConfig.nativeSpawnIntervalMs;

    if (g_debugSequenceCursor >= g_debugSequenceCount)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Debug serialized spawn sequence finished: requested=%u spawned=%u.", g_debugSequenceCount, g_debugSequenceSpawnedCount);
        }

        InterlockedExchange(&g_debugSequenceActive, 0);
    }

    return 1;
}

static void ProcessPendingReload(void)
{
    if (InterlockedExchange(&g_pendingReloadRequests, 0) == 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_debugSequenceActive, 0, 0) != 0)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Cancelling active debug spawn sequence before config reload.");
        }

        ResetDebugSequence();
    }

    LoadFrameworkConfig();
    UpdateHotkeySnapshot();
    ReloadCustomStrayDefinitions();
    LoadDebugSpawnConfig();

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Reloaded custom stray framework definitions on the game thread!");
    }
}

static void ProcessPendingDebugRequest(void)
{
    if (InterlockedCompareExchange(&g_debugSequenceActive, 0, 0) != 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_pendingDebugSpawnRequests, 0, 0) <= 0)
    {
        return;
    }

    InterlockedDecrement(&g_pendingDebugSpawnRequests);
    BeginDebugSpawnSequence();
}

static void PumpSpawnCoordinatorOnGameThread(void)
{
    if (g_mainThreadId != 0U && GetCurrentThreadId() != g_mainThreadId)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_mainThreadTickActive, 1, 0) != 0)
    {
        return;
    }

    ProcessPendingReload();
    ProcessPendingDebugRequest();
    ConvertDueNativeEventsToStaggeredSpawnUnits();

    // At most one framework-created cat per pump to avoid burst mutation...
    if (!ProcessOneDebugSpawnIfDue())
    {
        ProcessOneStaggeredSpawnIfDue();
    }

    InterlockedExchange(&g_mainThreadTickActive, 0);
}

static LRESULT CALLBACK MainThreadGetMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0)
    {
        PumpSpawnCoordinatorOnGameThread();
    }

    return CallNextHookEx(g_mainThreadMessageHook, code, wParam, lParam);
}

static int CoordinatorHasPendingWork(void)
{
    return InterlockedCompareExchange(&g_pendingDebugSpawnRequests, 0, 0) > 0 || InterlockedCompareExchange(&g_pendingReloadRequests, 0, 0) > 0 || InterlockedCompareExchange(&g_pendingNativeAppendRequests, 0, 0) > 0 || InterlockedCompareExchange(&g_pendingStaggeredSpawnUnits, 0, 0) > 0 || InterlockedCompareExchange(&g_debugSequenceActive, 0, 0) != 0;
}

static DWORD WINAPI SpawnCoordinatorThreadProc(LPVOID parameter)
{
    HANDLE stopEvent;
    int previousSpawnTriggerPressed;
    int previousReloadTriggerPressed;

    stopEvent = (HANDLE)parameter;
    previousSpawnTriggerPressed = 0;
    previousReloadTriggerPressed = 0;

    while (WaitForSingleObject(stopEvent, 16U) == WAIT_TIMEOUT)
    {
        int spawnTriggerPressed;
        int reloadTriggerPressed;

        spawnTriggerPressed = ((GetAsyncKeyState((int)InterlockedCompareExchange(&g_spawnTriggerVk, 0, 0)) & 0x8000) != 0);
        reloadTriggerPressed = ((GetAsyncKeyState((int)InterlockedCompareExchange(&g_reloadTriggerVk, 0, 0)) & 0x8000) != 0);

        if (spawnTriggerPressed && !previousSpawnTriggerPressed)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Debug custom stray spawn hotkey detected; queueing for game thread!");
            }

            QueueDebugStraySpawn();
        }

        if (reloadTriggerPressed && !previousReloadTriggerPressed)
        {
            QueueConfigReload();
        }

        if (CoordinatorHasPendingWork())
        {
            WakeMainThread();
        }

        previousSpawnTriggerPressed = spawnTriggerPressed;
        previousReloadTriggerPressed = reloadTriggerPressed;
    }

    return 0U;
}

void StartSpawnCoordinator(void)
{
    if (InterlockedCompareExchange(&g_spawnCoordinatorStarted, 1, 0) != 0)
    {
        return;
    }

    DWORD windowThreadId;

    // HookSceneCreateEntity calls us on a thread known to mutate Scene safely...
    g_mainThreadId = GetCurrentThreadId();
    UpdateHotkeySnapshot();
    windowThreadId = FindProcessWindowThreadId();

    if (windowThreadId == g_mainThreadId)
    {
        g_mainThreadMessageHook = SetWindowsHookExW(WH_GETMESSAGE, MainThreadGetMessageHook, g_hModule, g_mainThreadId);
    }

    if (!g_mainThreadMessageHook)
    {
        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "No safe game-thread message pump available (sceneThread=%lu windowThread=%lu error=%lu); coordinator will retry on the next Scene::CreateEntity call.", (unsigned long)g_mainThreadId, (unsigned long)windowThreadId, GetLastError());
        }

        g_mainThreadId = 0U;
        InterlockedExchange(&g_spawnCoordinatorStarted, 0);
        return;
    }

    g_spawnCoordinatorStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!g_spawnCoordinatorStopEvent)
    {
        if (g_mainThreadMessageHook)
        {
            UnhookWindowsHookEx(g_mainThreadMessageHook);
            g_mainThreadMessageHook = NULL;
        }
        
        g_mainThreadId = 0U;
        InterlockedExchange(&g_spawnCoordinatorStarted, 0);
        return;
    }

    g_spawnCoordinatorThread = CreateThread(NULL, 0U, SpawnCoordinatorThreadProc, g_spawnCoordinatorStopEvent, 0U, NULL);

    if (g_spawnCoordinatorThread && g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Custom stray coordinator started: worker polls hotkeys, game-thread=%lu performs all spawn mutations through the message pump.", (unsigned long)g_mainThreadId);
    }

    if (!g_spawnCoordinatorThread)
    {
        CloseHandle(g_spawnCoordinatorStopEvent);
        g_spawnCoordinatorStopEvent = NULL;

        if (g_mainThreadMessageHook)
        {
            UnhookWindowsHookEx(g_mainThreadMessageHook);
            g_mainThreadMessageHook = NULL;
        }

        g_mainThreadId = 0U;
        InterlockedExchange(&g_spawnCoordinatorStarted, 0);
    }
}

void StopSpawnCoordinator(void)
{
    if (g_spawnCoordinatorStopEvent)
    {
        SetEvent(g_spawnCoordinatorStopEvent);
    }

    if (g_spawnCoordinatorThread)
    {
        WaitForSingleObject(g_spawnCoordinatorThread, 1000U);
        CloseHandle(g_spawnCoordinatorThread);
        g_spawnCoordinatorThread = NULL;
    }

    if (g_spawnCoordinatorStopEvent)
    {
        CloseHandle(g_spawnCoordinatorStopEvent);
        g_spawnCoordinatorStopEvent = NULL;
    }

    if (g_mainThreadMessageHook)
    {
        UnhookWindowsHookEx(g_mainThreadMessageHook);
        g_mainThreadMessageHook = NULL;
    }

    ResetDebugSequence();
    g_mainThreadId = 0U;
    InterlockedExchange(&g_pendingDebugSpawnRequests, 0);
    InterlockedExchange(&g_pendingReloadRequests, 0);
    InterlockedExchange(&g_pendingNativeAppendRequests, 0);
    InterlockedExchange(&g_pendingStaggeredSpawnUnits, 0);
    InterlockedExchange(&g_spawnCoordinatorStarted, 0);
}
