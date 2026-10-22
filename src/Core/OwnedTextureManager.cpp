#include "OwnedTextureManager.h"

#include "AppRuntime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winhttp.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr size_t MaximumDownloadBytes = 16 * 1024 * 1024;
    constexpr UINT MaximumTextureDimension = 8192;

    struct DecodedImage
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<unsigned char> pixels;
    };

    struct OwnedTexture
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        UINT width = 0;
        UINT height = 0;

        size_t ByteSize() const
        {
            return static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        }
    };

    enum class SourceType { File, Url };

    struct DecodeRequest
    {
        std::string key;
        SourceType sourceType = SourceType::File;
        std::filesystem::path path;
        std::string url;
        bool detail = false;
        uint64_t generation = 0;
    };

    struct DecodeJob
    {
        DecodeRequest request;
        std::future<DecodedImage> result;
    };

    ComPtr<ID3D11Device> device;
    std::unordered_map<std::string, OwnedTexture> thumbnailTextures;
    std::unordered_set<std::string> desiredThumbnails;
    std::unordered_set<std::string> queuedThumbnailKeys;
    std::unordered_set<std::string> activeThumbnailKeys;
    std::deque<DecodeRequest> decodeRequests;
    std::vector<DecodeJob> decodeJobs;
    OwnedTexture detailTexture;
    std::string detailKey;
    uint64_t detailGeneration = 0;
    size_t uploadedTextures = 0;
    size_t releasedTextures = 0;
    size_t failedTextures = 0;

    template <typename T>
    void ReleaseRaw(T*& value)
    {
        if (value != nullptr)
        {
            value->Release();
            value = nullptr;
        }
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

        HINTERNET session = WinHttpOpen(L"HomeBrowser/1.0.0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == nullptr) return false;
        WinHttpSetTimeouts(session, 5000, 5000, 8000, 15000);
        HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
        const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(
            connection, L"GET", endpoint.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        const wchar_t headers[] = L"Accept: image/jpeg,image/png,image/*\r\n";
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

    DecodedImage DecodeFrame(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame)
    {
        DecodedImage decoded;
        if (factory == nullptr || frame == nullptr || FAILED(frame->GetSize(&decoded.width, &decoded.height)) ||
            decoded.width == 0 || decoded.height == 0 ||
            decoded.width > MaximumTextureDimension || decoded.height > MaximumTextureDimension)
        {
            return {};
        }

        IWICFormatConverter* converter = nullptr;
        HRESULT result = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(result))
        {
            result = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }
        const uint64_t byteCount = static_cast<uint64_t>(decoded.width) * decoded.height * 4u;
        if (byteCount > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
            byteCount > static_cast<uint64_t>((std::numeric_limits<UINT>::max)())) result = E_OUTOFMEMORY;
        if (SUCCEEDED(result))
        {
            decoded.pixels.resize(static_cast<size_t>(byteCount));
            result = converter->CopyPixels(nullptr, decoded.width * 4u,
                static_cast<UINT>(decoded.pixels.size()), decoded.pixels.data());
        }
        ReleaseRaw(converter);
        if (FAILED(result)) return {};
        return decoded;
    }

    DecodedImage DecodeFile(const std::filesystem::path& path)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(comResult);
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        DecodedImage decoded;

        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (SUCCEEDED(result)) result = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(result)) decoded = DecodeFrame(factory, frame);

        ReleaseRaw(frame);
        ReleaseRaw(decoder);
        ReleaseRaw(factory);
        if (uninitialize) CoUninitialize();
        return decoded;
    }

    DecodedImage DecodeUrl(const std::string& url)
    {
        std::vector<unsigned char> bytes;
        if (!Download(url, bytes)) return {};

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(comResult);
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        DecodedImage decoded;

        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
        if (SUCCEEDED(result)) result = stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()));
        if (SUCCEEDED(result)) result = factory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(result)) decoded = DecodeFrame(factory, frame);

        ReleaseRaw(frame);
        ReleaseRaw(decoder);
        ReleaseRaw(stream);
        ReleaseRaw(factory);
        if (uninitialize) CoUninitialize();
        return decoded;
    }

    DecodedImage Decode(const DecodeRequest& request)
    {
        return request.sourceType == SourceType::File
            ? DecodeFile(request.path)
            : DecodeUrl(request.url);
    }

    bool CreateTexture(const DecodedImage& decoded, OwnedTexture& output)
    {
        if (device == nullptr || decoded.width == 0 || decoded.height == 0 || decoded.pixels.empty()) return false;

        D3D11_TEXTURE2D_DESC description = {};
        description.Width = decoded.width;
        description.Height = decoded.height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData = {};
        initialData.pSysMem = decoded.pixels.data();
        initialData.SysMemPitch = decoded.width * 4u;

        OwnedTexture texture;
        HRESULT result = device->CreateTexture2D(&description, &initialData, texture.texture.GetAddressOf());
        if (SUCCEEDED(result))
        {
            result = device->CreateShaderResourceView(texture.texture.Get(), nullptr, texture.view.GetAddressOf());
        }
        if (FAILED(result)) return false;
        texture.width = decoded.width;
        texture.height = decoded.height;
        output = std::move(texture);
        return true;
    }

    void QueueRequest(DecodeRequest request, bool prioritize)
    {
        if (request.detail)
        {
            if (prioritize) decodeRequests.push_front(std::move(request));
            else decodeRequests.push_back(std::move(request));
            return;
        }
        if (thumbnailTextures.find(request.key) != thumbnailTextures.end() ||
            queuedThumbnailKeys.contains(request.key) || activeThumbnailKeys.contains(request.key))
        {
            return;
        }
        queuedThumbnailKeys.insert(request.key);
        if (prioritize) decodeRequests.push_front(std::move(request));
        else decodeRequests.push_back(std::move(request));
    }

    void RemoveQueuedDetailRequests()
    {
        decodeRequests.erase(std::remove_if(decodeRequests.begin(), decodeRequests.end(), [](const DecodeRequest& request)
        {
            return request.detail;
        }), decodeRequests.end());
    }

    void ReleaseTexture(OwnedTexture& texture)
    {
        if (texture.view != nullptr || texture.texture != nullptr) ++releasedTextures;
        texture.view.Reset();
        texture.texture.Reset();
        texture.width = 0;
        texture.height = 0;
    }
}

void OwnedTextureManager::Initialize()
{
    ClearAll();
    uploadedTextures = 0;
    releasedTextures = 0;
    failedTextures = 0;
    AddonAPI_t* api = AppRuntime::GetApi();
    IDXGISwapChain* swapChain = api == nullptr ? nullptr : static_cast<IDXGISwapChain*>(api->SwapChain);
    if (swapChain != nullptr)
    {
        swapChain->GetDevice(IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
    }
}

void OwnedTextureManager::Shutdown()
{
    for (DecodeJob& job : decodeJobs)
    {
        if (job.result.valid()) job.result.wait();
    }
    decodeJobs.clear();
    decodeRequests.clear();
    queuedThumbnailKeys.clear();
    activeThumbnailKeys.clear();
    ClearAll();
    device.Reset();
}

void OwnedTextureManager::Update(bool performanceMode)
{
    const size_t maximumJobs = performanceMode ? 1u : 2u;
    const size_t maximumUploads = performanceMode ? 1u : 2u;
    size_t uploads = 0;

    for (size_t index = 0; index < decodeJobs.size();)
    {
        DecodeJob& job = decodeJobs[index];
        if (job.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++index;
            continue;
        }
        if (uploads >= maximumUploads)
        {
            ++index;
            continue;
        }

        DecodedImage decoded = job.result.get();
        DecodeRequest request = std::move(job.request);
        decodeJobs.erase(decodeJobs.begin() + static_cast<std::ptrdiff_t>(index));
        if (!request.detail) activeThumbnailKeys.erase(request.key);

        const bool stillWanted = request.detail
            ? request.generation == detailGeneration && request.key == detailKey
            : desiredThumbnails.contains(request.key);
        if (!stillWanted) continue;

        OwnedTexture texture;
        if (decoded.pixels.empty() || !CreateTexture(decoded, texture))
        {
            ++failedTextures;
            continue;
        }
        ++uploads;
        ++uploadedTextures;
        if (request.detail)
        {
            ReleaseTexture(detailTexture);
            detailTexture = std::move(texture);
        }
        else
        {
            auto existing = thumbnailTextures.find(request.key);
            if (existing != thumbnailTextures.end()) ReleaseTexture(existing->second);
            thumbnailTextures[request.key] = std::move(texture);
        }
    }

    while (decodeJobs.size() < maximumJobs && !decodeRequests.empty())
    {
        DecodeRequest request = std::move(decodeRequests.front());
        decodeRequests.pop_front();
        if (!request.detail)
        {
            queuedThumbnailKeys.erase(request.key);
            if (!desiredThumbnails.contains(request.key)) continue;
            activeThumbnailKeys.insert(request.key);
        }
        else if (request.generation != detailGeneration || request.key != detailKey)
        {
            continue;
        }
        DecodeRequest jobRequest = request;
        decodeJobs.push_back({ std::move(request), std::async(std::launch::async,
            [jobRequest = std::move(jobRequest)]() { return Decode(jobRequest); }) });
    }
}

void OwnedTextureManager::SetVisibleThumbnails(const std::vector<ThumbnailSource>& sources)
{
    desiredThumbnails.clear();
    for (const ThumbnailSource& source : sources)
    {
        desiredThumbnails.insert(source.key);
    }

    for (auto iterator = thumbnailTextures.begin(); iterator != thumbnailTextures.end();)
    {
        if (desiredThumbnails.contains(iterator->first))
        {
            ++iterator;
        }
        else
        {
            ReleaseTexture(iterator->second);
            iterator = thumbnailTextures.erase(iterator);
        }
    }

    decodeRequests.erase(std::remove_if(decodeRequests.begin(), decodeRequests.end(), [](const DecodeRequest& request)
    {
        if (request.detail) return false;
        if (desiredThumbnails.contains(request.key)) return false;
        queuedThumbnailKeys.erase(request.key);
        return true;
    }), decodeRequests.end());

    for (const ThumbnailSource& source : sources)
    {
        DecodeRequest request;
        request.key = source.key;
        request.sourceType = SourceType::File;
        request.path = source.path;
        QueueRequest(std::move(request), false);
    }
}

void OwnedTextureManager::ClearVisibleThumbnails()
{
    SetVisibleThumbnails({});
}

void* OwnedTextureManager::GetThumbnail(const std::string& key)
{
    const auto found = thumbnailTextures.find(key);
    return found == thumbnailTextures.end() ? nullptr : found->second.view.Get();
}

void OwnedTextureManager::SetDetailFromUrl(const std::string& key, const std::string& url)
{
    if (key == detailKey) return;
    ClearDetail();
    detailKey = key;
    DecodeRequest request;
    request.key = key;
    request.sourceType = SourceType::Url;
    request.url = url;
    request.detail = true;
    request.generation = detailGeneration;
    QueueRequest(std::move(request), true);
}

void OwnedTextureManager::SetDetailFromFile(const std::string& key, const std::filesystem::path& path)
{
    if (key == detailKey) return;
    ClearDetail();
    detailKey = key;
    DecodeRequest request;
    request.key = key;
    request.sourceType = SourceType::File;
    request.path = path;
    request.detail = true;
    request.generation = detailGeneration;
    QueueRequest(std::move(request), true);
}

void OwnedTextureManager::ClearDetail()
{
    ++detailGeneration;
    detailKey.clear();
    RemoveQueuedDetailRequests();
    ReleaseTexture(detailTexture);
}

void* OwnedTextureManager::GetDetail(const std::string& key)
{
    return key == detailKey ? detailTexture.view.Get() : nullptr;
}

void OwnedTextureManager::ClearAll()
{
    desiredThumbnails.clear();
    queuedThumbnailKeys.clear();
    decodeRequests.clear();
    for (auto& [key, texture] : thumbnailTextures)
    {
        (void)key;
        ReleaseTexture(texture);
    }
    thumbnailTextures.clear();
    ClearDetail();
}

bool OwnedTextureManager::IsIdle()
{
    return decodeRequests.empty() && decodeJobs.empty();
}

OwnedTextureManager::Statistics OwnedTextureManager::GetStatistics()
{
    Statistics statistics;
    statistics.thumbnailTextures = thumbnailTextures.size();
    statistics.detailTextures = detailTexture.view == nullptr ? 0u : 1u;
    statistics.queuedDecodes = decodeRequests.size();
    statistics.activeDecodes = decodeJobs.size();
    statistics.uploadedTextures = uploadedTextures;
    statistics.releasedTextures = releasedTextures;
    statistics.failedTextures = failedTextures;
    statistics.estimatedGpuBytes = detailTexture.ByteSize();
    for (const auto& [key, texture] : thumbnailTextures)
    {
        (void)key;
        statistics.estimatedGpuBytes += texture.ByteSize();
    }
    return statistics;
}
