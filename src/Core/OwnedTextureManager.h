#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace OwnedTextureManager
{
    struct ThumbnailSource
    {
        std::string key;
        std::filesystem::path path;
    };

    struct Statistics
    {
        size_t thumbnailTextures = 0;
        size_t detailTextures = 0;
        size_t queuedDecodes = 0;
        size_t activeDecodes = 0;
        size_t estimatedGpuBytes = 0;
        size_t uploadedTextures = 0;
        size_t releasedTextures = 0;
        size_t failedTextures = 0;
    };

    void Initialize();
    void Shutdown();
    void Update(bool performanceMode);

    void SetVisibleThumbnails(const std::vector<ThumbnailSource>& sources);
    void ClearVisibleThumbnails();
    void* GetThumbnail(const std::string& key);

    void SetDetailFromUrl(const std::string& key, const std::string& url);
    void SetDetailFromFile(const std::string& key, const std::filesystem::path& path);
    void ClearDetail();
    void* GetDetail(const std::string& key);

    void ClearAll();
    bool IsIdle();
    Statistics GetStatistics();
}
