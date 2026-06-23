#include "string_utils.h"
#include "mod_state.h"

const char* GetNarrowStringData(const NarrowString* value)
{
    if (!value)
    {
        return NULL;
    }

    if (value->capacity > 15ULL)
    {
        return value->storage.heap_ptr;
    }

    return value->storage.inline_buf;
}

int StringEqualsLiteral(const NarrowString* value, const char* literal)
{
    const char* data;
    size_t literalLength;

    if (!value || !literal)
    {
        return 0;
    }

    data = GetNarrowStringData(value);

    if (!data)
    {
        return 0;
    }

    literalLength = strlen(literal);

    if (value->size != (uint64_t)literalLength)
    {
        return 0;
    }

    return memcmp(data, literal, literalLength) == 0;
}

void InitSmallNarrowString(NarrowString* outString, const char* text)
{
    size_t length;

    if (!outString)
    {
        return;
    }

    memset(outString, 0, sizeof(*outString));

    if (!text)
    {
        outString->storage.inline_buf[0] = '\0';
        outString->size = 0ULL;
        outString->capacity = 15ULL;
        return;
    }

    length = strlen(text);

    if (length > 15U)
    {
        length = 15U;
    }

    memcpy(outString->storage.inline_buf, text, length);
    outString->storage.inline_buf[length] = '\0';
    outString->size = (uint64_t)length;
    outString->capacity = 15ULL;
}

int IsUnsetText(const char* value)
{
    if (!value || value[0] == '\0')
    {
        return 1;
    }

    if (_stricmp(value, "none") == 0)
    {
        return 1;
    }

    return 0;
}

void SetCatDataNarrowSlot(NarrowString* destination, const char* text)
{
    UINT_PTR gameBase;
    fn_init_narrow_string_from_literal initNarrowString;
    fn_destroy_narrow_string destroyNarrowString;

    if (!destination || !g_mj.GetGameBase)
    {
        return;
    }

    gameBase = g_mj.GetGameBase();

    if (!gameBase)
    {
        return;
    }

    initNarrowString = (fn_init_narrow_string_from_literal)(gameBase + (UINT_PTR)RVA_INIT_NARROW_STRING);
    destroyNarrowString = (fn_destroy_narrow_string)(gameBase + (UINT_PTR)RVA_DESTROY_NARROW_STRING);
    destroyNarrowString(destination);

    if (IsUnsetText(text))
    {
        InitSmallNarrowString(destination, "");
        return;
    }

    initNarrowString(destination, text);
}