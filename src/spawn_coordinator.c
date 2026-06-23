#include "spawn_coordinator.h"
#include "config.h"
#include "mod_state.h"
#include "spawn.h"

static HANDLE g_spawnCoordinatorThread = NULL;
static HANDLE g_spawnCoordinatorStopEvent = NULL;
static volatile LONG g_spawnCoordinatorStarted = 0;
static volatile LONG g_pendingNativeAppendRequests = 0;
static volatile LONG g_nativeAppendDueTick = 0;
static volatile LONG g_pendingStaggeredSpawnUnits = 0;
static volatile LONG g_nextStaggeredSpawnDueTick = 0;
static volatile LONG g_nextStaggeredPlacementIndex = 0;

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

    if (g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Queued deferred custom stray append after native event: pendingEvents=%ld delayMs=%d intervalMs=%d", g_pendingNativeAppendRequests, g_frameworkConfig.nativeAppendDelayMs, g_frameworkConfig.nativeSpawnIntervalMs);
    }
}

static void ConvertDueNativeEventsToStaggeredSpawnUnits(void)
{
    LONG dueTick;
    DWORD nowTick;

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

    LONG pendingEvents;
    LONG totalPlanned;
    LONG eventIndex;

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

static void ProcessOneStaggeredSpawnIfDue(void)
{
    LONG dueTick;
    DWORD nowTick;

    if (g_pendingStaggeredSpawnUnits <= 0)
    {
        return;
    }

    dueTick = g_nextStaggeredSpawnDueTick;
    nowTick = GetTickCount();

    if ((LONG)(nowTick - (DWORD)dueTick) < 0)
    {
        return;
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
            // (Avoid retry loop if the House scene temporarily cannot accept another cat)...
            remaining = InterlockedCompareExchange(&g_pendingStaggeredSpawnUnits, 0, 0);
        }

        InterlockedExchange(&g_nextStaggeredSpawnDueTick, (LONG)(GetTickCount() + (DWORD)g_frameworkConfig.nativeSpawnIntervalMs));

        if (g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Processed staggered extra stray spawn unit: spawned=%d placementIndex=%u remaining=%ld nextDelayMs=%d.", spawnedCount, placementIndex, remaining, g_frameworkConfig.nativeSpawnIntervalMs);
        }
    }
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

        spawnTriggerPressed = ((GetAsyncKeyState(g_frameworkConfig.spawnTriggerVk) & 0x8000) != 0);
        reloadTriggerPressed = ((GetAsyncKeyState(g_frameworkConfig.reloadTriggerVk) & 0x8000) != 0);

        if (spawnTriggerPressed && !previousSpawnTriggerPressed)
        {
            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Debug custom stray spawn hotkey detected!");
            }

            TriggerDebugStraySpawn();
        }

        if (reloadTriggerPressed && !previousReloadTriggerPressed)
        {
            LoadFrameworkConfig();
            ReloadCustomStrayDefinitions();
            LoadDebugSpawnConfig();

            if (g_mj.Log)
            {
                g_mj.Log(MOD_NAME, "Reloaded custom stray framework definitions from hotkey!");
            }
        }

        ConvertDueNativeEventsToStaggeredSpawnUnits();
        ProcessOneStaggeredSpawnIfDue();

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

    g_spawnCoordinatorStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!g_spawnCoordinatorStopEvent)
    {
        InterlockedExchange(&g_spawnCoordinatorStarted, 0);
        return;
    }

    g_spawnCoordinatorThread = CreateThread(NULL, 0U, SpawnCoordinatorThreadProc, g_spawnCoordinatorStopEvent, 0U, NULL);

    if (g_spawnCoordinatorThread && g_mj.Log)
    {
        g_mj.Log(MOD_NAME, "Custom stray framework coordinator thread started!");
    }

    if (!g_spawnCoordinatorThread)
    {
        CloseHandle(g_spawnCoordinatorStopEvent);
        g_spawnCoordinatorStopEvent = NULL;
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

    InterlockedExchange(&g_spawnCoordinatorStarted, 0);
}