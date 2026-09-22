#pragma once

#include "HomeModels.h"

#include <string>
#include <vector>

struct Gw2HomesApiResult
{
    std::vector<HomeListing> listings;
    int page = 1;
    int lastPage = 1;
    int total = 0;
    std::string error;
};

struct Gw2HomesDetailResult
{
    std::string uuid;
    std::vector<DecorationRequirement> decorations;
    int decorationCount = 0;
    std::string error;
};

namespace Gw2HomesApi
{
    Gw2HomesApiResult FetchListings(
        bool includeWithoutXml,
        int page = 1,
        int pageSize = 20,
        const std::string& orderBy = "likes");
    Gw2HomesApiResult FetchListingIndex(bool includeWithoutXml);
    Gw2HomesDetailResult FetchDetails(const std::string& homesteadId);
}
