#pragma once

#include "../nexus/Nexus.h"

namespace Localization
{
    void Initialize(AddonAPI_t* api);
    void Shutdown();
    const char* Translate(const char* key);
}

#define HB_TEXT(key) Localization::Translate(key)

