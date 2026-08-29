#pragma once

#include "game_runtime_types.h"

void StartSpawnCoordinator(void);
void StopSpawnCoordinator(void);
void QueueNativeStrayAppend(void);
void QueueDebugStraySpawn(void);
void QueueConfigReload(void);
