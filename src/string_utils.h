#pragma once

#include "game_runtime_types.h"

const char* GetNarrowStringData(const NarrowString* value);
int StringEqualsLiteral(const NarrowString* value, const char* literal);
void InitSmallNarrowString(NarrowString* outString, const char* text);
int IsUnsetText(const char* value);
void SetCatDataNarrowSlot(NarrowString* destination, const char* text);