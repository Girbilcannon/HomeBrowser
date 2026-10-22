#include "MainWindow.h"

#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/Localization.h"
#include "../Core/ThumbnailCache.h"
#include "../Core/XmlDownloader.h"
#include "../Data/ProviderRegistry.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{
    enum class BrowseMode { TopRated, Newest, Random, ByTag };
    enum class TypeFilter { All, Homesteads, GuildHalls };

    const ImVec4 Gold(0.88f, 0.69f, 0.36f, 1.0f);
    const ImVec4 MutedText(0.72f, 0.69f, 0.62f, 1.0f);
    constexpr std::array<const char*, 14> SeedTags =
    {
        "Cantha", "Cozy Café", "Elona", "Festival of the Four Winds",
        "Jumping Puzzle", "Kryta", "Lunar New Year", "Maguuma",
        "Nature", "Roller Beetle Race Track", "Shiverpeaks",
        "Super Adventure Box", "The Mists", "Wintersday"
    };

    std::array<char, 192> search = {};
    std::vector<std::string> selectedTags;
    BrowseMode browseMode = BrowseMode::TopRated;
    TypeFilter typeFilter = TypeFilter::All;
    int mapFilter = 0;
    int currentPage = 1;
    bool detailsVisible = false;
    bool settingsVisible = false;
    bool showMissing = false;
    size_t selectedListing = 0;
    AppSettings::ViewMode currentView = AppSettings::ViewMode::Thumbnails;
    float resultsContentCenterX = 0.0f;
    float resultsRightEdgeX = 0.0f;
    unsigned int randomSeed = 1;
    std::vector<size_t> filteredCache;
    uint64_t filteredRevision = (std::numeric_limits<uint64_t>::max)();
    bool filtersDirty = true;
    size_t filterRebuildCount = 0;
    double lastFilterMilliseconds = 0.0;

    constexpr const char* SettingsTextureIdentifier = "HOMEBROWSER_SETTINGS_ICON";

    const std::array<const char*, 7> Maps =
    {
        "", "Hearth's Glow", "Comosus Isle", "Gilded Hollow",
        "Lost Precipice", "Windswept Haven", "Isle of Reflection"
    };

    struct StyleScope
    {
        StyleScope()
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.075f, 0.07f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.095f, 0.095f, 0.09f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.095f, 0.085f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.27f, 0.20f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.115f, 0.105f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.17f, 0.15f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.135f, 0.11f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.23f, 0.19f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.31f, 0.23f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.19f, 0.11f, 0.86f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.25f, 0.13f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, Gold);
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.31f, 0.27f, 0.20f, 0.85f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
        }
        ~StyleScope()
        {
            ImGui::PopStyleVar(4);
            ImGui::PopStyleColor(13);
        }
    };

    struct FontScope
    {
        bool pushed = false;
        FontScope()
        {
            NexusLinkData_t* link = AppRuntime::GetNexusLink();
            if (link != nullptr && link->FontUI != nullptr)
            {
                ImGui::PushFont(static_cast<ImFont*>(link->FontUI));
                pushed = true;
            }
        }
        ~FontScope()
        {
            if (pushed) ImGui::PopFont();
        }
    };

    struct DisabledScope
    {
        bool disabled = false;
        explicit DisabledScope(bool value) : disabled(value)
        {
            if (disabled)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.50f);
            }
        }
        ~DisabledScope()
        {
            if (disabled)
            {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
        }
    };

    bool HollowActionButton(const char* label, const ImVec2& size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.23f, 0.19f, 0.13f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.31f, 0.23f, 0.13f, 0.90f));
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::ColorConvertFloat4ToU32(Gold), 3.0f, 0, 1.25f);
        return clicked;
    }

    bool GoldActionButton(const char* label, const ImVec2& size)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.085f, 0.055f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, Gold);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.78f, 0.46f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.76f, 0.56f, 0.27f, 1.0f));
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return clicked;
    }

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    void InvalidateFilters()
    {
        filtersDirty = true;
        currentPage = 1;
    }

    bool MatchesTags(const HomeListing& listing)
    {
        for (const std::string& selectedTag : selectedTags)
        {
            if (std::find(listing.tags.begin(), listing.tags.end(), selectedTag) == listing.tags.end())
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> AvailableTags()
    {
        std::vector<std::string> tags;
        tags.reserve(SeedTags.size() + ProviderRegistry::Listings().size());
        for (const char* tag : SeedTags) tags.emplace_back(tag);
        for (const HomeListing& listing : ProviderRegistry::Listings())
        {
            for (const std::string& tag : listing.tags)
            {
                if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end())
                {
                    tags.push_back(tag);
                }
            }
        }
        std::sort(tags.begin(), tags.end());
        return tags;
    }

    const std::vector<size_t>& FilteredListings()
    {
        const uint64_t revision = ProviderRegistry::ListingsRevision();
        if (!filtersDirty && filteredRevision == revision) return filteredCache;

        const auto started = std::chrono::steady_clock::now();
        const std::vector<HomeListing>& all = ProviderRegistry::Listings();
        filteredCache.clear();
        filteredCache.reserve(all.size());
        const std::string query = Lower(search.data());

        for (size_t index = 0; index < all.size(); ++index)
        {
            const HomeListing& item = all[index];
            const bool hasXml = !item.downloadUrl.empty();
            if (ProviderRegistry::IsLive() && !AppSettings::Get().showListingsWithoutXml && !hasXml) continue;
            if (typeFilter != TypeFilter::All && !hasXml) continue;
            if (typeFilter == TypeFilter::Homesteads && item.guildHall) continue;
            if (typeFilter == TypeFilter::GuildHalls && !item.guildHall) continue;
            if (mapFilter > 0 && (!hasXml || item.map != Maps[mapFilter])) continue;
            if (!MatchesTags(item)) continue;

            bool matchesSearch = query.empty() || Lower(item.name).find(query) != std::string::npos ||
                Lower(item.creator).find(query) != std::string::npos || Lower(item.map).find(query) != std::string::npos ||
                Lower(item.description).find(query) != std::string::npos;
            for (const std::string& tag : item.tags)
            {
                matchesSearch = matchesSearch || Lower(tag).find(query) != std::string::npos;
            }
            if (matchesSearch) filteredCache.push_back(index);
        }

        if (browseMode == BrowseMode::TopRated)
        {
            std::stable_sort(filteredCache.begin(), filteredCache.end(), [&all](size_t left, size_t right)
            {
                return all[left].likes > all[right].likes;
            });
        }
        else if (browseMode == BrowseMode::Newest)
        {
            std::stable_sort(filteredCache.begin(), filteredCache.end(), [&all](size_t left, size_t right)
            {
                const std::string& leftDate = all[left].publishedAt.empty() ? all[left].updatedAt : all[left].publishedAt;
                const std::string& rightDate = all[right].publishedAt.empty() ? all[right].updatedAt : all[right].publishedAt;
                return leftDate > rightDate;
            });
        }
        else if (browseMode == BrowseMode::Random && filteredCache.size() > 1)
        {
            std::mt19937 generator(randomSeed);
            std::shuffle(filteredCache.begin(), filteredCache.end(), generator);
        }
        filteredRevision = revision;
        filtersDirty = false;
        ++filterRebuildCount;
        lastFilterMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return filteredCache;
    }

    ImVec4 ProviderColor(const HomeListing& item)
    {
        (void)item;
        return Gold;
    }

    bool ModeButton(const char* label, BrowseMode mode)
    {
        const bool active = browseMode == mode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.27f, 0.12f, 1.0f));
        const bool clicked = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        if (clicked)
        {
            browseMode = mode;
            if (mode == BrowseMode::Random) ++randomSeed;
            InvalidateFilters();
        }
        return clicked;
    }

    bool ViewModeButton(const char* id, AppSettings::ViewMode mode, const char* tooltip)
    {
        const bool active = currentView == mode;
        const float size = ImGui::GetFrameHeight();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##ViewMode", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const ImU32 background = ImGui::ColorConvertFloat4ToU32(
            active ? ImVec4(0.38f, 0.27f, 0.12f, 1.0f)
            : hovered ? ImVec4(0.23f, 0.19f, 0.13f, 1.0f)
            : ImVec4(0.15f, 0.135f, 0.11f, 1.0f));
        draw->AddRectFilled(minimum, maximum, background, 3.0f);
        draw->AddRect(minimum, maximum, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Border)), 3.0f);

        const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(active ? Gold : MutedText);
        const float left = minimum.x + 6.0f;
        const float right = maximum.x - 6.0f;
        if (mode == AppSettings::ViewMode::List)
        {
            const float centerY = (minimum.y + maximum.y) * 0.5f;
            draw->AddLine(ImVec2(left, centerY - 5.0f), ImVec2(right, centerY - 5.0f), iconColor, 1.5f);
            draw->AddLine(ImVec2(left, centerY), ImVec2(right, centerY), iconColor, 1.5f);
            draw->AddLine(ImVec2(left, centerY + 5.0f), ImVec2(right, centerY + 5.0f), iconColor, 1.5f);
        }
        else
        {
            draw->AddRect(ImVec2(left, minimum.y + 6.0f), ImVec2(right, maximum.y - 6.0f), iconColor, 1.5f, 0, 1.5f);
        }
        if (hovered) ImGui::SetTooltip("%s", tooltip);
        ImGui::PopID();
        if (clicked && !(AppSettings::Get().disableImages && mode == AppSettings::ViewMode::Thumbnails))
        {
            currentView = mode;
        }
        return clicked;
    }

    void DrawViewModeButtons()
    {
        const float buttonSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float totalWidth = buttonSize * 2.0f + spacing;
        const float targetX = ImGui::GetWindowContentRegionMax().x - totalWidth;
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + spacing, targetX));
        ViewModeButton("List", AppSettings::ViewMode::List, HB_TEXT("HB_LIST_VIEW"));
        ImGui::SameLine();
        {
            DisabledScope disabled(AppSettings::Get().disableImages);
            ViewModeButton("Thumbnails", AppSettings::ViewMode::Thumbnails, HB_TEXT("HB_THUMBNAIL_VIEW"));
        }
    }

    void OpenListing(size_t index)
    {
        selectedListing = index;
        detailsVisible = true;
        showMissing = false;
        const std::vector<HomeListing>& listings = ProviderRegistry::Listings();
        if (index < listings.size() && !listings[index].downloadUrl.empty())
        {
            ProviderRegistry::RequestDetails(listings[index].uuid);
        }
    }

    void FilterByTag(const std::string& tag)
    {
        selectedTags.clear();
        selectedTags.push_back(tag);
        browseMode = BrowseMode::ByTag;
        InvalidateFilters();
        detailsVisible = false;
    }

    void DrawTagPopup()
    {
        if (ImGui::BeginPopup("HB_TAG_FILTER_POPUP"))
        {
            ImGui::TextColored(Gold, "%s", HB_TEXT("HB_TAG_FILTERS"));
            ImGui::Separator();
            const std::vector<std::string> availableTags = AvailableTags();
            for (const std::string& tag : availableTags)
            {
                bool selected = std::find(selectedTags.begin(), selectedTags.end(), tag) != selectedTags.end();
                if (ImGui::Checkbox(tag.c_str(), &selected))
                {
                    if (selected)
                    {
                        selectedTags.push_back(tag);
                    }
                    else
                    {
                        selectedTags.erase(std::remove(selectedTags.begin(), selectedTags.end(), tag), selectedTags.end());
                    }
                    InvalidateFilters();
                }
            }
            ImGui::Separator();
            if (ImGui::Button(HB_TEXT("HB_CLEAR_TAGS")))
            {
                selectedTags.clear();
                InvalidateFilters();
            }
            ImGui::EndPopup();
        }
    }

    void DrawFilters()
    {
        ModeButton(HB_TEXT("HB_TOP_RATED"), BrowseMode::TopRated);
        ImGui::SameLine();
        ModeButton(HB_TEXT("HB_NEWEST"), BrowseMode::Newest);
        ImGui::SameLine();
        ModeButton(HB_TEXT("HB_RANDOM"), BrowseMode::Random);
        ImGui::SameLine();
        if (ModeButton(HB_TEXT("HB_BY_TAG"), BrowseMode::ByTag))
        {
            ImGui::OpenPopup("HB_TAG_FILTER_POPUP");
        }
        DrawTagPopup();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        const char* typeLabel = typeFilter == TypeFilter::All ? HB_TEXT("HB_ALL_TYPES")
            : typeFilter == TypeFilter::Homesteads ? HB_TEXT("HB_HOMESTEADS") : HB_TEXT("HB_GUILD_HALLS");
        if (ImGui::BeginCombo("##TypeFilter", typeLabel))
        {
            if (ImGui::Selectable(HB_TEXT("HB_ALL_TYPES"), typeFilter == TypeFilter::All)) { typeFilter = TypeFilter::All; InvalidateFilters(); }
            if (ImGui::Selectable(HB_TEXT("HB_HOMESTEADS"), typeFilter == TypeFilter::Homesteads)) { typeFilter = TypeFilter::Homesteads; InvalidateFilters(); }
            if (ImGui::Selectable(HB_TEXT("HB_GUILD_HALLS"), typeFilter == TypeFilter::GuildHalls)) { typeFilter = TypeFilter::GuildHalls; InvalidateFilters(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##MapFilter", mapFilter == 0 ? HB_TEXT("HB_ALL_MAPS") : Maps[mapFilter]))
        {
            if (ImGui::Selectable(HB_TEXT("HB_ALL_MAPS"), mapFilter == 0)) { mapFilter = 0; InvalidateFilters(); }
            for (size_t index = 1; index < Maps.size(); ++index)
            {
                if (ImGui::Selectable(Maps[index], mapFilter == static_cast<int>(index)))
                {
                    mapFilter = static_cast<int>(index);
                    InvalidateFilters();
                }
            }
            ImGui::EndCombo();
        }
        DrawViewModeButtons();
    }

    void DrawList(const std::vector<size_t>& filtered, size_t begin, size_t end)
    {
        const std::vector<HomeListing>& all = ProviderRegistry::Listings();
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("HB_RESULTS_TABLE", 5, flags, ImVec2(0.0f, -1.0f))) return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(HB_TEXT("HB_NAME_CREATOR"), ImGuiTableColumnFlags_WidthStretch, 2.3f);
        ImGui::TableSetupColumn(HB_TEXT("HB_MAP"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(HB_TEXT("HB_TAGS"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(HB_TEXT("HB_DECORATIONS"), ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn(HB_TEXT("HB_SOURCE"), ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableHeadersRow();

        for (size_t position = begin; position < end; ++position)
        {
            const size_t index = filtered[position];
            const HomeListing& item = all[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 45.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetWindowFontScale(1.08f);
            if (ImGui::Selectable(item.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, ImGui::GetTextLineHeight() + 2.0f)))
            {
                OpenListing(index);
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.0f);
            ImGui::SetWindowFontScale(0.92f);
            ImGui::TextColored(MutedText, HB_TEXT("HB_BY"), item.creator.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            if (!item.downloadUrl.empty()) ImGui::TextWrapped("%s", item.map.c_str());
            ImGui::TableSetColumnIndex(2);
            for (size_t tagIndex = 0; tagIndex < item.tags.size(); ++tagIndex)
            {
                if (tagIndex > 0) ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(MutedText, "%s%s", tagIndex > 0 ? "· " : "", item.tags[tagIndex].c_str());
            }
            ImGui::TableSetColumnIndex(3);
            if (!item.downloadUrl.empty()) ImGui::Text("%d", item.decorationCount);
            ImGui::TableSetColumnIndex(4); ImGui::TextColored(ProviderColor(item), "%s", item.providerName.c_str());
        }
        ImGui::EndTable();
    }

    void DrawThumbnail(const HomeListing& item, float width, bool fullSize = false)
    {
        if (AppSettings::Get().disableImages) return;
        const float height = fullSize
            ? (std::min)(320.0f, width * 9.0f / 16.0f)
            : width * 9.0f / 16.0f;
        void* texture = fullSize
            ? ThumbnailCache::GetDetail(item, !AppSettings::Get().performanceMode)
            : ThumbnailCache::GetThumbnail(item);
        if (fullSize && texture == nullptr && !AppSettings::Get().performanceMode)
        {
            // Display the visible-page thumbnail while the addon-owned HD texture is decoded.
            texture = ThumbnailCache::GetThumbnail(item);
        }
        if (texture != nullptr)
        {
            ImGui::Image(texture, ImVec2(width, height));
            return;
        }
        ImGui::InvisibleButton("##Thumbnail", ImVec2(width, height));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(minimum, maximum, IM_COL32(25, 24, 22, 255), 3.0f);
        draw->AddRect(minimum, maximum, ImGui::ColorConvertFloat4ToU32(ProviderColor(item)), 3.0f);
        draw->AddLine(ImVec2(minimum.x + 18.0f, maximum.y - 22.0f), ImVec2(maximum.x - 18.0f, minimum.y + 22.0f), IM_COL32(72, 66, 55, 180));
        draw->AddLine(ImVec2(minimum.x + 18.0f, minimum.y + 22.0f), ImVec2(maximum.x - 18.0f, maximum.y - 22.0f), IM_COL32(72, 66, 55, 180));
        const char* thumbnailState = item.socialThumbnailUrl.empty()
            ? "HB_THUMBNAIL_PENDING"
            : ThumbnailCache::HasFailed(item) ? "HB_THUMBNAIL_UNAVAILABLE" : "HB_THUMBNAIL_LOADING";
        const char* text = HB_TEXT(thumbnailState);
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(minimum.x + (width - size.x) * 0.5f, minimum.y + (height - size.y) * 0.5f), IM_COL32(198, 174, 126, 255), text);
    }

    void DrawGrid(const std::vector<size_t>& filtered, size_t begin, size_t end)
    {
        const std::vector<HomeListing>& all = ProviderRegistry::Listings();
        const float available = ImGui::GetContentRegionAvail().x;
        const float targetWidth = 240.0f;
        const float maximumCardWidth = 268.0f;
        const ImVec2 cardPadding(8.0f, 8.0f);
        const float cardTextHeight = ImGui::GetTextLineHeight() * 4.0f;
        const float cardSpacingHeight = ImGui::GetStyle().ItemSpacing.y * 4.0f;
        const float cardHeightSafetyMargin = 4.0f;
        const int columns = (std::max)(1, static_cast<int>((available + 10.0f) / (targetWidth + 10.0f)));

        if (!ImGui::BeginTable("HB_GRID", columns, ImGuiTableFlags_SizingStretchSame)) return;
        for (size_t position = begin; position < end; ++position)
        {
            const size_t index = filtered[position];
            const HomeListing& item = all[index];
            const int column = static_cast<int>((position - begin) % static_cast<size_t>(columns));
            if (column == 0) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(column);
            const float cellWidth = ImGui::GetContentRegionAvail().x;
            const float cardWidth = (std::max)(120.0f, (std::min)(maximumCardWidth, cellWidth));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (cellWidth - cardWidth) * 0.5f));
            ImGui::PushID(static_cast<int>(index));
            const float cardContentWidth = (std::max)(1.0f, cardWidth - cardPadding.x * 2.0f);
            const float cardImageHeight = AppSettings::Get().disableImages
                ? 0.0f
                : cardContentWidth * 9.0f / 16.0f;
            const float cardHeight = cardPadding.y * 2.0f + cardImageHeight +
                cardTextHeight + cardSpacingHeight + cardHeightSafetyMargin;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, cardPadding);
            ImGui::BeginChild(
                "##Card",
                ImVec2(cardWidth, cardHeight),
                true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawThumbnail(item, ImGui::GetContentRegionAvail().x);
            const bool imageClicked = !AppSettings::Get().disableImages && ImGui::IsItemClicked();
            ImGui::TextColored(ProviderColor(item), "%s", item.providerName.c_str());
            if (ImGui::Selectable(item.name.c_str(), false) || imageClicked)
            {
                OpenListing(index);
            }
            ImGui::TextColored(MutedText, HB_TEXT("HB_BY"), item.creator.c_str());
            if (!item.downloadUrl.empty())
            {
                ImGui::TextDisabled("%s · %d %s", item.map.c_str(), item.decorationCount, HB_TEXT("HB_DECORATIONS"));
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void DrawResults()
    {
        const std::vector<size_t>& filtered = FilteredListings();
        const int perPage = AppSettings::Get().resultsPerPage;
        ProviderRegistry::EnsurePageAhead(currentPage, perPage);
        const bool unfiltered = search[0] == '\0' && selectedTags.empty() &&
            typeFilter == TypeFilter::All && mapFilter == 0;
        const int knownTotal = unfiltered && ProviderRegistry::TotalListings() > 0
            ? ProviderRegistry::TotalListings()
            : static_cast<int>(filtered.size());
        const int pageCount = (std::max)(1, (knownTotal + perPage - 1) / perPage);
        const int loadedPageCount = (std::max)(1, static_cast<int>((filtered.size() + perPage - 1) / perPage));
        currentPage = (std::max)(1, (std::min)(currentPage, loadedPageCount));
        const size_t begin = (std::min)(filtered.size(), static_cast<size_t>((currentPage - 1) * perPage));
        const size_t end = (std::min)(filtered.size(), begin + static_cast<size_t>(perPage));

        const std::vector<HomeListing>& all = ProviderRegistry::Listings();
        if (!AppSettings::Get().disableImages && !AppSettings::Get().performanceMode)
        {
            const size_t prefetchEnd = (std::min)(filtered.size(), end + static_cast<size_t>(perPage));
            for (size_t position = end; position < prefetchEnd; ++position)
            {
                ThumbnailCache::Prefetch(all[filtered[position]]);
            }
        }

        if (currentView == AppSettings::ViewMode::Thumbnails && !AppSettings::Get().disableImages)
        {
            std::vector<const HomeListing*> visibleListings;
            visibleListings.reserve(end - begin);
            for (size_t position = begin; position < end; ++position)
            {
                visibleListings.push_back(&all[filtered[position]]);
            }
            ThumbnailCache::SetVisibleListings(visibleListings);
        }
        else
        {
            ThumbnailCache::ClearVisibleTextures();
        }

        ImGui::BeginChild("##Results", ImVec2(0.0f, -43.0f), true);
        const float resultsStartX = ImGui::GetCursorScreenPos().x;
        resultsRightEdgeX = resultsStartX + ImGui::GetContentRegionAvail().x;
        resultsContentCenterX = (resultsStartX + resultsRightEdgeX) * 0.5f;
        if (filtered.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 28.0f));
            ImGui::TextDisabled("%s", HB_TEXT("HB_NO_RESULTS"));
        }
        else if (currentView == AppSettings::ViewMode::List)
        {
            DrawList(filtered, begin, end);
        }
        else
        {
            DrawGrid(filtered, begin, end);
        }
        ImGui::EndChild();

        const ImVec2 footerStart = ImGui::GetCursorScreenPos();
        const float footerHeight = ImGui::GetFrameHeight();
        ImGui::InvisibleButton("##Settings", ImVec2(footerHeight, footerHeight));
        const ImVec2 settingsMinimum = ImGui::GetItemRectMin();
        const ImVec2 settingsMaximum = ImGui::GetItemRectMax();
        ImDrawList* footerDraw = ImGui::GetWindowDrawList();
        const bool settingsHovered = ImGui::IsItemHovered();
        footerDraw->AddRectFilled(
            settingsMinimum,
            settingsMaximum,
            ImGui::ColorConvertFloat4ToU32(settingsHovered ? ImVec4(0.23f, 0.19f, 0.13f, 1.0f) : ImVec4(0.15f, 0.135f, 0.11f, 1.0f)),
            3.0f);
        footerDraw->AddRect(settingsMinimum, settingsMaximum, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Border)), 3.0f);
        AddonAPI_t* api = AppRuntime::GetApi();
        Texture_t* settingsTexture = api == nullptr || api->Textures_Get == nullptr ? nullptr : api->Textures_Get(SettingsTextureIdentifier);
        if (settingsTexture != nullptr && settingsTexture->Resource != nullptr)
        {
            footerDraw->AddImage(
                settingsTexture->Resource,
                ImVec2(settingsMinimum.x + 4.0f, settingsMinimum.y + 4.0f),
                ImVec2(settingsMaximum.x - 4.0f, settingsMaximum.y - 4.0f),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                ImGui::ColorConvertFloat4ToU32(settingsHovered ? Gold : MutedText));
        }
        if (ImGui::IsItemClicked()) settingsVisible = true;
        if (settingsHovered) ImGui::SetTooltip("%s", HB_TEXT("HB_SETTINGS"));

        char resultsLabel[64] = {};
        if (unfiltered && knownTotal > static_cast<int>(filtered.size()))
        {
            std::snprintf(resultsLabel, sizeof(resultsLabel), HB_TEXT("HB_RESULTS_LOADED"),
                static_cast<int>(filtered.size()), knownTotal);
        }
        else
        {
            std::snprintf(resultsLabel, sizeof(resultsLabel), HB_TEXT("HB_RESULTS"), static_cast<int>(filtered.size()));
        }
        const ImVec2 resultsLabelSize = ImGui::CalcTextSize(resultsLabel);
        ImGui::SetCursorScreenPos(ImVec2(
            resultsContentCenterX - resultsLabelSize.x * 0.5f,
            footerStart.y + (footerHeight - resultsLabelSize.y) * 0.5f));
        ImGui::TextDisabled("%s", resultsLabel);

        char pageLabel[64] = {};
        std::snprintf(pageLabel, sizeof(pageLabel), HB_TEXT("HB_PAGE"), currentPage, pageCount);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float previousWidth = ImGui::CalcTextSize(HB_TEXT("HB_PREVIOUS")).x + style.FramePadding.x * 2.0f;
        const float pageWidth = ImGui::CalcTextSize(pageLabel).x;
        const float nextWidth = ImGui::CalcTextSize(HB_TEXT("HB_NEXT")).x + style.FramePadding.x * 2.0f;
        const float pagerWidth = previousWidth + pageWidth + nextWidth + style.ItemSpacing.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(resultsRightEdgeX - pagerWidth, footerStart.y));
        {
            DisabledScope disabled(currentPage <= 1);
            if (ImGui::Button(HB_TEXT("HB_PREVIOUS"))) --currentPage;
        }
        ImGui::SameLine();
        ImGui::Text("%s", pageLabel);
        ImGui::SameLine();
        {
            const bool nextPageLoaded = static_cast<size_t>(currentPage * perPage) < filtered.size();
            DisabledScope disabled(currentPage >= pageCount || !nextPageLoaded);
            if (ImGui::Button(HB_TEXT("HB_NEXT"))) ++currentPage;
        }
    }

    void DrawMainWindow()
    {
        AppSettings::Data& settings = AppSettings::Get();
        if (!settings.windowVisible)
        {
            ThumbnailCache::ClearVisibleTextures();
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(1040.0f, 690.0f), ImGuiCond_FirstUseEver);
        const bool wasVisible = settings.windowVisible;
        const std::string windowTitle = std::string(HB_TEXT("HB_WINDOW_TITLE")) + "###Home Browser";
        if (ImGui::Begin(windowTitle.c_str(), &settings.windowVisible, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::SetWindowFontScale(1.35f);
            ImGui::TextColored(Gold, "HOME BROWSER");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();

            ImGui::SetNextItemWidth(-112.0f);
            if (ImGui::InputTextWithHint("##Search", HB_TEXT("HB_SEARCH"), search.data(), search.size())) InvalidateFilters();
            ImGui::SameLine();
            {
                DisabledScope disabled(ProviderRegistry::IsLoading());
                if (ImGui::Button(HB_TEXT("HB_REFRESH")))
                {
                    ProviderRegistry::RequestRefresh();
                    InvalidateFilters();
                    detailsVisible = false;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", ProviderRegistry::Status().c_str());
            }

            DrawFilters();
            ImGui::Separator();

            DrawResults();
        }
        ImGui::End();

        if (settings.rememberWindowState && wasVisible != settings.windowVisible)
        {
            AppSettings::MarkDirty();
        }
    }

    void DrawDetailsActionBar(const HomeListing& item, bool hasXml)
    {
        constexpr float barHeight = 52.0f;
        ImGui::BeginChild("##DetailsActionBar", ImVec2(0.0f, barHeight), true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const float buttonHeight = 30.0f;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float websiteWidth = ImGui::CalcTextSize(HB_TEXT("HB_OPEN_ORIGINAL")).x + style.FramePadding.x * 3.0f;
        const float downloadWidth = hasXml
            ? ImGui::CalcTextSize(HB_TEXT("HB_DOWNLOAD_XML")).x + style.FramePadding.x * 3.0f
            : 0.0f;
        const float buttonsWidth = websiteWidth + (hasXml ? style.ItemSpacing.x + downloadWidth : 0.0f);
        const float buttonY = (ImGui::GetWindowHeight() - buttonHeight) * 0.5f;
        const float buttonsX = (std::max)(ImGui::GetWindowContentRegionMin().x,
            ImGui::GetWindowContentRegionMax().x - buttonsWidth);

        if (!XmlDownloader::Status().empty())
        {
            ImGui::SetCursorPosY(buttonY);
            ImGui::PushTextWrapPos(buttonsX - style.ItemSpacing.x);
            ImGui::TextDisabled("%s", XmlDownloader::Status().c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::SetCursorPos(ImVec2(buttonsX, buttonY));
        if (HollowActionButton(HB_TEXT("HB_OPEN_ORIGINAL"), ImVec2(websiteWidth, buttonHeight)))
            AppRuntime::OpenUrl(item.sourceUrl);

        if (hasXml)
        {
            ImGui::SameLine();
            const char* destination = item.guildHall
                ? AppSettings::Get().guildHallFolder.data()
                : AppSettings::Get().homesteadFolder.data();
            {
                DisabledScope disabled(XmlDownloader::IsBusy() || destination[0] == '\0');
                if (GoldActionButton(HB_TEXT("HB_DOWNLOAD_XML"), ImVec2(downloadWidth, buttonHeight)))
                    XmlDownloader::Start(item.downloadUrl, destination, item.name);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && destination[0] == '\0')
                ImGui::SetTooltip("%s", HB_TEXT("HB_DOWNLOAD_FOLDER_REQUIRED"));
        }
        ImGui::EndChild();
    }

    void DrawDetailsWindow()
    {
        const std::vector<HomeListing>& listings = ProviderRegistry::Listings();
        if (!detailsVisible || selectedListing >= listings.size()) return;
        const HomeListing& item = listings[selectedListing];

        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 displayCenter(displaySize.x * 0.5f, displaySize.y * 0.5f);
        ImGui::SetNextWindowPos(displayCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(780.0f, 600.0f), ImGuiCond_FirstUseEver);
        const std::string detailsTitle = std::string(HB_TEXT("HB_DETAILS_TITLE")) + "###Home Browser Details";
        if (ImGui::Begin(detailsTitle.c_str(), &detailsVisible, ImGuiWindowFlags_NoCollapse))
        {
            const bool hasXml = !item.downloadUrl.empty();
            ImGui::SetWindowFontScale(1.28f);
            ImGui::TextColored(Gold, "%s", item.name.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(MutedText, HB_TEXT("HB_BY"), item.creator.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ProviderColor(item), "  ·  %s", item.providerName.c_str());
            ImGui::Separator();

            constexpr float actionBarReserve = 60.0f;
            ImGui::BeginChild("##DetailsContent", ImVec2(0.0f, -actionBarReserve), false);

            const float available = ImGui::GetContentRegionAvail().x;
            const bool twoColumns = hasXml && available >= 680.0f;
            if (twoColumns) ImGui::BeginChild("##DetailMain", ImVec2(available - 245.0f, 0.0f), false);

            DrawThumbnail(item, ImGui::GetContentRegionAvail().x, true);
            constexpr float DetailPadding = 10.0f;
            ImGui::Indent(DetailPadding);
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - DetailPadding);
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
            ImGui::TextColored(Gold, "%s", HB_TEXT("HB_DESCRIPTION"));
            ImGui::Separator();
            ImGui::TextWrapped("%s", item.description.c_str());
            ImGui::Spacing();
            for (const std::string& tag : item.tags)
            {
                ImGui::PushID(tag.c_str());
                if (ImGui::SmallButton(tag.c_str())) FilterByTag(tag);
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            if (hasXml)
            {
                ImGui::Text("%s: %s", HB_TEXT("HB_TYPE"), item.guildHall ? HB_TEXT("HB_GUILD_HALL") : HB_TEXT("HB_HOMESTEAD"));
                ImGui::Text("%s: %s", HB_TEXT("HB_MAP"), item.map.c_str());
                ImGui::Text("%s: %d", HB_TEXT("HB_DECORATIONS"), item.decorationCount);
            }
            ImGui::Text("%s: %d   %s: %d", HB_TEXT("HB_VIEWS"), item.views, HB_TEXT("HB_LIKES"), item.likes);

            ImGui::PopTextWrapPos();
            ImGui::Unindent(DetailPadding);

            if (hasXml && twoColumns)
            {
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("##DecorationSidebar", ImVec2(0.0f, 0.0f), true);
            }
            else if (hasXml)
            {
                ImGui::Separator();
            }

            if (hasXml)
            {
                const ProviderRegistry::DetailLoadState detailState = ProviderRegistry::DetailsState(item.uuid);
                if (detailState == ProviderRegistry::DetailLoadState::Loading || detailState == ProviderRegistry::DetailLoadState::NotRequested)
                {
                    if (detailState == ProviderRegistry::DetailLoadState::NotRequested) ProviderRegistry::RequestDetails(item.uuid);
                    ImGui::TextDisabled("%s", HB_TEXT("HB_LOADING_DECORATIONS"));
                }
                else if (detailState == ProviderRegistry::DetailLoadState::Failed)
                {
                    ImGui::TextWrapped("%s", ProviderRegistry::DetailsError(item.uuid).c_str());
                    if (ImGui::Button(HB_TEXT("HB_RETRY"))) ProviderRegistry::RetryDetails(item.uuid);
                }
                else
                {
                    const bool hasApiKey = AppSettings::Get().apiKey[0] != '\0';
                    if (hasApiKey && !item.guildHall) ProviderRegistry::RequestAccountDecorationCounts();
                    if (ImGui::RadioButton(HB_TEXT("HB_REQUIRED"), !showMissing)) showMissing = false;
                    ImGui::SameLine();
                    {
                        DisabledScope disabled(!hasApiKey || item.guildHall);
                        if (ImGui::RadioButton(HB_TEXT("HB_MISSING"), showMissing)) showMissing = true;
                    }
                    if (!hasApiKey)
                    {
                        showMissing = false;
                        ImGui::TextWrapped("%s", HB_TEXT("HB_REQUIRED_ONLY"));
                        ImGui::Separator();
                    }
                    else if (item.guildHall)
                    {
                        showMissing = false;
                        ImGui::TextWrapped("%s", HB_TEXT("HB_GUILD_MISSING_LATER"));
                        ImGui::Separator();
                    }

                    if (showMissing)
                    {
                        const ProviderRegistry::AccountLoadState accountState = ProviderRegistry::AccountCountsState();
                        if (accountState == ProviderRegistry::AccountLoadState::Loading ||
                            accountState == ProviderRegistry::AccountLoadState::NotRequested)
                        {
                            ImGui::TextDisabled("%s", HB_TEXT("HB_LOADING_ACCOUNT_DECORATIONS"));
                        }
                        else if (accountState == ProviderRegistry::AccountLoadState::Failed)
                        {
                            ImGui::TextWrapped("%s", ProviderRegistry::AccountCountsError().c_str());
                            if (ImGui::Button(HB_TEXT("HB_RETRY"))) ProviderRegistry::RetryAccountDecorationCounts();
                        }
                        else
                        {
                            bool anyMissing = false;
                            for (const DecorationRequirement& decoration : item.decorations)
                            {
                                const int available = ProviderRegistry::AvailableDecorationCount(decoration.id);
                                const int missing = (std::max)(0, decoration.quantity - available);
                                if (missing <= 0) continue;
                                anyMissing = true;
                                ImGui::Text(HB_TEXT("HB_DECORATION_ROW"), missing, decoration.name.c_str());
                            }
                            if (!anyMissing) ImGui::TextColored(Gold, "%s", HB_TEXT("HB_NONE_MISSING"));
                        }
                    }
                    else
                    {
                        const ProviderRegistry::AccountLoadState accountState = ProviderRegistry::AccountCountsState();
                        const bool canCompare = hasApiKey && !item.guildHall &&
                            accountState == ProviderRegistry::AccountLoadState::Loaded;
                        if (hasApiKey && !item.guildHall && accountState == ProviderRegistry::AccountLoadState::Loading)
                        {
                            ImGui::TextDisabled("%s", HB_TEXT("HB_LOADING_ACCOUNT_DECORATIONS"));
                            ImGui::Separator();
                        }
                        for (const DecorationRequirement& decoration : item.decorations)
                        {
                            const bool insufficient = canCompare &&
                                ProviderRegistry::AvailableDecorationCount(decoration.id) < decoration.quantity;
                            if (insufficient) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.28f, 0.28f, 1.0f));
                            ImGui::Text(HB_TEXT("HB_DECORATION_ROW"), decoration.quantity, decoration.name.c_str());
                            if (insufficient) ImGui::PopStyleColor();
                        }
                    }
                }
                if (twoColumns) ImGui::EndChild();
            }
            ImGui::EndChild();
            DrawDetailsActionBar(item, hasXml);
        }
        ImGui::End();
    }

    void SectionHeading(const char* text)
    {
        ImGui::SetWindowFontScale(1.17f);
        ImGui::TextColored(Gold, "%s", text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void DrawSettingsWindow()
    {
        if (!settingsVisible) return;
        AppSettings::Data& settings = AppSettings::Get();
        ImGui::SetNextWindowSize(ImVec2(600.0f, 560.0f), ImGuiCond_FirstUseEver);
        const std::string settingsTitle = std::string(HB_TEXT("HB_SETTINGS")) + "###Home Browser Settings";
        if (ImGui::Begin(settingsTitle.c_str(), &settingsVisible, ImGuiWindowFlags_NoCollapse))
        {
            SectionHeading(HB_TEXT("HB_API_KEY"));
            ImGui::TextWrapped("%s", HB_TEXT("HB_API_HELP"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ApiKey", settings.apiKey.data(), settings.apiKey.size(), ImGuiInputTextFlags_Password)) AppSettings::MarkDirty();

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            SectionHeading(HB_TEXT("HB_XML_FOLDERS"));
            ImGui::Text("%s", HB_TEXT("HB_HOMESTEAD_FOLDER"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##HomesteadFolder", settings.homesteadFolder.data(), settings.homesteadFolder.size())) AppSettings::MarkDirty();
            ImGui::Text("%s", HB_TEXT("HB_GUILD_HALL_FOLDER"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##GuildHallFolder", settings.guildHallFolder.data(), settings.guildHallFolder.size())) AppSettings::MarkDirty();

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            SectionHeading(HB_TEXT("HB_PREFERENCES"));
            if (ImGui::Checkbox(HB_TEXT("HB_REMEMBER_WINDOW"), &settings.rememberWindowState)) AppSettings::MarkDirty();
            if (ImGui::Checkbox(HB_TEXT("HB_SHOW_XMLLESS"), &settings.showListingsWithoutXml))
            {
                InvalidateFilters();
                detailsVisible = false;
                AppSettings::MarkDirty();
                ProviderRegistry::RequestRefresh();
            }
            if (ImGui::Checkbox(HB_TEXT("HB_PERFORMANCE_MODE"), &settings.performanceMode))
            {
                ThumbnailCache::ClearDetailTexture();
                AppSettings::MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", HB_TEXT("HB_PERFORMANCE_MODE_TOOLTIP"));
            if (ImGui::Checkbox(HB_TEXT("HB_DISABLE_IMAGES"), &settings.disableImages))
            {
                if (settings.disableImages)
                {
                    currentView = AppSettings::ViewMode::List;
                    settings.defaultView = AppSettings::ViewMode::List;
                    ThumbnailCache::CancelPrecache();
                    ThumbnailCache::ClearVisibleTextures();
                    ThumbnailCache::ClearDetailTexture();
                }
                AppSettings::MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", HB_TEXT("HB_DISABLE_IMAGES_TOOLTIP"));
            ImGui::Text("%s", HB_TEXT("HB_DEFAULT_VIEW"));
            int defaultView = static_cast<int>(settings.defaultView);
            if (ImGui::RadioButton(HB_TEXT("HB_LIST_VIEW"), defaultView == 0))
            {
                settings.defaultView = AppSettings::ViewMode::List;
                AppSettings::MarkDirty();
            }
            ImGui::SameLine();
            {
                DisabledScope disabled(settings.disableImages);
                if (ImGui::RadioButton(HB_TEXT("HB_THUMBNAIL_VIEW"), defaultView == 1))
                {
                    settings.defaultView = AppSettings::ViewMode::Thumbnails;
                    AppSettings::MarkDirty();
                }
            }

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            SectionHeading(HB_TEXT("HB_THUMBNAIL_CACHE"));
            const ThumbnailCache::Statistics textureStats = ThumbnailCache::GetStatistics();
            if (ThumbnailCache::IsPrecaching())
            {
                const size_t total = ThumbnailCache::PrecacheTotal();
                const size_t completed = ThumbnailCache::PrecacheCompleted();
                const float progress = total == 0 ? 0.0f : static_cast<float>(completed) / static_cast<float>(total);
                char overlay[64] = {};
                std::snprintf(overlay, sizeof(overlay), "%zu / %zu", completed, total);
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), overlay);
                if (ImGui::Button(HB_TEXT("HB_CANCEL_PRECACHE"))) ThumbnailCache::CancelPrecache();
            }
            else
            {
                DisabledScope disabled(settings.disableImages || ProviderRegistry::Listings().empty() || ThumbnailCache::IsClearingDiskCache());
                if (ImGui::Button(HB_TEXT("HB_PRECACHE_ALL")))
                    ThumbnailCache::PrecacheAll(ProviderRegistry::Listings());
            }
            ImGui::SameLine();
            {
                DisabledScope disabled(ThumbnailCache::IsClearingDiskCache());
                if (ImGui::Button(HB_TEXT("HB_CLEAR_THUMBNAIL_CACHE"))) ThumbnailCache::ClearDiskCache();
            }
            if (!ThumbnailCache::Status().empty()) ImGui::TextDisabled("%s", ThumbnailCache::Status().c_str());
            ImGui::TextDisabled(HB_TEXT("HB_CACHE_USAGE"), textureStats.diskFiles,
                static_cast<double>(textureStats.diskBytes) / (1024.0 * 1024.0));

            if (ImGui::TreeNode(HB_TEXT("HB_DIAGNOSTICS")))
            {
                ImGui::Text("Disk queue: %zu pending, %zu active", textureStats.pendingDownloads, textureStats.activeDownloads);
                ImGui::Text("Decode queue: %zu pending, %zu active", textureStats.pendingDecodes, textureStats.activeDecodes);
                ImGui::Text("GPU textures: %zu thumbnails, %zu detail", textureStats.gpuThumbnails, textureStats.gpuDetailTextures);
                ImGui::Text("Estimated GPU memory: %.2f MiB", static_cast<double>(textureStats.gpuBytes) / (1024.0 * 1024.0));
                ImGui::Text("Uploads: %zu   Releases: %zu   Failures: %zu",
                    textureStats.uploadedTextures, textureStats.releasedTextures, textureStats.failedTextures);
                ImGui::Text("Filter rebuilds: %zu   Last: %.3f ms", filterRebuildCount, lastFilterMilliseconds);
                ImGui::TreePop();
            }

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            SectionHeading(HB_TEXT("HB_PROVIDER_STATUS"));
            for (const ProviderDescriptor& provider : ProviderRegistry::Providers())
            {
                const ImVec4 color(provider.accent[0], provider.accent[1], provider.accent[2], 1.0f);
                ImGui::TextColored(color, "%s", provider.name.c_str());
                ImGui::SameLine(210.0f);
                const char* statusKey = provider.live
                    ? "HB_CONNECTED"
                    : ProviderRegistry::IsLoading() ? "HB_CONNECTING" : "HB_UNAVAILABLE";
                ImGui::TextDisabled("%s", HB_TEXT(statusKey));
            }
            ImGui::TextWrapped("%s", ProviderRegistry::Status().c_str());
        }
        ImGui::End();
    }
}

void MainWindow::Initialize()
{
    search.fill('\0');
    selectedTags.clear();
    browseMode = BrowseMode::TopRated;
    typeFilter = TypeFilter::All;
    mapFilter = 0;
    currentPage = 1;
    randomSeed = 1;
    detailsVisible = false;
    settingsVisible = false;
    selectedListing = 0;
    currentView = AppSettings::Get().defaultView;
    if (AppSettings::Get().disableImages) currentView = AppSettings::ViewMode::List;
    filteredCache.clear();
    filteredRevision = (std::numeric_limits<uint64_t>::max)();
    filtersDirty = true;
    filterRebuildCount = 0;
    lastFilterMilliseconds = 0.0;
}

void MainWindow::Shutdown()
{
    XmlDownloader::Shutdown();
    ThumbnailCache::ClearVisibleTextures();
    ThumbnailCache::ClearDetailTexture();
    detailsVisible = false;
    settingsVisible = false;
}

void MainWindow::Render()
{
    XmlDownloader::Update();
    AppSettings::Data& settings = AppSettings::Get();
    if (settings.disableImages) currentView = AppSettings::ViewMode::List;
    if (!detailsVisible || settings.disableImages) ThumbnailCache::ClearDetailTexture();
    if (!settings.windowVisible || settings.disableImages || currentView == AppSettings::ViewMode::List)
        ThumbnailCache::ClearVisibleTextures();
    ThumbnailCache::Update();
    StyleScope style;
    FontScope font;
    DrawMainWindow();
    DrawDetailsWindow();
    DrawSettingsWindow();
}

void MainWindow::RenderOptions()
{
    FontScope font;
    ImGui::TextColored(Gold, "Home Browser 1.0.0.4");
    ImGui::Separator();
    AppSettings::Data& settings = AppSettings::Get();
    bool visible = settings.windowVisible;
    if (ImGui::Checkbox("Show Home Browser", &visible))
    {
        settings.windowVisible = visible;
        AppSettings::MarkDirty();
    }
}

void MainWindow::Toggle()
{
    AppSettings::Data& settings = AppSettings::Get();
    settings.windowVisible = !settings.windowVisible;
    if (settings.rememberWindowState) AppSettings::MarkDirty();
}

bool* MainWindow::DetailsVisiblePointer()
{
    return &detailsVisible;
}

bool* MainWindow::SettingsVisiblePointer()
{
    return &settingsVisible;
}
