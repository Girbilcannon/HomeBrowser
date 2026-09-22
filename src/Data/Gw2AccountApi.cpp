#include "Gw2AccountApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <cctype>

#pragma comment(lib, "Winhttp.lib")

namespace
{
    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring output(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), count);
        return output;
    }

    size_t FindObjectEnd(const std::string& source, size_t begin)
    {
        int depth = 0;
        bool quoted = false;
        bool escaped = false;
        for (size_t position = begin; position < source.size(); ++position)
        {
            const char character = source[position];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
                continue;
            }
            if (character == '"') quoted = true;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0) return position;
        }
        return std::string::npos;
    }

    int JsonInt(const std::string& source, const char* key, size_t begin, size_t end, int fallback = -1)
    {
        const std::string token = std::string("\"") + key + "\"";
        size_t position = source.find(token, begin);
        if (position == std::string::npos || position >= end) return fallback;
        position = source.find(':', position + token.size());
        if (position == std::string::npos || position >= end) return fallback;
        ++position;
        while (position < end && std::isspace(static_cast<unsigned char>(source[position])) != 0) ++position;
        try { return std::stoi(source.substr(position, end - position)); }
        catch (...) { return fallback; }
    }

    std::string ApiErrorText(const std::string& json)
    {
        const std::string token = "\"text\"";
        size_t position = json.find(token);
        if (position == std::string::npos) return {};
        position = json.find(':', position + token.size());
        position = position == std::string::npos ? position : json.find('"', position + 1);
        if (position == std::string::npos) return {};
        const size_t end = json.find('"', position + 1);
        return end == std::string::npos ? std::string() : json.substr(position + 1, end - position - 1);
    }

    std::string Get(const std::string& apiKey, std::string& error)
    {
        HINTERNET session = WinHttpOpen(L"HomeBrowser/1.0.0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == nullptr) { error = "Could not initialize the Guild Wars 2 API connection."; return {}; }
        WinHttpSetTimeouts(session, 5000, 5000, 8000, 12000);
        HINTERNET connection = WinHttpConnect(session, L"api.guildwars2.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET",
            L"/v2/account/homestead/decorations", nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        const std::wstring headers = L"Accept: application/json\r\nAuthorization: Bearer " + Utf8ToWide(apiKey) + L"\r\n";
        bool succeeded = request != nullptr && WinHttpSendRequest(request, headers.c_str(),
            static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE;
        if (succeeded) succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded) succeeded = WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) == TRUE;

        std::string response;
        while (succeeded)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { succeeded = false; break; }
            if (available == 0) break;
            const size_t oldSize = response.size();
            response.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.data() + oldSize, available, &read)) { succeeded = false; break; }
            response.resize(oldSize + read);
        }
        const DWORD windowsError = succeeded ? ERROR_SUCCESS : GetLastError();
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!succeeded)
        {
            error = "Could not connect to the Guild Wars 2 API (Windows error " + std::to_string(windowsError) + ").";
            return {};
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            error = "Guild Wars 2 API returned HTTP " + std::to_string(statusCode);
            const std::string detail = ApiErrorText(response);
            if (!detail.empty()) error += ": " + detail;
            error += ".";
            return {};
        }
        return response;
    }
}

Gw2AccountApiResult Gw2AccountApi::FetchHomesteadDecorationCounts(const std::string& apiKey)
{
    Gw2AccountApiResult result;
    if (apiKey.empty()) { result.error = "Enter a Guild Wars 2 API key in Settings."; return result; }
    const std::string json = Get(apiKey, result.error);
    if (!result.error.empty()) return result;

    size_t position = 0;
    while (true)
    {
        const size_t begin = json.find('{', position);
        if (begin == std::string::npos) break;
        const size_t end = FindObjectEnd(json, begin);
        if (end == std::string::npos) break;
        const int id = JsonInt(json, "id", begin, end);
        const int count = JsonInt(json, "count", begin, end, 0);
        if (id > 0) result.counts[id] = count;
        position = end + 1;
    }
    if (result.counts.empty()) result.error = "The Guild Wars 2 API returned no homestead decoration counts.";
    return result;
}
