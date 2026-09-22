#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "AppRuntime.h"

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>

#pragma comment(lib, "Shell32.lib")

namespace
{
    AddonAPI_t* runtimeApi = nullptr;
    NexusLinkData_t* nexusLink = nullptr;
    std::string addonDirectory;
}

void AppRuntime::Initialize(AddonAPI_t* api)
{
    runtimeApi = api;
    nexusLink = runtimeApi == nullptr
        ? nullptr
        : static_cast<NexusLinkData_t*>(runtimeApi->DataLink_Get(DL_NEXUS_LINK));

    addonDirectory.clear();
    if (runtimeApi != nullptr && runtimeApi->Paths_GetAddonDirectory != nullptr)
    {
        const char* path = runtimeApi->Paths_GetAddonDirectory("HomeBrowser");
        if (path != nullptr)
        {
            addonDirectory = path;
        }
    }

    if (addonDirectory.empty())
    {
        addonDirectory = ".";
    }

    std::error_code error;
    std::filesystem::create_directories(addonDirectory, error);
}

void AppRuntime::Shutdown()
{
    addonDirectory.clear();
    nexusLink = nullptr;
    runtimeApi = nullptr;
}

AddonAPI_t* AppRuntime::GetApi()
{
    return runtimeApi;
}

NexusLinkData_t* AppRuntime::GetNexusLink()
{
    return nexusLink;
}

const std::string& AppRuntime::GetAddonDirectory()
{
    return addonDirectory;
}

void AppRuntime::OpenUrl(const std::string& url)
{
    if (!url.empty())
    {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

