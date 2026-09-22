#pragma once

#include <map>
#include <string>

struct Gw2AccountApiResult
{
    std::map<int, int> counts;
    std::string error;
};

namespace Gw2AccountApi
{
    Gw2AccountApiResult FetchHomesteadDecorationCounts(const std::string& apiKey);
}
