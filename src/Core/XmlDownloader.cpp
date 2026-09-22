#include "XmlDownloader.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#pragma comment(lib, "Winhttp.lib")

namespace
{
    constexpr size_t MaximumXmlBytes = 32 * 1024 * 1024;

    struct Result
    {
        bool success = false;
        std::string message;
    };

    std::future<Result> downloadJob;
    std::string status;

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring output(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count);
        return output;
    }

    std::filesystem::path Utf8Path(const std::string& value)
    {
#ifdef _WIN32
        return std::filesystem::path(Utf8ToWide(value));
#else
        return std::filesystem::path(value);
#endif
    }

    std::string SafeStem(std::string value)
    {
        for (char& character : value)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (byte < 32 || character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' || character == '|' ||
                character == '?' || character == '*')
            {
                character = '_';
            }
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '.')) value.pop_back();
        return value.empty() ? "Homestead" : value;
    }

    bool Download(const std::string& url, std::vector<unsigned char>& bytes, std::string& error)
    {
        const std::wstring wideUrl = Utf8ToWide(url);
        URL_COMPONENTS components = {};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (wideUrl.empty() || !WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components))
        {
            error = "The XML download URL was invalid.";
            return false;
        }

        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring endpoint(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0)
            endpoint.append(components.lpszExtraInfo, components.dwExtraInfoLength);

        HINTERNET session = WinHttpOpen(L"HomeBrowser/1.0.0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == nullptr) { error = "Could not initialize the XML download."; return false; }
        WinHttpSetTimeouts(session, 5000, 5000, 8000, 30000);
        HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
        const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET",
            endpoint.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        const wchar_t headers[] = L"Accept: application/xml,text/xml,*/*\r\n";
        bool succeeded = request != nullptr && WinHttpSendRequest(request, headers,
            static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE;
        if (succeeded) succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded) succeeded = WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) == TRUE;

        while (succeeded)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { succeeded = false; break; }
            if (available == 0) break;
            if (bytes.size() + available > MaximumXmlBytes)
            {
                error = "The XML file exceeded the 32 MB safety limit.";
                succeeded = false;
                break;
            }
            const size_t oldSize = bytes.size();
            bytes.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, bytes.data() + oldSize, available, &read)) { succeeded = false; break; }
            bytes.resize(oldSize + read);
        }

        const DWORD windowsError = succeeded ? ERROR_SUCCESS : GetLastError();
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!succeeded)
        {
            if (error.empty()) error = "The XML download failed (Windows error " + std::to_string(windowsError) + ").";
            return false;
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            error = "gw2.homes returned HTTP " + std::to_string(statusCode) + " for the XML download.";
            return false;
        }
        if (bytes.empty() || std::find(bytes.begin(), bytes.end(), static_cast<unsigned char>('<')) == bytes.end())
        {
            error = "gw2.homes did not return a valid XML document.";
            return false;
        }
        return true;
    }

    Result Run(const std::string& url, const std::string& folderText, const std::string& preferredName)
    {
        Result result;
        std::vector<unsigned char> bytes;
        if (!Download(url, bytes, result.message)) return result;

        const std::filesystem::path folder = Utf8Path(folderText);
        std::error_code fileError;
        std::filesystem::create_directories(folder, fileError);
        if (fileError)
        {
            result.message = "The configured XML folder could not be created or opened.";
            return result;
        }

        const std::string stem = SafeStem(preferredName);
        std::filesystem::path output = folder / Utf8Path(stem + ".xml");
        for (int suffix = 2; std::filesystem::exists(output, fileError); ++suffix)
            output = folder / Utf8Path(stem + " (" + std::to_string(suffix) + ").xml");

        std::filesystem::path temporary = output;
        temporary += L".part";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            result.message = "The XML file could not be created in the configured folder.";
            return result;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file)
        {
            std::filesystem::remove(temporary, fileError);
            result.message = "The XML file could not be written completely.";
            return result;
        }
        std::filesystem::rename(temporary, output, fileError);
        if (fileError)
        {
            std::filesystem::remove(temporary, fileError);
            result.message = "The completed XML file could not be moved into place.";
            return result;
        }

        result.success = true;
        result.message = "XML download complete.";
        return result;
    }
}

void XmlDownloader::Update()
{
    if (!downloadJob.valid() || downloadJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { status = downloadJob.get().message; }
    catch (...) { status = "The background XML download failed."; }
}

void XmlDownloader::Shutdown()
{
    if (downloadJob.valid()) downloadJob.wait();
    downloadJob = {};
    status.clear();
}

bool XmlDownloader::Start(const std::string& url, const std::string& destinationFolder, const std::string& preferredName)
{
    if (downloadJob.valid() || url.empty()) return false;
    if (destinationFolder.empty()) { status = "Set the XML download folder in Settings first."; return false; }
    status = "Downloading XML...";
    downloadJob = std::async(std::launch::async, [url, destinationFolder, preferredName]()
    {
        return Run(url, destinationFolder, preferredName);
    });
    return true;
}

bool XmlDownloader::IsBusy() { return downloadJob.valid(); }
const std::string& XmlDownloader::Status() { return status; }
