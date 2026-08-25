/**
 * bridge/hooks/protect/TakeEntityEvent.cpp — "PlayerTakeEntityEvent"：玩家正要
 * 把一个**实体**收进物品栏，**可取消**。
 *
 * # 为什么需要它，而不是用 PlayerPickUpItemEvent
 *
 * LeviLamina 的 `PlayerPickUpItemEvent` 也挂在 `Player::take` 上，但里面有一道
 * 门（`ll/api/event/player/PlayerPickUpItemEvent.cpp`）：
 *
 * ```cpp
 * if (itemActor.hasCategory(ActorCategory::Item)) {
 *     auto ev = PlayerPickUpItemEvent(...);
 *     EventBus::getInstance().publish(ev);
 *     if (ev.isCancelled()) return false;
 * }
 * return origin(itemActor, orgCount, favoredSlot);   // ← 非 Item 类走这里
 * ```
 *
 * **射出去的箭矢和三叉戟不是 `ActorCategory::Item`。** 它们是投射物实体
 * （`minecraft:arrow` / `minecraft:thrown_trident`），落地后仍然是投射物，捡
 * 起来同样走 `Player::take`，但 `hasCategory(Item)` 为假，于是那个事件**根本
 * 不发布**。
 *
 * 这个失效方式特别难查：订阅是成功的（日志里不会有任何异常），保护对掉落物完
 * 全正常，只有箭矢和三叉戟悄悄穿过去。看起来像"保护偶尔失灵"，其实是这一类实
 * 体从来就没进过那条判定。
 *
 * 所以这里挂同一个 `Player::take`，但**不按类别过滤**，把实体类型如实报给
 * Rust 侧，由它自己决定拦不拦。
 *
 * # 和 PlayerPickUpItemEvent 的关系
 *
 * 两个 hook 会同时挂在 `Player::take` 上。掉落物会**两个都触发** —— 这是有意
 * 的：现有 mod 订阅的 `PlayerPickUpItemEvent` 行为不变，新订阅这个的能多拿到
 * 箭矢/三叉戟。任何一个取消掉，`take` 就返回 false。
 *
 * 想只处理"原来漏掉的那部分"，按 payload 里的 `isItemActor` 过滤即可。
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, entity, entityId, isItemActor, item, _player:{name,xuid,uuid}}
 * ```
 *
 * - `entity` —— 被捡实体的类型名（`minecraft:arrow` 等）
 * - `isItemActor` —— 是不是掉落物。false 就是 PlayerPickUpItemEvent 漏掉的那类
 * - `item` —— 掉落物时是里面的物品名；非掉落物时为空串
 *
 * `x/y/z` 是玩家位置的整数，和本目录其它 hook 一致（LL 的反射把 Vec3 序列化成
 * JSON **数组**，按 `{x,y,z}` 读的消费方什么都读不到）。
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <set>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/projectile/Arrow.h"
#include "mc/world/actor/projectile/ThrownTrident.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& takeDef(); // fwd —— gDef 定义在本文件末尾

        /** 每种实体类型打一次到达证明，让「箭矢有没有走到这里」是可确认的事实。 */
        void logFirstTouch(Actor const& a);

        /** 被捡实体的类型名。取不到就给空串而不是猜。 */
        std::string safeActorType(Actor const& a)
        {
            try
            {
                return std::string{a.getTypeName()};
            }
            catch (std::exception const&)
            {
                return {};
            }
        }

        /** 掉落物里装的是什么。非掉落物返回空串。 */
        std::string carriedItemName(Actor const& a, bool isItem)
        {
            if (!isItem) return {};
            try
            {
                auto const& stack = static_cast<ItemActor const&>(a).item();
                return std::string{stack.getTypeName()};
            }
            catch (std::exception const&)
            {
                return {};
            }
        }

        std::string buildSnbt(Player& p, Actor const& taken, bool isItem)
        {
            auto const pos = p.getPosition();
            return std::string{"{\"eventId\":\"PlayerTakeEntityEvent\",\"x\":"}
                 + snbtNum(static_cast<int>(pos.x)) + ",\"y\":" + snbtNum(static_cast<int>(pos.y))
                 + ",\"z\":" + snbtNum(static_cast<int>(pos.z))
                 + ",\"dim\":" + snbtNum(static_cast<int>(p.getDimensionId()))
                 + ",\"entity\":\"" + snbtEscape(safeActorType(taken))
                 + "\",\"entityId\":" + snbtNum(static_cast<int64_t>(taken.getOrCreateUniqueID().rawID))
                 + "L,\"isItemActor\":" + (isItem ? "true" : "false")
                 + ",\"item\":\"" + snbtEscape(carriedItemName(taken, isItem))
                 + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                 + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                 + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";
        }

        void logFirstTouch(Actor const& a)
        {
            // 每种实体类型一次。这条日志的用途是把「箭矢到底有没有走到这里」
            // 变成可以确认的事实 —— 「hook 没装上」「装错了函数」「装对了但判定
            // 放行」三种情况的现象完全一样，没有它只能靠猜，而上一版正是挂错了
            // 函数（Player::take）却看不出来。
            static std::set<std::string> seen;
            std::string const key = safeActorType(a);
            if (seen.insert(key).second)
            {
                bridgeLogger().info("[TakeEntityEvent] 首次触碰 '{}'", key);
            }
        }

        /**
         * 拦一类投射物的拾取。
         *
         * # 为什么挂 playerTouch 而不是 Player::take
         *
         * 上一版挂的是 `Player::take` —— 那是 `ItemActor::playerTouch` 内部调
         * 的函数，掉落物走它。但 `Arrow::playerTouch` 和
         * `ThrownTrident::playerTouch` 是**各自独立的实现**，它们直接把物品塞
         * 进背包，根本不经过 `Player::take`。所以那个 hook 对箭矢一次都没触发
         * 过 —— 装是装上了，只是挂错了地方。
         *
         * `playerTouch` 是 `Actor` 上的虚函数，每个子类各有一份实现，所以要按
         * 具体类分别挂。
         *
         * 返回 void，没法"取消"—— 拦截方式是**不调用 origin**：不调用就等于这
         * 次触碰什么都没发生，实体留在原地，玩家什么也没拿到。
         */
#define LEVI_RS_PICKUP_HOOK(HookName, ActorClass, HeaderGuardName)                                 \
    LL_TYPE_INSTANCE_HOOK(                                                                         \
        /* 虚函数必须挂 $ 前缀那份 —— LeviLamina 用它绕开 vtable 派发；          \
         * 直接取 &Cls::playerTouch 会被 static_assert 拦下。同 DropItemEvent。 */ \
        HookName, ll::memory::HookPriority::Normal, ActorClass, &ActorClass::$playerTouch, void, \
        ::Player& player)                                                                          \
    {                                                                                              \
        auto& def = takeDef();                                                                     \
        if (!def.live())                                                                           \
        {                                                                                          \
            origin(player);                                                                        \
            return;                                                                                \
        }                                                                                          \
        logFirstTouch(*this);                                                                      \
        if (dispatchHookEventCancellable(def, buildSnbt(player, *this, false)))                    \
        {                                                                                          \
            /* 不调 origin = 这次触碰什么都没发生。实体留在原地，可以再试。 */                     \
            return;                                                                                \
        }                                                                                          \
        origin(player);                                                                            \
    }

        LEVI_RS_PICKUP_HOOK(ArrowPickupHook, Arrow, arrow)
        LEVI_RS_PICKUP_HOOK(TridentPickupHook, ThrownTrident, trident)

#undef LEVI_RS_PICKUP_HOOK

        HookEventDef gDef{
            "PlayerTakeEntityEvent",
            []
            {
                // 和 DropItemEvent 一样显式装、显式报状态：0 == 成功。
                // 装失败必须看得见 —— 一个没装上的保护和「装上了但从不拦」在
                // 行为上完全一样，而这正是箭矢那个 bug 拖了这么久的原因。
                int ra = ArrowPickupHook::hook();
                int rt = TridentPickupHook::hook();
                auto& log = bridgeLogger();
                log.info(
                    "[TakeEntityEvent] 安装 detour：Arrow::$playerTouch={} (code={})，"
                    "ThrownTrident::$playerTouch={} (code={})",
                    ra == 0 ? "成功" : "失败", ra,
                    rt == 0 ? "成功" : "失败", rt);
                if (ra != 0 || rt != 0)
                {
                    log.error(
                        "[TakeEntityEvent] 原生 detour 安装失败（非 0 状态码）。"
                        "结果：拾取保护对**箭矢、三叉戟等投射物**完全不生效"
                        "（掉落物仍由 PlayerPickUpItemEvent 覆盖）。"
                        "最常见原因是本 loader 链接的 BDS/LeviLamina 版本与服务器"
                        "实际运行的版本不一致，导致 $playerTouch 的符号地址解析错误。");
                }
            }};

        HookEventDef& takeDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
