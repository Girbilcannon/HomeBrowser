#include "ThumbnailCache.h"

#include "AppRuntime.h"
#include "../Data/HomeModels.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")

namespace
{
    constexpr UINT ThumbnailWidth = 384;
    constexpr UINT ThumbnailHeight = 216;
    constexpr size_t MaximumDownloadBytes = 16 * 1024 * 1024;
    constexpr uintmax_t MaximumCacheBytes = 100ull * 1024ull * 1024ull;
    constexpr size_t MaximumConcurrentJobs = 3;

    struct Request
    {
        std::string key;
        std::string url;
        std::filesystem::path path;
    };

    struct Job
    {
        std::string key;
        std::future<bool> result;
    };

    std::filesystem::path cacheDirectory;
    std::deque<Request> requests;
    std::vector<Job> jobs;
    std::unordered_set<std::string> pending;
    std::unordered_set<std::string> failed;

    uint64_t Hash(const std::string& value)
    {
        uint64_t hash = 1469598103934665603ull;
        for (const unsigned char character : value)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string CacheKey(const HomeListing& listing)
    {
        return listing.uuid + "_" + std::to_string(Hash(listing.updatedAt + "|" + listing.socialThumbnailUrl));
    }

    std::filesystem::path CachePath(const std::string& key)
    {
        return cacheDirectory / (key + ".jpg");
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring output(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), output.data(), count);
        return output;
    }

    bool Download(const std::string& url, std::vector<unsigned char>& bytes)
    {
        const std::wstring wideUrl = Utf8ToWide(url);
        if (wideUrl.empty()) return false;

        URL_COMPONENTS components = {};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) return false;

        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring endpoint(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0)
        {
            endpoint.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        }

        HINTERNET session = WinHttpOpen(L"HomeBrowser/1.0.0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == nullptr) return false;
        WinHttpSetTimeouts(session, 5000, 5000, 8000, 15000);
        HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
        const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(
            connection, L"GET", endpoint.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        const wchar_t headers[] = L"Accept: image/jpeg\r\n";
        bool succeeded = request != nullptr && WinHttpSendRequest(
            request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE;
        if (succeeded) succeeded = WinHttpReceiveResponse(request, nullptr) == TRUE;

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (succeeded)
        {
            succeeded = WinHttpQueryHeaders(request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                WINHTTP_NO_HEADER_INDEX) == TRUE;
        }

        while (succeeded)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { succeeded = false; break; }
            if (available == 0) break;
            if (bytes.size() + available > MaximumDownloadBytes) { succeeded = false; break; }
            const size_t previousSize = bytes.size();
            bytes.resize(previousSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, bytes.data() + previousSize, available, &read))
            {
                succeeded = false;
                break;
            }
            bytes.resize(previousSize + read);
        }

        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return succeeded && statusCode >= 200 && statusCode < 300 && !bytes.empty();
    }

    template <typename T>
    void Release(T*& value)
    {
        if (value != nullptr)
        {
            value->Release();
            value = nullptr;
        }
    }

    bool ResizeJpeg(const std::vector<unsigned char>& bytes, const std::filesystem::path& outputPath)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(comResult);
        IWICImagingFactory* factory = nullptr;
        IWICStream* input = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* source = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICStream* output = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* properties = nullptr;

        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (SUCCEEDED(result)) result = factory->CreateStream(&input);
        if (SUCCEEDED(result)) result = input->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
        if (SUCCEEDED(result)) result = factory->CreateDecoderFromStream(
            input, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, &source);
        if (SUCCEEDED(result)) result = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(result)) result = scaler->Initialize(
            source, ThumbnailWidth, ThumbnailHeight, WICBitmapInterpolationModeFant);

        std::filesystem::path temporaryPath = outputPath;
        temporaryPath += ".tmp";
        std::error_code fileError;
        std::filesystem::remove(temporaryPath, fileError);
        if (SUCCEEDED(result)) result = factory->CreateStream(&output);
        if (SUCCEEDED(result)) result = output->InitializeFromFilename(temporaryPath.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
        if (SUCCEEDED(result)) result = encoder->Initialize(output, WICBitmapEncoderNoCache);
        if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &properties);
        if (SUCCEEDED(result)) result = frame->Initialize(properties);
        if (SUCCEEDED(result)) result = frame->SetSize(ThumbnailWidth, ThumbnailHeight);
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
        if (SUCCEEDED(result)) result = frame->WriteSource(scaler, nullptr);
        if (SUCCEEDED(result)) result = frame->Commit();
        if (SUCCEEDED(result)) result = encoder->Commit();

        Release(properties);
        Release(frame);
        Release(encoder);
        Release(output);
        Release(scaler);
        Release(source);
        Release(decoder);
        Release(input);
        Release(factory);
        if (uninitialize) CoUninitialize();

        if (FAILED(result))
        {
            std::filesystem::remove(temporaryPath, fileError);
            return false;
        }
        std::filesystem::rename(temporaryPath, outputPath, fileError);
        if (fileError)
        {
            std::filesystem::remove(temporaryPath, fileError);
            return std::filesystem::exists(outputPath);
        }
        return true;
    }

    bool CreateThumbnail(const Request& request)
    {
        std::vector<unsigned char> bytes;
        return Download(request.url, bytes) && ResizeJpeg(bytes, request.path);
    }

    void PruneCache()
    {
        struct CachedFile
        {
            std::filesystem::path path;
            std::filesystem::file_time_type modified;
            uintmax_t size = 0;
        };
        std::vector<CachedFile> files;
        uintmax_t total = 0;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cacheDirectory, error))
        {
            if (error || !entry.is_regular_file() || entry.path().extension() != ".jpg") continue;
            CachedFile file { entry.path(), entry.last_write_time(error), entry.file_size(error) };
            if (error) { error.clear(); continue; }
            total += file.size;
            files.push_back(std::move(file));
        }
        std::sort(files.begin(), files.end(), [](const CachedFile& left, const CachedFile& right)
        {
            return left.modified < right.modified;
        });
        for (const CachedFile& file : files)
        {
            if (total <= MaximumCacheBytes) break;
            std::filesystem::remove(file.path, error);
            if (!error) total -= file.size;
            error.clear();
        }
    }
}

void ThumbnailCache::Initialize()
{
    cacheDirectory = std::filesystem::path(AppRuntime::GetAddonDirectory()) / "cache" / "thumbnails";
    std::error_code error;
    std::filesystem::create_directories(cacheDirectory, error);
    PruneCache();
}

void ThumbnailCache::Shutdown()
{
    for (Job& job : jobs)
    {
        if (job.result.valid()) job.result.wait();
    }
    jobs.clear();
    requests.clear();
    pending.clear();
    failed.clear();
    cacheDirectory.clear();
}

void ThumbnailCache::Update()
{
    for (size_t index = 0; index < jobs.size();)
    {
        Job& job = jobs[index];
        if (job.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++index;
            continue;
        }
        const bool succeeded = job.result.get();
        pending.erase(job.key);
        if (!succeeded) failed.insert(job.key);
        jobs.erase(jobs.begin() + static_cast<std::ptrdiff_t>(index));
    }

    while (jobs.size() < MaximumConcurrentJobs && !requests.empty())
    {
        Request request = std::move(requests.front());
        requests.pop_front();
        const std::string key = request.key;
        jobs.push_back({ key, std::async(std::launch::async, [request = std::move(request)]()
        {
            return CreateThumbnail(request);
        }) });
    }
}

void ThumbnailCache::Prefetch(const HomeListing& listing)
{
    if (listing.socialThumbnailUrl.empty() || cacheDirectory.empty()) return;
    const std::string key = CacheKey(listing);
    const std::filesystem::path path = CachePath(key);
    if (std::filesystem::exists(path)) return;

    if (pending.find(key) == pending.end() && failed.find(key) == failed.end())
    {
        pending.insert(key);
        requests.push_back({ key, listing.socialThumbnailUrl, path });
    }
}

Texture_t* ThumbnailCache::GetThumbnail(const HomeListing& listing)
{
    if (listing.socialThumbnailUrl.empty() || cacheDirectory.empty()) return nullptr;
    const std::string key = CacheKey(listing);
    const std::filesystem::path path = CachePath(key);
    if (!std::filesystem::exists(path))
    {
        Prefetch(listing);
        return nullptr;
    }
    AddonAPI_t* api = AppRuntime::GetApi();
    return api == nullptr || api->Textures_GetOrCreateFromFile == nullptr
        ? nullptr
        : api->Textures_GetOrCreateFromFile(("HOMEBROWSER_THUMB_" + key).c_str(), path.string().c_str());
}

bool ThumbnailCache::HasFailed(const HomeListing& listing)
{
    return !listing.socialThumbnailUrl.empty() && failed.find(CacheKey(listing)) != failed.end();
}
