#pragma once

#include <string>

namespace XmlDownloader
{
    void Update();
    void Shutdown();
    bool Start(
        const std::string& url,
        const std::string& destinationFolder,
        const std::string& preferredName);
    bool IsBusy();
    const std::string& Status();
}
