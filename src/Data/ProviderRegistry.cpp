#include "ProviderRegistry.h"
#include "Gw2HomesApi.h"
#include "Gw2AccountApi.h"
#include "../Core/AppSettings.h"

#include <chrono>
#include <future>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>

namespace
{
    std::vector<ProviderDescriptor> providers;
    std::vector<HomeListing> listings;
    std::future<Gw2HomesApiResult> refreshJob;
    bool refreshQueued = false;
    bool refreshIsAppend = false;
    int nextApiPage = 1;
    int lastApiPage = 1;
    int totalListings = 0;
    bool paginationBlocked = false;
    std::future<Gw2HomesDetailResult> detailJob;
    std::string queuedDetailId;
    struct DetailStatus
    {
        ProviderRegistry::DetailLoadState state = ProviderRegistry::DetailLoadState::NotRequested;
        std::string error;
    };
    std::unordered_map<std::string, DetailStatus> detailStatuses;
    std::future<Gw2AccountApiResult> accountJob;
    ProviderRegistry::AccountLoadState accountState = ProviderRegistry::AccountLoadState::NotRequested;
    std::map<int, int> accountCounts;
    std::string accountError;
    std::string accountApiKey;
    std::string status;
    bool live = false;
}

void ProviderRegistry::Initialize()
{
    providers =
    {
        { "gw2homes", "gw2.homes", { 0.78f, 0.67f, 0.45f }, false }
    };

    listings.clear();
    status = "Building the gw2.homes gallery index...";
    RequestRefresh();
}

void ProviderRegistry::Shutdown()
{
    if (refreshJob.valid()) refreshJob.wait();
    if (detailJob.valid()) detailJob.wait();
    if (accountJob.valid()) accountJob.wait();
    listings.clear();
    providers.clear();
    status.clear();
    detailStatuses.clear();
    queuedDetailId.clear();
    live = false;
    refreshQueued = false;
    refreshIsAppend = false;
    nextApiPage = 1;
    lastApiPage = 1;
    totalListings = 0;
    paginationBlocked = false;
    accountState = AccountLoadState::NotRequested;
    accountCounts.clear();
    accountError.clear();
    accountApiKey.clear();
}

void ProviderRegistry::Update()
{
    if (refreshJob.valid() && refreshJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        Gw2HomesApiResult result = refreshJob.get();
        if (!result.error.empty())
        {
            if (live)
            {
                status = result.error + " Existing listings kept; use Refresh to retry.";
                if (refreshIsAppend) paginationBlocked = true;
            }
            else
            {
                status = result.error + " No gallery data is currently available; use Refresh to retry.";
                listings.clear();
            }
        }
        else
        {
            if (refreshIsAppend)
            {
                for (HomeListing& incoming : result.listings)
                {
                    const auto duplicate = std::find_if(listings.begin(), listings.end(), [&incoming](const HomeListing& existing)
                    {
                        return existing.uuid == incoming.uuid;
                    });
                    if (duplicate == listings.end()) listings.push_back(std::move(incoming));
                }
            }
            else
            {
                listings = std::move(result.listings);
                detailStatuses.clear();
            }
            live = true;
            if (!providers.empty()) providers.front().live = true;
            nextApiPage = result.page + 1;
            lastApiPage = result.lastPage;
            totalListings = result.total;
            status = "Connected - " + std::to_string(listings.size()) +
                " homesteads indexed. Thumbnails remain on-demand.";
        }
        refreshIsAppend = false;
        if (refreshQueued)
        {
            refreshQueued = false;
            RequestRefresh();
        }
    }

    if (accountJob.valid() && accountJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        Gw2AccountApiResult result = accountJob.get();
        accountError = result.error;
        if (result.error.empty())
        {
            accountCounts = std::move(result.counts);
            accountState = AccountLoadState::Loaded;
        }
        else
        {
            accountCounts.clear();
            accountState = AccountLoadState::Failed;
        }
    }

    if (detailJob.valid() && detailJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        Gw2HomesDetailResult result = detailJob.get();
        DetailStatus& detailStatus = detailStatuses[result.uuid];
        detailStatus.error = result.error;
        detailStatus.state = result.error.empty()
            ? DetailLoadState::Loaded
            : DetailLoadState::Failed;
        if (result.error.empty())
        {
            for (HomeListing& listing : listings)
            {
                if (listing.uuid != result.uuid) continue;
                listing.decorations = std::move(result.decorations);
                if (result.decorationCount > 0) listing.decorationCount = result.decorationCount;
                break;
            }
        }
    }

    if (!detailJob.valid() && !queuedDetailId.empty())
    {
        const std::string requestedId = queuedDetailId;
        queuedDetailId.clear();
        RequestDetails(requestedId);
    }
}

void ProviderRegistry::RequestRefresh()
{
    if (refreshJob.valid())
    {
        refreshQueued = true;
        return;
    }
    status = "Connecting to gw2.homes...";
    refreshIsAppend = false;
    nextApiPage = 1;
    lastApiPage = 1;
    totalListings = 0;
    paginationBlocked = false;
    const bool includeWithoutXml = AppSettings::Get().showListingsWithoutXml;
    refreshJob = std::async(std::launch::async, [includeWithoutXml]()
    {
        return Gw2HomesApi::FetchListingIndex(includeWithoutXml);
    });
}

void ProviderRegistry::EnsurePageAhead(int visiblePage, int resultsPerPage)
{
    if (!live || refreshJob.valid() || paginationBlocked || nextApiPage > lastApiPage || resultsPerPage <= 0) return;
    const size_t wanted = static_cast<size_t>((visiblePage + 1) * resultsPerPage);
    if (listings.size() >= wanted) return;
    const bool includeWithoutXml = AppSettings::Get().showListingsWithoutXml;
    const int page = nextApiPage;
    refreshIsAppend = true;
    status = "Loading more homesteads...";
    refreshJob = std::async(std::launch::async, [includeWithoutXml, page]()
    {
        return Gw2HomesApi::FetchListings(includeWithoutXml, page);
    });
}

void ProviderRegistry::RequestAccountDecorationCounts()
{
    const std::string apiKey = AppSettings::Get().apiKey.data();
    if (apiKey.empty()) return;
    if (accountApiKey != apiKey)
    {
        if (accountJob.valid()) return;
        accountApiKey = apiKey;
        accountCounts.clear();
        accountError.clear();
        accountState = AccountLoadState::NotRequested;
    }
    if (accountState != AccountLoadState::NotRequested) return;
    accountState = AccountLoadState::Loading;
    accountError.clear();
    accountJob = std::async(std::launch::async, [apiKey]()
    {
        return Gw2AccountApi::FetchHomesteadDecorationCounts(apiKey);
    });
}

void ProviderRegistry::RetryAccountDecorationCounts()
{
    if (accountJob.valid()) return;
    accountState = AccountLoadState::NotRequested;
    accountError.clear();
    RequestAccountDecorationCounts();
}

ProviderRegistry::AccountLoadState ProviderRegistry::AccountCountsState() { return accountState; }
const std::string& ProviderRegistry::AccountCountsError() { return accountError; }
int ProviderRegistry::AvailableDecorationCount(int decorationId)
{
    const auto found = accountCounts.find(decorationId);
    return found == accountCounts.end() ? 0 : found->second;
}

void ProviderRegistry::RequestDetails(const std::string& homesteadId)
{
    if (homesteadId.empty()) return;
    DetailStatus& detailStatus = detailStatuses[homesteadId];
    if (detailStatus.state == DetailLoadState::Loaded || detailStatus.state == DetailLoadState::Loading) return;
    if (detailJob.valid())
    {
        queuedDetailId = homesteadId;
        return;
    }
    detailStatus.state = DetailLoadState::Loading;
    detailStatus.error.clear();
    detailJob = std::async(std::launch::async, [homesteadId]() { return Gw2HomesApi::FetchDetails(homesteadId); });
}

void ProviderRegistry::RetryDetails(const std::string& homesteadId)
{
    if (homesteadId.empty()) return;
    DetailStatus& detailStatus = detailStatuses[homesteadId];
    detailStatus.state = DetailLoadState::NotRequested;
    detailStatus.error.clear();
    RequestDetails(homesteadId);
}

ProviderRegistry::DetailLoadState ProviderRegistry::DetailsState(const std::string& homesteadId)
{
    const auto iterator = detailStatuses.find(homesteadId);
    return iterator == detailStatuses.end() ? DetailLoadState::NotRequested : iterator->second.state;
}

const std::string& ProviderRegistry::DetailsError(const std::string& homesteadId)
{
    static const std::string empty;
    const auto iterator = detailStatuses.find(homesteadId);
    return iterator == detailStatuses.end() ? empty : iterator->second.error;
}

const std::vector<ProviderDescriptor>& ProviderRegistry::Providers()
{
    return providers;
}

const std::vector<HomeListing>& ProviderRegistry::Listings()
{
    return listings;
}

bool ProviderRegistry::IsLoading()
{
    return refreshJob.valid();
}

bool ProviderRegistry::IsLoadingMore() { return refreshJob.valid() && refreshIsAppend; }

bool ProviderRegistry::IsLive()
{
    return live;
}

int ProviderRegistry::TotalListings() { return totalListings; }

const std::string& ProviderRegistry::Status()
{
    return status;
}
