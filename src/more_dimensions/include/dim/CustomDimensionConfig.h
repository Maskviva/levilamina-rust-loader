#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace more_dimensions::CustomDimensionConfig
{
    struct DimensionInfo
    {
        int dimId{};
        std::string sNbt;
    };

    struct Config
    {
        int version = 4;
        std::unordered_map<std::string, DimensionInfo> dimensionList{};
    };

    Config& getConfig();
    void setDimensionConfigPath();
    bool loadConfigFile();
    bool saveConfigFile();
} // namespace more_dimensions::CustomDimensionConfig
