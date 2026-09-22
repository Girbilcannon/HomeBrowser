#pragma once

#include "HomeModels.h"

#include <vector>

class IHomeProvider
{
public:
    virtual ~IHomeProvider() = default;
    virtual const ProviderDescriptor& Descriptor() const = 0;
    virtual const std::vector<HomeListing>& Listings() const = 0;
};
