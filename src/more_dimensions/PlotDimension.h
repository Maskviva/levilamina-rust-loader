#pragma once

#include "more_dimensions/Macros.h"
#include "more_dimensions/PlotLayout.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/dimension/Dimension.h"

namespace more_dimensions
{
    struct DimensionFactoryInfo;

    /**
     * 一个使用 PlotGenerator 的自定义维度。
     *
     * 结构与 SimpleCustomDimension 一致（同样的 override 集合），区别只在
     * createGenerator 返回 PlotGenerator，以及把 PlotLayout 一起存进
     * dimensions.json —— 这样重启后布局不会变，玩家已经建好的地皮也就不会
     * 因为管理员改了配置而错位。
     *
     * 这一点很重要：**布局是维度的固有属性，不是可热改的配置。**
     * Rust 侧的 `worlds.layout` 只用于新建维度和几何计算；真正决定地形
     * 长什么样的是这里存下来的这一份。
     */
    class PlotDimension final : public Dimension
    {
        uint       mSeed{0};
        PlotLayout mLayout{};

    public:
        MORE_DIMENSIONS_API PlotDimension(std::string const& name, DimensionFactoryInfo const& info);

        /** 首次注册时调用，产出存进 dimensions.json 的那份数据。 */
        MORE_DIMENSIONS_API static CompoundTag generateNewData(uint seed, PlotLayout const& layout);

        [[nodiscard]] PlotLayout const& layout() const { return mLayout; }

        void init(br::worldgen::StructureSetRegistry const&) override;
        std::unique_ptr<WorldGenerator> createGenerator(br::worldgen::StructureSetRegistry const&) override;
        void upgradeLevelChunk(ChunkSource& chunkSource, LevelChunk& oldLc, LevelChunk& newLc) override;
        void fixWallChunk(ChunkSource& cs, LevelChunk& lc) override;
        bool levelChunkNeedsUpgrade(LevelChunk const& lc) const override;
        void _upgradeOldLimboEntity(CompoundTag& tag, ::LimboEntitiesVersion vers) override;
        Vec3 translatePosAcrossDimension(Vec3 const& pos, DimensionType did) const override;
        std::unique_ptr<ChunkSource>
        _wrapStorageForVersionCompatibility(std::unique_ptr<ChunkSource> cs, ::StorageVersion ver) override;
        mce::Color getBrightnessDependentFogColor(mce::Color const& color, float brightness) const override;
        short      getCloudHeight() const override;
    };
} // namespace more_dimensions
