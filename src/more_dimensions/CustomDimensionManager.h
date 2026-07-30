#pragma once

#include <functional>
#include <memory>
#include <string>

#include "more_dimensions/Macros.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/dimension/DimensionType.h"

class Dimension;
class DerivedDimensionArguments;

namespace more_dimensions
{
    struct DimensionFactoryInfo
    {
        DerivedDimensionArguments& arguments;
        CompoundTag const&         data;
        DimensionType              dimId;
    };

    class CustomDimensionManager
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

        CustomDimensionManager();
        ~CustomDimensionManager();

    public:
        using DimensionFactoryT = std::shared_ptr<Dimension>(DimensionFactoryInfo const&);

    protected:
        MORE_DIMENSIONS_API DimensionType addDimension(
            std::string const&                  dimName,
            std::function<DimensionFactoryT>    factory,
            std::function<CompoundTag()> const& newData
        );

    public:
        MORE_DIMENSIONS_API static CustomDimensionManager& getInstance();

        [[deprecated("please use VanillaDimensions::fromString")]] MORE_DIMENSIONS_API static DimensionType
        getDimensionIdFromName(std::string const& dimName);

        template <std::derived_from<Dimension> D, class... Args>
        DimensionType addDimension(std::string const& dimName, Args&&... args)
        {
            return addDimension(
                dimName,
                [dimName](more_dimensions::DimensionFactoryInfo const& info) -> std::shared_ptr<Dimension> {
                    return std::make_shared<D>(dimName, info);
                },
                [&] { return D::generateNewData(std::forward<Args>(args)...); }
            );
        }
    };
} // namespace more_dimensions
