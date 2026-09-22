#include <Windows.h>

#include <atomic>
#include <cstddef>

#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "resource.h"

#include "Core/AppRuntime.h"
#include "Core/AppSettings.h"
#include "Core/Localization.h"
#include "Core/ThumbnailCache.h"
#include "Data/ProviderRegistry.h"
#include "UI/MainWindow.h"

namespace
{
    constexpr const char* AddonName = "Home Browser";
    constexpr const char* MainWindowName = "Home Browser";
    constexpr const char* DetailsWindowName = "Home Browser Details";
    constexpr const char* SettingsWindowName = "Home Browser Settings";
    constexpr const char* QuickAccessIdentifier = "HOMEBROWSER_QUICKACCESS";
    constexpr const char* QuickAccessTextureIdentifier = "HOMEBROWSER_QUICKACCESS_ICON";
    constexpr const char* SettingsTextureIdentifier = "HOMEBROWSER_SETTINGS_ICON";
    constexpr const char* ToggleInputBind = "KB_HOMEBROWSER_TOGGLE";

    AddonDefinition_t addonDefinition = {};
    AddonAPI_t* nexusApi = nullptr;
    HMODULE addonModule = nullptr;
    std::atomic_bool toggleRequested = false;

    void AddonLoad(AddonAPI_t* api);
    void AddonUnload();
    void AddonRender();
    void AddonOptions();
    void OnToggle(const char*, bool isRelease);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        addonModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    addonDefinition.Signature = 0x623CFD2A;
    addonDefinition.APIVersion = NEXUS_API_VERSION;
    addonDefinition.Name = AddonName;
    addonDefinition.Version.Major = 1;
    addonDefinition.Version.Minor = 0;
    addonDefinition.Version.Build = 0;
    addonDefinition.Version.Revision = 2;
    addonDefinition.Author = "Girbilcannon.8259";
    addonDefinition.Description = "Browse and download community Homestead and Guild Hall layouts.";
    addonDefinition.Load = AddonLoad;
    addonDefinition.Unload = AddonUnload;
    addonDefinition.Flags = AF_None;
    addonDefinition.Provider = UP_GitHub;
    addonDefinition.UpdateLink = "https://github.com/Girbilcannon/HomeBrowser";
    return &addonDefinition;
}

namespace
{
    void AddonLoad(AddonAPI_t* api)
    {
        nexusApi = api;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(nexusApi->ImguiContext));
        ImGui::SetAllocatorFunctions(
            reinterpret_cast<void* (*)(std::size_t, void*)>(nexusApi->ImguiMalloc),
            reinterpret_cast<void (*)(void*, void*)>(nexusApi->ImguiFree)
        );

        AppRuntime::Initialize(nexusApi);
        ThumbnailCache::Initialize();
        AppSettings::Initialize();
        Localization::Initialize(nexusApi);
        ProviderRegistry::Initialize();
        MainWindow::Initialize();

        nexusApi->Textures_GetOrCreateFromResource(QuickAccessTextureIdentifier, IDR_HOMEBROWSER_QUICKACCESS_ICON, addonModule);
        nexusApi->Textures_GetOrCreateFromResource(SettingsTextureIdentifier, IDR_HOMEBROWSER_SETTINGS_ICON, addonModule);
        nexusApi->InputBinds_RegisterWithString(ToggleInputBind, OnToggle, "");
        nexusApi->QuickAccess_Add(QuickAccessIdentifier, QuickAccessTextureIdentifier, QuickAccessTextureIdentifier, ToggleInputBind, AddonName);
        nexusApi->GUI_Register(RT_Render, AddonRender);
        nexusApi->GUI_Register(RT_OptionsRender, AddonOptions);
        nexusApi->GUI_RegisterCloseOnEscape(MainWindowName, &AppSettings::Get().windowVisible);
        nexusApi->GUI_RegisterCloseOnEscape(DetailsWindowName, MainWindow::DetailsVisiblePointer());
        nexusApi->GUI_RegisterCloseOnEscape(SettingsWindowName, MainWindow::SettingsVisiblePointer());
        nexusApi->Log(LOGL_INFO, AddonName, "Home Browser 1.0.0.2 loaded.");
    }

    void AddonUnload()
    {
        if (nexusApi == nullptr) return;
        nexusApi->GUI_DeregisterCloseOnEscape(SettingsWindowName);
        nexusApi->GUI_DeregisterCloseOnEscape(DetailsWindowName);
        nexusApi->GUI_DeregisterCloseOnEscape(MainWindowName);
        nexusApi->QuickAccess_Remove(QuickAccessIdentifier);
        nexusApi->InputBinds_Deregister(ToggleInputBind);
        nexusApi->GUI_Deregister(AddonRender);
        nexusApi->GUI_Deregister(AddonOptions);

        MainWindow::Shutdown();
        ProviderRegistry::Shutdown();
        ThumbnailCache::Shutdown();
        Localization::Shutdown();
        AppSettings::Shutdown();
        AppRuntime::Shutdown();
        nexusApi = nullptr;
    }

    void AddonRender()
    {
        if (toggleRequested.exchange(false, std::memory_order_acq_rel)) MainWindow::Toggle();
        AppSettings::Update();
        ProviderRegistry::Update();
        MainWindow::Render();
    }

    void AddonOptions()
    {
        MainWindow::RenderOptions();
    }

    void OnToggle(const char*, bool isRelease)
    {
        if (!isRelease) toggleRequested.store(true, std::memory_order_release);
    }
}
