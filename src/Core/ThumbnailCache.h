#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct HomeListing;

namespace ThumbnailCache
{
    struct Statistics
    {
        size_t diskFiles = 0;
        size_t diskBytes = 0;
        size_t pendingDownloads = 0;
        size_t activeDownloads = 0;
        size_t gpuThumbnails = 0;
        size_t gpuDetailTextures = 0;
        size_t gpuBytes = 0;
        size_t pendingDecodes = 0;
        size_t activeDecodes = 0;
        size_t uploadedTextures = 0;
        size_t releasedTextures = 0;
        size_t failedTextures = 0;
    };

    void Initialize();
    void Shutdown();
    void Update();
    void Prefetch(const HomeListing& listing);
    void SetVisibleListings(const std::vector<const HomeListing*>& listings);
    void ClearVisibleTextures();
    void* GetThumbnail(const HomeListing& listing);
    void* GetDetail(const HomeListing& listing, bool highResolution);
    void ClearDetailTexture();
    bool HasFailed(const HomeListing& listing);

    void PrecacheAll(const std::vector<HomeListing>& listings);
    void CancelPrecache();
    bool IsPrecaching();
    size_t PrecacheCompleted();
    size_t PrecacheTotal();
    void ClearDiskCache();
    bool IsClearingDiskCache();
    const std::string& Status();
    Statistics GetStatistics();
}
