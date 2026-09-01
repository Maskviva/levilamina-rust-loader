#include "more_dimensions/include/dim/CustomDimensionConfig.h"

#include <fstream>
#include <stdexcept>

#include "ll/api/Config.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/utils/Base64Utils.h"
#include "ll/api/utils/ErrorUtils.h"
#include "ll/api/utils/StringUtils.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/server/PropertiesSettings.h"

#include "more_dimensions/include/base/Utils.h"

namespace more_dimensions::CustomDimensionConfig
{
    static ll::io::Logger& logger()
    {
        static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("more_dimensions");
        return *log;
    }

    static std::filesystem::path dimensionConfigPath{u8"./worlds"};

    Config& getConfig()
    {
        static Config instance;
        return instance;
    }

    void setDimensionConfigPath()
    {
        if (!ll::service::getLevel())
        {
            throw std::runtime_error("Level nullptr");
        }
        dimensionConfigPath /= ll::string_utils::str2u8str(ll::service::getPropertiesSettings()->mLevelName);
        dimensionConfigPath /= u8"dimension_config.json";
    }

    bool loadConfigFile()
    {
        if (std::ifstream(dimensionConfigPath).good())
        {
            try
            {
                if (ll::config::loadConfig(
                    getConfig(),
                    dimensionConfigPath,
                    [](Config& config, nlohmann::ordered_json& data)
                    {
                        if (data["version"] < config.version)
                        {
                            for (auto& item : data["dimensionList"])
                            {
                                auto decompressed = utils::decompress(ll::base64_utils::decode(item["base64Nbt"]));
                                auto nbtTag = CompoundTag::fromBinaryNbt(decompressed);
                                if (!nbtTag)
                                {
                                    continue;
                                }
                                item["sNbt"] = nbtTag->toSnbt(SnbtFormat::Minimize);
                                item.erase("base64Nbt");
                            }
                        }
                        data.erase("version");
                        auto patch = ll::reflection::serialize<nlohmann::ordered_json>(config);
                        patch.value().merge_patch(data);
                        data = *std::move(patch);
                        return true;
                    }
                ))
                {
                    return true;
                }
            }
            catch (...)
            {
                ll::error_utils::printCurrentException(logger());
            }
        }
        try
        {
            return ll::config::saveConfig(getConfig(), dimensionConfigPath);
        }
        catch (...)
        {
            return false;
        }
    }

    bool saveConfigFile()
    {
        try
        {
            printf("saveConfigFile::: %ls", dimensionConfigPath.c_str());
            return ll::config::saveConfig(getConfig(), dimensionConfigPath);
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace more_dimensions::CustomDimensionConfig
