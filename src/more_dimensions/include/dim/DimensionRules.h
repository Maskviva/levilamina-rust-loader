#pragma once

/**
 * 按维度生效的行为规则。
 *
 * 和 gamerule 的区别见 DimensionRules.cpp 的文件头：gamerule 是全服的，
 * 这里的规则是钩在真正干活的函数上、按维度判定的。
 *
 * 没有设过规则的维度**完全不受影响** —— 所有 hook 都会直接 origin()。
 */
namespace more_dimensions
{
    /**
     * 规则编号。**必须和 LeviRsAbi.h 里的 LeviRsDimRule 逐值一致**，
     * 因为 ABI 传过来的就是这个整数。只能追加，不能重排。
     */
    enum class DimRule : int
    {
        SpawnMonster = 0,
        SpawnAnimal = 1,
        SpawnSpawner = 2,
        ExplodeBlocks = 3,
        FireSpread = 4,
        MobGriefing = 5,
        Projectile = 6,
        PistonPush = 7,
        LiquidFlow = 8,
        FarmlandDecay = 9,
        Ride = 10,
        /**
         * 活塞把方块推**过地皮边界**。
         *
         * 和 `PistonPush` 是两件事，别混：`PistonPush=false` 是整个维度里活塞
         * 搬不动任何方块；`PistonCrossPlot=false` 是地皮内部照常推、跨界才拦。
         * 两条都设时任意一条禁止就推不动。
         *
         * 需要 `PlotConfine::setPlotGrid` 注册过网格才有意义；没有网格的维度
         * 这一条恒等于放行。
         */
        PistonCrossPlot = 11,
        /** 实体越过地皮边界（玩家和载人的载具不受此限，见 PlotConfine.cpp）。 */
        EntityCrossPlot = 12,
    };

    inline constexpr int kDimRuleCount = 13;

    void setDimensionRule(int dimension, int rule, bool allow);
    bool getDimensionRule(int dimension, int rule, bool* outAllow);
    void clearDimensionRules(int dimension);

    void registerDimensionRuleHooks();
    void unregisterDimensionRuleHooks();
} // namespace more_dimensions
