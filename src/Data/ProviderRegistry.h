#pragma once

#include "HomeModels.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ProviderRegistry
{
    enum class DetailLoadState { NotRequested, Loading, Loaded, Failed };
    enum class AccountLoadState { NotRequested, Loading, Loaded, Failed };

    void Initialize();
    void Shutdown();
    void Update();
    void RequestRefresh();
    void EnsurePageAhead(int visiblePage, int resultsPerPage);
    void RequestDetails(const std::string& homesteadId);
    void RetryDetails(const std::string& homesteadId);
    DetailLoadState DetailsState(const std::string& homesteadId);
    const std::string& DetailsError(const std::string& homesteadId);
    void RequestAccountDecorationCounts();
    void RetryAccountDecorationCounts();
    AccountLoadState AccountCountsState();
    const std::string& AccountCountsError();
    int AvailableDecorationCount(int decorationId);
    const std::vector<ProviderDescriptor>& Providers();
    const std::vector<HomeListing>& Listings();
    bool IsLoading();
    bool IsLoadingMore();
    bool IsLive();
    int TotalListings();
    uint64_t ListingsRevision();
    const std::string& Status();
}
