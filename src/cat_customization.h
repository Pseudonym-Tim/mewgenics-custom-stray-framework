#pragma once

#include "game_runtime_types.h"
#include "config.h"

int ApplyNativeCustomCatTemplate(CatData* cat, const CustomStrayDefinition* def);
void ApplyStrayMetadata(CatData* cat, const CustomStrayDefinition* def);
void ApplyConfiguredStatsAbilitiesAndPassives(CatData* cat, const CustomStrayDefinition* def);