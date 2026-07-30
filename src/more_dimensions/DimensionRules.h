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
        SpawnMonster  = 0,
        SpawnAnimal   = 1,
        SpawnSpawner  = 2,
        ExplodeBlocks = 3,
        FireSpread    = 4,
        MobGriefing   = 5,
        Projectile    = 6,
        PistonPush    = 7,
        LiquidFlow    = 8,
        FarmlandDecay = 9,
        Ride          = 10,
    };

    inline constexpr int kDimRuleCount = 11;

    void setDimensionRule(int dimension, int rule, bool allow);
    bool getDimensionRule(int dimension, int rule, bool* outAllow);
    void clearDimensionRules(int dimension);

    void registerDimensionRuleHooks();
    void unregisterDimensionRuleHooks();
} // namespace more_dimensions
