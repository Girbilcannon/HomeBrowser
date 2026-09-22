#pragma once

struct HomeListing;
struct Texture_t;

namespace ThumbnailCache
{
    void Initialize();
    void Shutdown();
    void Update();
    void Prefetch(const HomeListing& listing);
    Texture_t* GetThumbnail(const HomeListing& listing);
    bool HasFailed(const HomeListing& listing);
}
