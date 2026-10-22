#pragma once

#include <array>

namespace AppSettings
{
    enum class ViewMode
    {
        List = 0,
        Thumbnails = 1
    };

    struct Data
    {
        std::array<char, 128> apiKey = {};
        std::array<char, 512> homesteadFolder = {};
        std::array<char, 512> guildHallFolder = {};
        bool rememberWindowState = true;
        bool windowVisible = true;
        bool showListingsWithoutXml = false;
        bool performanceMode = false;
        bool disableImages = false;
        ViewMode defaultView = ViewMode::Thumbnails;
        int resultsPerPage = 10;
    };

    void Initialize();
    void Shutdown();
    void Update();
    void MarkDirty();
    void SaveNow();
    Data& Get();
}
