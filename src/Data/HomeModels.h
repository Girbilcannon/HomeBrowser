#pragma once

#include <string>
#include <vector>

struct DecorationRequirement
{
    std::string name;
    int quantity = 0;
    int id = 0;
    std::string iconUrl;
};

struct HomeListing
{
    std::string providerId;
    std::string providerName;
    std::string uuid;
    std::string name;
    std::string creator;
    std::string map;
    std::vector<std::string> tags;
    std::string description;
    std::string sourceUrl;
    bool guildHall = false;
    int decorationCount = 0;
    int views = 0;
    int likes = 0;
    std::vector<DecorationRequirement> decorations;
    int mapId = 0;
    std::string thumbnailUrl;
    std::string socialThumbnailUrl;
    std::string downloadUrl;
    std::string updatedAt;
    std::string authorUrl;
    std::string publishedAt;
};

struct ProviderDescriptor
{
    std::string id;
    std::string name;
    float accent[3] = {};
    bool live = false;
};
