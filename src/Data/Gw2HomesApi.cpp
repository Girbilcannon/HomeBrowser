#include "Gw2HomesApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "Winhttp.lib")

namespace
{
    constexpr wchar_t Host[] = L"gw2.homes";
    constexpr size_t MaximumResponseBytes = 8 * 1024 * 1024;

    struct JsonValue
    {
        enum class Type { Null, Boolean, Number, String, Array, Object } type = Type::Null;
        bool boolean = false;
        double number = 0.0;
        std::string string;
        std::vector<JsonValue> array;
        std::unordered_map<std::string, JsonValue> object;
    };

    class JsonParser
    {
    public:
        explicit JsonParser(const std::string& source) : text(source) {}

        bool Parse(JsonValue& output, std::string& error)
        {
            SkipWhitespace();
            if (!ParseValue(output))
            {
                error = message.empty() ? "Invalid JSON response." : message;
                return false;
            }
            SkipWhitespace();
            if (position != text.size())
            {
                error = "Unexpected data after the JSON response.";
                return false;
            }
            return true;
        }

    private:
        const std::string& text;
        size_t position = 0;
        std::string message;

        void SkipWhitespace()
        {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) ++position;
        }

        bool Fail(const char* error)
        {
            if (message.empty()) message = error;
            return false;
        }

        bool Consume(char expected)
        {
            SkipWhitespace();
            if (position >= text.size() || text[position] != expected) return false;
            ++position;
            return true;
        }

        static void AppendUtf8(std::string& output, unsigned int codepoint)
        {
            if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }

        bool ParseHex4(unsigned int& value)
        {
            if (position + 4 > text.size()) return false;
            value = 0;
            for (int index = 0; index < 4; ++index)
            {
                const char character = text[position++];
                value <<= 4;
                if (character >= '0' && character <= '9') value += static_cast<unsigned int>(character - '0');
                else if (character >= 'a' && character <= 'f') value += static_cast<unsigned int>(character - 'a' + 10);
                else if (character >= 'A' && character <= 'F') value += static_cast<unsigned int>(character - 'A' + 10);
                else return false;
            }
            return true;
        }

        bool ParseString(std::string& output)
        {
            SkipWhitespace();
            if (position >= text.size() || text[position++] != '"') return Fail("Expected a JSON string.");
            output.clear();
            while (position < text.size())
            {
                const char character = text[position++];
                if (character == '"') return true;
                if (character != '\\')
                {
                    output.push_back(character);
                    continue;
                }
                if (position >= text.size()) return Fail("Incomplete JSON escape sequence.");
                const char escaped = text[position++];
                switch (escaped)
                {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u':
                {
                    unsigned int codepoint = 0;
                    if (!ParseHex4(codepoint)) return Fail("Invalid JSON Unicode escape.");
                    AppendUtf8(output, codepoint);
                    break;
                }
                default: return Fail("Unknown JSON escape sequence.");
                }
            }
            return Fail("Unterminated JSON string.");
        }

        bool ParseNumber(JsonValue& output)
        {
            const char* begin = text.c_str() + position;
            char* end = nullptr;
            const double value = std::strtod(begin, &end);
            if (end == begin) return Fail("Invalid JSON number.");
            position += static_cast<size_t>(end - begin);
            output.type = JsonValue::Type::Number;
            output.number = value;
            return true;
        }

        bool ParseArray(JsonValue& output)
        {
            if (!Consume('[')) return false;
            output.type = JsonValue::Type::Array;
            SkipWhitespace();
            if (Consume(']')) return true;
            while (position < text.size())
            {
                JsonValue value;
                if (!ParseValue(value)) return false;
                output.array.push_back(std::move(value));
                SkipWhitespace();
                if (Consume(']')) return true;
                if (!Consume(',')) return Fail("Expected a comma in a JSON array.");
            }
            return Fail("Unterminated JSON array.");
        }

        bool ParseObject(JsonValue& output)
        {
            if (!Consume('{')) return false;
            output.type = JsonValue::Type::Object;
            SkipWhitespace();
            if (Consume('}')) return true;
            while (position < text.size())
            {
                std::string key;
                if (!ParseString(key) || !Consume(':')) return Fail("Invalid JSON object member.");
                JsonValue value;
                if (!ParseValue(value)) return false;
                output.object.insert_or_assign(std::move(key), std::move(value));
                SkipWhitespace();
                if (Consume('}')) return true;
                if (!Consume(',')) return Fail("Expected a comma in a JSON object.");
            }
            return Fail("Unterminated JSON object.");
        }

        bool ParseValue(JsonValue& output)
        {
            SkipWhitespace();
            if (position >= text.size()) return Fail("Unexpected end of JSON response.");
            const char character = text[position];
            if (character == '{') return ParseObject(output);
            if (character == '[') return ParseArray(output);
            if (character == '"')
            {
                output.type = JsonValue::Type::String;
                return ParseString(output.string);
            }
            if (character == '-' || (character >= '0' && character <= '9')) return ParseNumber(output);
            if (text.compare(position, 4, "true") == 0)
            {
                position += 4; output.type = JsonValue::Type::Boolean; output.boolean = true; return true;
            }
            if (text.compare(position, 5, "false") == 0)
            {
                position += 5; output.type = JsonValue::Type::Boolean; output.boolean = false; return true;
            }
            if (text.compare(position, 4, "null") == 0)
            {
                position += 4; output.type = JsonValue::Type::Null; return true;
            }
            return Fail("Unknown JSON value.");
        }
    };

    const JsonValue* Find(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        if (value.type != JsonValue::Type::Object) return nullptr;
        for (const char* key : keys)
        {
            const auto iterator = value.object.find(key);
            if (iterator != value.object.end()) return &iterator->second;
        }
        return nullptr;
    }

    std::string StringValue(const JsonValue& value)
    {
        if (value.type == JsonValue::Type::String) return value.string;
        if (value.type == JsonValue::Type::Number) return std::to_string(static_cast<long long>(value.number));
        return {};
    }

    std::string StringField(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        const JsonValue* field = Find(value, keys);
        return field == nullptr ? std::string() : StringValue(*field);
    }

    int IntField(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        const JsonValue* field = Find(value, keys);
        if (field == nullptr) return 0;
        if (field->type == JsonValue::Type::Number) return static_cast<int>(std::llround(field->number));
        if (field->type == JsonValue::Type::String)
        {
            try { return std::stoi(field->string); }
            catch (...) { return 0; }
        }
        return 0;
    }

    bool BoolField(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        const JsonValue* field = Find(value, keys);
        if (field == nullptr) return false;
        if (field->type == JsonValue::Type::Boolean) return field->boolean;
        if (field->type == JsonValue::Type::Number) return field->number != 0.0;
        if (field->type == JsonValue::Type::String)
        {
            std::string text = field->string;
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return text == "true" || text == "1" || text.find("guild") != std::string::npos;
        }
        return false;
    }

    std::string NestedName(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        const JsonValue* field = Find(value, keys);
        if (field == nullptr) return {};
        if (field->type == JsonValue::Type::String) return field->string;
        return StringField(*field, { "name", "title", "username", "displayName", "display_name", "slug" });
    }

    int NestedCount(const JsonValue& value, std::initializer_list<const char*> keys)
    {
        const JsonValue* stats = Find(value, { "stats", "statistics", "counts" });
        const int direct = IntField(value, keys);
        return direct != 0 || stats == nullptr ? direct : IntField(*stats, keys);
    }

    bool IsGuildHallMap(int mapId, const std::string& mapName)
    {
        if (mapId == 1068 || mapId == 1101 || mapId == 1214 || mapId == 1419) return true;
        return mapName == "Gilded Hollow" || mapName == "Lost Precipice" ||
            mapName == "Windswept Haven" || mapName == "Isle of Reflection";
    }

    std::vector<std::string> Tags(const JsonValue& value)
    {
        std::vector<std::string> result;
        const JsonValue* tags = Find(value, { "tags", "categories" });
        if (tags == nullptr || tags->type != JsonValue::Type::Array) return result;
        for (const JsonValue& tag : tags->array)
        {
            const std::string name = tag.type == JsonValue::Type::String
                ? tag.string
                : StringField(tag, { "name", "title", "label" });
            if (!name.empty()) result.push_back(name);
        }
        return result;
    }

    std::string CleanDecorationName(std::string value)
    {
        // Some decoration API names append a second flavor-description line,
        // such as "Café Drink Station\n<c=@flavor>Provides drinks...".
        const size_t lineBreak = value.find_first_of("\r\n");
        const size_t flavorTag = value.find("<c=@flavor");
        size_t descriptionStart = lineBreak;
        if (descriptionStart == std::string::npos ||
            (flavorTag != std::string::npos && flavorTag < descriptionStart))
        {
            descriptionStart = flavorTag;
        }
        if (descriptionStart != std::string::npos) value.erase(descriptionStart);

        while (true)
        {
            const size_t tagStart = value.find('<');
            if (tagStart == std::string::npos) break;
            const size_t tagEnd = value.find('>', tagStart + 1);
            if (tagEnd == std::string::npos)
            {
                value.erase(tagStart);
                break;
            }
            value.erase(tagStart, tagEnd - tagStart + 1);
        }

        std::string output;
        bool pendingSpace = false;
        for (const char character : value)
        {
            if (std::isspace(static_cast<unsigned char>(character)) != 0)
            {
                pendingSpace = !output.empty();
            }
            else
            {
                if (pendingSpace) output += ' ';
                output += character;
                pendingSpace = false;
            }
        }
        return output;
    }

    const JsonValue* ListingsArray(const JsonValue& root)
    {
        if (root.type == JsonValue::Type::Array) return &root;
        const JsonValue* candidate = Find(root, { "homesteads", "items", "results", "data" });
        if (candidate == nullptr) return nullptr;
        if (candidate->type == JsonValue::Type::Array) return candidate;
        return Find(*candidate, { "homesteads", "items", "results", "data" });
    }

    std::string NormalizeUrl(const std::string& value)
    {
        if (value.empty()) return {};
        if (value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0) return value;
        if (value.front() == '/') return "https://gw2.homes" + value;
        return "https://gw2.homes/" + value;
    }

    HomeListing ConvertListing(const JsonValue& value)
    {
        HomeListing listing;
        listing.providerId = "gw2homes";
        listing.providerName = "gw2.homes";
        listing.uuid = StringField(value, { "uuid", "id", "homesteadUuid", "homestead_uuid" });
        listing.name = StringField(value, { "name", "title" });
        listing.creator = NestedName(value, { "creator", "createdBy", "created_by", "author", "owner", "user", "account", "username" });
        listing.tags = Tags(value);
        listing.description = StringField(value, { "description", "summary", "body" });
        listing.sourceUrl = NormalizeUrl(StringField(value, { "url", "pageUrl", "page_url", "permalink", "link" }));
        listing.thumbnailUrl = NormalizeUrl(StringField(value, { "thumbnail_url", "thumbnailUrl", "thumbnail" }));
        listing.socialThumbnailUrl = NormalizeUrl(StringField(value, { "social_thumbnail_url", "socialThumbnailUrl" }));
        listing.updatedAt = StringField(value, { "updated_at", "updatedAt" });
        listing.publishedAt = StringField(value, { "published_at", "publishedAt", "created_at", "createdAt" });
        const JsonValue* author = Find(value, { "author", "creator", "owner", "user" });
        if (author != nullptr) listing.authorUrl = NormalizeUrl(StringField(*author, { "url", "profile_url", "profileUrl" }));

        const JsonValue* layout = Find(value, { "layout" });
        const JsonValue* map = layout == nullptr ? Find(value, { "map" }) : Find(*layout, { "map" });
        if (map != nullptr)
        {
            listing.map = map->type == JsonValue::Type::String
                ? map->string
                : StringField(*map, { "name", "title", "mapName", "map_name" });
            listing.mapId = IntField(*map, { "id", "mapId", "map_id" });
        }
        if (listing.map.empty()) listing.map = NestedName(value, { "map", "mapName", "map_name" });

        listing.guildHall = BoolField(value, { "guildHall", "guild_hall", "isGuildHall", "is_guild_hall", "type" }) ||
            IsGuildHallMap(listing.mapId, listing.map);
        listing.decorationCount = layout == nullptr
            ? NestedCount(value, { "decorationCount", "decoration_count", "decorationsCount", "decorations_count", "totalDecorations", "total_decorations" })
            : NestedCount(*layout, { "decorations", "decorationCount", "decoration_count", "decorationsCount", "decorations_count", "totalDecorations", "total_decorations" });
        listing.views = NestedCount(value, { "views", "viewCount", "view_count", "totalViews", "total_views" });
        listing.likes = NestedCount(value, { "likes", "likeCount", "like_count", "totalLikes", "total_likes" });
        if (layout != nullptr)
        {
            listing.downloadUrl = NormalizeUrl(StringField(*layout, { "download_url", "downloadUrl" }));
        }

        if (listing.sourceUrl.empty() && !listing.creator.empty() && !listing.uuid.empty())
        {
            listing.sourceUrl = "https://gw2.homes/" + listing.creator + "/homestead/" + listing.uuid;
        }
        if (listing.map.empty()) listing.map = "Unknown map";
        return listing;
    }

    Gw2HomesDetailResult ConvertDetails(const JsonValue& root, const std::string& homesteadId)
    {
        Gw2HomesDetailResult result;
        result.uuid = homesteadId;
        const JsonValue* homestead = Find(root, { "data", "homestead" });
        if (homestead == nullptr) homestead = &root;
        const JsonValue* layout = Find(*homestead, { "layout" });
        if (layout == nullptr || layout->type == JsonValue::Type::Null)
        {
            result.error = "This homestead has no downloadable layout.";
            return result;
        }

        const JsonValue* decorations = Find(*layout, { "decorations" });
        if (decorations == nullptr || decorations->type != JsonValue::Type::Array)
        {
            result.error = "The homestead response did not contain a decoration list.";
            return result;
        }

        for (const JsonValue& value : decorations->array)
        {
            if (value.type != JsonValue::Type::Object) continue;
            DecorationRequirement decoration;
            decoration.id = IntField(value, { "id" });
            decoration.name = CleanDecorationName(StringField(value, { "name", "title" }));
            if (decoration.name.empty()) decoration.name = "Unknown Decoration";
            decoration.iconUrl = NormalizeUrl(StringField(value, { "icon_url", "iconUrl", "icon" }));
            decoration.quantity = IntField(value, { "quantity", "count", "amount" });
            if (decoration.quantity <= 0) continue;
            result.decorationCount += decoration.quantity;
            result.decorations.push_back(std::move(decoration));
        }

        if (result.decorations.empty())
        {
            result.error = "The homestead response contained no usable decorations.";
        }
        return result;
    }

    std::string ReadResponse(const wchar_t* path, std::string& error)
    {
        HINTERNET session = WinHttpOpen(
            L"HomeBrowser/1.0.0.4",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (session == nullptr)
        {
            error = "Could not initialize the HTTPS connection.";
            return {};
        }

        WinHttpSetTimeouts(session, 5000, 5000, 8000, 12000);
        HINTERNET connection = WinHttpConnect(session, Host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(
            connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        const wchar_t headers[] = L"Accept: application/json\r\n";
        bool succeeded = request != nullptr && WinHttpSendRequest(
            request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE;
        if (succeeded) succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded)
        {
            succeeded = WinHttpQueryHeaders(
                request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                WINHTTP_NO_HEADER_INDEX) == TRUE;
        }

        std::string response;
        while (succeeded)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                succeeded = false;
                break;
            }
            if (available == 0) break;
            if (response.size() + available > MaximumResponseBytes)
            {
                error = "The gw2.homes response exceeded the 8 MB safety limit.";
                succeeded = false;
                break;
            }
            const size_t oldSize = response.size();
            response.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.data() + oldSize, available, &read))
            {
                succeeded = false;
                break;
            }
            response.resize(oldSize + read);
        }

        const DWORD windowsError = succeeded ? ERROR_SUCCESS : GetLastError();
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!succeeded)
        {
            if (error.empty()) error = "Could not connect to gw2.homes (Windows error " + std::to_string(windowsError) + ").";
            return {};
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            error = "gw2.homes returned HTTP " + std::to_string(statusCode) + ".";
            return {};
        }
        if (response.empty()) error = "gw2.homes returned an empty response.";
        return response;
    }
}

Gw2HomesApiResult Gw2HomesApi::FetchListings(
    bool includeWithoutXml,
    int page,
    int pageSize,
    const std::string& orderBy)
{
    Gw2HomesApiResult result;
    result.page = (std::max)(1, page);
    pageSize = (std::max)(1, (std::min)(100, pageSize));
    const std::string safeOrder = orderBy == "newest" || orderBy == "oldest" ||
        orderBy == "updated" || orderBy == "views" || orderBy == "decorations_low" ||
        orderBy == "decorations_high" ? orderBy : "likes";
    std::wstring path = L"/api/homesteads?page_size=" + std::to_wstring(pageSize) +
        L"&order_by=" + std::wstring(safeOrder.begin(), safeOrder.end()) +
        L"&page=" + std::to_wstring(result.page);
    if (!includeWithoutXml) path += L"&downloadable=1";
    const std::string response = ReadResponse(path.c_str(), result.error);
    if (!result.error.empty()) return result;

    JsonValue root;
    JsonParser parser(response);
    if (!parser.Parse(root, result.error)) return result;
    const JsonValue* items = ListingsArray(root);
    if (items == nullptr || items->type != JsonValue::Type::Array)
    {
        result.error = "The gw2.homes response did not contain a homestead list.";
        return result;
    }

    const JsonValue* meta = Find(root, { "meta" });
    if (meta != nullptr)
    {
        result.page = (std::max)(1, IntField(*meta, { "current_page", "currentPage" }));
        result.lastPage = (std::max)(result.page, IntField(*meta, { "last_page", "lastPage" }));
        result.total = (std::max)(0, IntField(*meta, { "total" }));
    }
    else
    {
        result.lastPage = result.page;
        result.total = static_cast<int>(items->array.size());
    }

    for (const JsonValue& item : items->array)
    {
        if (item.type != JsonValue::Type::Object) continue;
        HomeListing listing = ConvertListing(item);
        if (!listing.name.empty() && !listing.uuid.empty()) result.listings.push_back(std::move(listing));
    }
    if (result.listings.empty()) result.error = "The gw2.homes response contained no usable homesteads.";
    return result;
}

Gw2HomesApiResult Gw2HomesApi::FetchListingIndex(bool includeWithoutXml)
{
    // Listing responses contain metadata and image URLs only. Images remain lazy-loaded.
    constexpr int IndexPageSize = 100;
    Gw2HomesApiResult index = FetchListings(includeWithoutXml, 1, IndexPageSize, "likes");
    if (!index.error.empty()) return index;

    const int sourceLastPage = index.lastPage;
    for (int page = 2; page <= sourceLastPage; ++page)
    {
        Gw2HomesApiResult batch = FetchListings(includeWithoutXml, page, IndexPageSize, "likes");
        if (!batch.error.empty())
        {
            index.error = "Could not complete the gallery index: " + batch.error;
            index.listings.clear();
            return index;
        }
        for (HomeListing& listing : batch.listings) index.listings.push_back(std::move(listing));
    }

    index.page = 1;
    index.lastPage = 1;
    if (index.total <= 0) index.total = static_cast<int>(index.listings.size());
    return index;
}

Gw2HomesDetailResult Gw2HomesApi::FetchDetails(const std::string& homesteadId)
{
    Gw2HomesDetailResult result;
    result.uuid = homesteadId;
    if (homesteadId.empty())
    {
        result.error = "The homestead ID was empty.";
        return result;
    }

    const std::wstring path = L"/api/homesteads/" + std::wstring(homesteadId.begin(), homesteadId.end());
    const std::string response = ReadResponse(path.c_str(), result.error);
    if (!result.error.empty()) return result;

    JsonValue root;
    JsonParser parser(response);
    if (!parser.Parse(root, result.error)) return result;
    return ConvertDetails(root, homesteadId);
}
