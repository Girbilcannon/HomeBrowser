#include "AppSettings.h"

#include "AppRuntime.h"

#include <Windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#pragma comment(lib, "Ole32.lib")

namespace
{
    AppSettings::Data settings;
    bool dirty = false;
    DWORD dirtySince = 0;

    std::string ConfigPath()
    {
        return AppRuntime::GetAddonDirectory() + "\\HomeBrowser.json";
    }

    std::string DocumentsFolder()
    {
        PWSTR widePath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &widePath)))
        {
            const std::filesystem::path path(widePath);
            CoTaskMemFree(widePath);
            return path.string();
        }
        if (widePath != nullptr)
        {
            CoTaskMemFree(widePath);
        }
        return {};
    }

    std::string DefaultXmlFolder(const char* folder)
    {
        const std::string documents = DocumentsFolder();
        if (documents.empty())
        {
            return {};
        }
        return (std::filesystem::path(documents) / "Guild Wars 2" / folder).string();
    }

    template <size_t Size>
    void CopyToArray(std::array<char, Size>& destination, const std::string& value)
    {
        strncpy_s(destination.data(), destination.size(), value.c_str(), _TRUNCATE);
    }

    size_t FindValue(const std::string& json, const char* key)
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = json.find(token);
        if (position == std::string::npos)
        {
            return position;
        }
        position = json.find(':', position + token.size());
        return position == std::string::npos ? position : position + 1;
    }

    std::string FindString(const std::string& json, const char* key, const char* fallback)
    {
        size_t position = FindValue(json, key);
        if (position == std::string::npos || (position = json.find('"', position)) == std::string::npos)
        {
            return fallback;
        }

        std::string output;
        bool escaped = false;
        for (++position; position < json.size(); ++position)
        {
            const char character = json[position];
            if (escaped)
            {
                output += character;
                escaped = false;
            }
            else if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                return output;
            }
            else
            {
                output += character;
            }
        }
        return fallback;
    }

    bool FindBool(const std::string& json, const char* key, bool fallback)
    {
        const size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return fallback;
        }
        const size_t first = json.find_first_not_of(" \t\r\n", position);
        if (first == std::string::npos)
        {
            return fallback;
        }
        if (json.compare(first, 4, "true") == 0) return true;
        if (json.compare(first, 5, "false") == 0) return false;
        return fallback;
    }

    int FindInt(const std::string& json, const char* key, int fallback)
    {
        const size_t position = FindValue(json, key);
        if (position == std::string::npos)
        {
            return fallback;
        }
        try
        {
            return std::stoi(json.substr(position));
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::string Escape(const char* value)
    {
        std::string output;
        for (const char character : std::string(value == nullptr ? "" : value))
        {
            if (character == '\\' || character == '"') output += '\\';
            output += character;
        }
        return output;
    }

    void Load()
    {
        std::ifstream file(ConfigPath(), std::ios::binary);
        if (!file.is_open())
        {
            return;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string json = contents.str();

        CopyToArray(settings.apiKey, FindString(json, "apiKey", settings.apiKey.data()));
        CopyToArray(settings.homesteadFolder, FindString(json, "homesteadFolder", settings.homesteadFolder.data()));
        CopyToArray(settings.guildHallFolder, FindString(json, "guildHallFolder", settings.guildHallFolder.data()));
        settings.rememberWindowState = FindBool(json, "rememberWindowState", settings.rememberWindowState);
        settings.windowVisible = FindBool(json, "windowVisible", settings.windowVisible);
        settings.showListingsWithoutXml = FindBool(json, "showListingsWithoutXml", settings.showListingsWithoutXml);
        settings.defaultView = FindInt(json, "defaultView", 1) == 1
            ? AppSettings::ViewMode::Thumbnails
            : AppSettings::ViewMode::List;
        // The API fetches twenty records, while the UI intentionally presents ten per page.
        settings.resultsPerPage = 10;
        if (!settings.rememberWindowState)
        {
            settings.windowVisible = true;
        }
    }
}

void AppSettings::Initialize()
{
    settings = Data{};
    CopyToArray(settings.homesteadFolder, DefaultXmlFolder("Homesteads"));
    CopyToArray(settings.guildHallFolder, DefaultXmlFolder("GuildHalls"));
    Load();
    dirty = false;
    dirtySince = 0;
}

void AppSettings::Shutdown()
{
    SaveNow();
}

void AppSettings::Update()
{
    if (dirty && GetTickCount() - dirtySince >= 750)
    {
        SaveNow();
    }
}

void AppSettings::MarkDirty()
{
    dirty = true;
    dirtySince = GetTickCount();
}

void AppSettings::SaveNow()
{
    if (!dirty)
    {
        return;
    }

    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        return;
    }

    file << "{\n"
         << "  \"apiKey\": \"" << Escape(settings.apiKey.data()) << "\",\n"
         << "  \"homesteadFolder\": \"" << Escape(settings.homesteadFolder.data()) << "\",\n"
         << "  \"guildHallFolder\": \"" << Escape(settings.guildHallFolder.data()) << "\",\n"
         << "  \"rememberWindowState\": " << (settings.rememberWindowState ? "true" : "false") << ",\n"
         << "  \"windowVisible\": " << (settings.windowVisible ? "true" : "false") << ",\n"
         << "  \"showListingsWithoutXml\": " << (settings.showListingsWithoutXml ? "true" : "false") << ",\n"
         << "  \"defaultView\": " << static_cast<int>(settings.defaultView) << ",\n"
         << "  \"resultsPerPage\": " << settings.resultsPerPage << "\n"
         << "}\n";

    dirty = false;
}

AppSettings::Data& AppSettings::Get()
{
    return settings;
}
