#pragma once

#include <string>

#include "../nexus/Nexus.h"

namespace AppRuntime
{
    void Initialize(AddonAPI_t* api);
    void Shutdown();

    AddonAPI_t* GetApi();
    NexusLinkData_t* GetNexusLink();
    const std::string& GetAddonDirectory();
    void OpenUrl(const std::string& url);
}

