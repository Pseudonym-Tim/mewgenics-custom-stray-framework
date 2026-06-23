#pragma once

#include "game_runtime_types.h"
#include "config.h"

int SpawnCustomStrayAtHouse(const CustomStrayDefinition* def, uint32_t placementIndex);
int SpawnNativeRandomStrayAtHouse(uint32_t placementIndex);
int SpawnOneConfiguredExtraStrayAtHouse(uint32_t placementIndex);
int SpawnOneConfiguredReplacementStrayAtHouse(uint32_t placementIndex);
int SpawnWeightedFrameworkStraysAtHouse(Component* houseComponent, int32_t forceCount, int32_t ignoreChance);
void TriggerDebugStraySpawn(void);
int TriggerNativeHouseStrayGeneration(void);
Component* FindConfiguredHouseComponent(Scene* houseScene);
Scene* FindConfiguredHouseScene(void);