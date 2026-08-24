/**
 * `PlayerAttackTargetEvent` — 攻击事件，**带上被打的是谁**。
 *
 * # 为什么不用 LeviLamina 自己的 `PlayerAttackEvent`
 *
 * 它存在，也可以 cancel，唯一的问题是**载荷里读不出目标是什么**。
 * 真机上抓到的那一份：
 *
 * ```text
 * {_player:{name,pos,uuid,xuid}, cancelled:0b, cause:"EntityAttack", dim:1006,
 *  eventId:"ll::event::PlayerAttackEvent",
 *  self  :{_pointer_:2226787454976L, _type_:"Player"},
 *  target:{_pointer_:2226776030720L, _type_:"Actor"}}
 * ```
 *
 * `target` 是 LL 的 reflection 对一个 `Actor&` 的序列化 —— 一个裸指针加一个
 * **静态**类型名（永远是 `"Actor"`，因为事件就是这么声明的）。里面没有任何
 * 一格能区分「打的是玩家」和「打的是一头牛」。
 *
 * 而权限侧必须分开这两件事：
 *
 * > 分不开的话，`pvp` 旗标一关，在自己的地皮里打怪也被当成 PvP 拦下 ——
 * > 整个服务器不能战斗。反过来把攻击一律当「打生物」，PvP 就拦不住。
 *
 * 所以这里按 `InteractEntityEvent.cpp` / `PushEntityEvent.cpp` 的同一个形状
 * 加一条 bridge hook，把 `targetIsPlayer` 和 `target`（**动态**类型名）发出来。
 *
 * # 载荷
 *
 * ```text
 * {eventId, x, y, z, dim, target, targetIsPlayer, cause, _player:{name,xuid,uuid}}
 * ```
 *
 * `x/y/z` 是**目标的**位置，不是攻击者的 —— 权限问题问的是「你能不能在
 * 那一格动手」。这一条和 `InteractEntityEvent` / `RideEvent` 保持一致；
 * 不一致的话「打」和「右键」在同一只羊上会落到两块不同的地皮上。
 *
 * # Cancelling
 *
 * 返回一个值初始化的 `ActorHurtResult`（全零 = 没造成伤害），不调 origin。
 * 挥空的动画照常播 —— 玩家看到的是「打了但没伤害」，和被别的保护插件
 * 拦下时一样。
 *
 * # 两个重载都挂
 *
 * `Player::attack` 有 `(Actor&, ActorDamageCause const&)` 和多一个
 * `AttackParameters const&` 两个重载。哪个是真正的实现路径、哪个只是转发，
 * 没法在这里确认 —— 挂错一个的后果是**保护静默不生效**，而那正是这个文件
 * 要消掉的东西。所以两个都挂，代价见 `PlayerAttackHook2` 上方的说明。
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/player/Player.h"
// `ActorDamageCause` **没有独立头文件** —— 它是
// `SharedTypes::Legacy::ActorDamageCause`，由 `Player.h` 传递引入。
// `src/bridge/Players.cpp:626` 里就是这么用的，而那一处是编过的。

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& attackDef(); // fwd

        /**
         * `Player::attack` **有两个重载**，所以 `&Player::attack` 本身是歧义的
         * （C2664：无法从 overloaded-function 推导）。编译器把两个都打出来了：
         *
         * ```cpp
         * ActorHurtResult Player::attack(Actor&, ActorDamageCause const&);
         * ActorHurtResult Player::attack(Actor&, ActorDamageCause const&,
         *                                Player::AttackParameters const&);
         * ```
         *
         * 顺带纠正两处我上一版猜错的：返回值是 `ActorHurtResult` 不是 `bool`。
         *
         * 先 typedef 再 `static_cast` —— **不能把 `static_cast<A (T::*)(X, Y)>`
         * 直接写进宏**，模板参数里的逗号会被预处理器当成宏参数分隔符切开。
         *
         * # `$` 前缀：二参那个是**虚函数**
         *
         * ```text
         * static_assert failed: '...&Player::attack) is a virtual function,
         *                        you need use prefix $ workaround to hook it.'
         * ```
         *
         * LeviLamina 给每个虚函数生成一个 `$` 别名，hook 虚函数必须用它 ——
         * 这个目录里 `&Player::$drop` / `&Player::$setPlayerGameType` /
         * `&GameMode::$startDestroyBlock` 都是这么写的。
         *
         * 二参那个是 `Mob::attack` 的重写，所以是虚的。**三参那个我不知道**：
         * 编译在二参就停了，没报到它。
         *
         * ⚠ **如果三参那条也报同一个 static_assert**，把下面
         * `PlayerAttackHook3` 里的 `&Player::attack` 改成 `&Player::$attack`
         * 就行（一处，第 ~190 行）。反过来如果它报「没有匹配的函数」，
         * 说明三参不是虚函数**而且** `$attack` 只有一个 —— 那就把整个
         * `PlayerAttackHook3` 连同下面 `hook()` 里的 `r3` 一起删掉，
         * 只留虚函数那条。
         */
        using AttackFn2 = ::ActorHurtResult (Player::*)(
            ::Actor&,
            ::SharedTypes::Legacy::ActorDamageCause const&);
        using AttackFn3 = ::ActorHurtResult (Player::*)(
            ::Actor&,
            ::SharedTypes::Legacy::ActorDamageCause const&,
            ::Player::AttackParameters const&);

        /**
         * 拦下时返回什么。
         *
         * 值初始化的 `ActorHurtResult` —— 全零，也就是「没造成伤害」。
         * `InteractEntityEvent.cpp` 那边对 `InteractionResult` 用的是同一招
         * （构造一个再改字段）。**这里的字段名我不知道，所以不改，只给全零。**
         * 如果实测发现拦下之后仍然掉血，就是这个假设错了，那时候把
         * `ActorHurtResult` 的定义贴出来。
         */
        ::ActorHurtResult refusedHurt()
        {
            return ::ActorHurtResult{};
        }

        std::string buildAttackSnbt(Player& self, ::Actor& actor, int cause)
        {
            // `getTypeName` / `isPlayer` 会抛（实体正在被销毁时）。抛出去的话
            // 整台服务器在一次攻击上崩掉，所以和另外两个 hook 一样吞掉 ——
            // 订阅方读到空字符串会退回粗动作，不会更松。
            std::string targetName;
            try
            {
                targetName = actor.getTypeName();
            }
            catch (...)
            {
                targetName.clear();
            }

            bool isPlayerTarget = false;
            try
            {
                isPlayerTarget = actor.isPlayer();
            }
            catch (...)
            {
                isPlayerTarget = false;
            }

            auto const& pos = actor.getPosition();
            return std::string{"{\"eventId\":\"PlayerAttackTargetEvent\""}
                 + ",\"x\":" + std::to_string(static_cast<int>(pos.x))
                 + ",\"y\":" + std::to_string(static_cast<int>(pos.y))
                 + ",\"z\":" + std::to_string(static_cast<int>(pos.z))
                 + ",\"dim\":" + std::to_string(static_cast<int>(actor.getDimensionId()))
                 + ",\"targetIsPlayer\":" + (isPlayerTarget ? "1" : "0")
                 + ",\"target\":\"" + snbtEscape(targetName)
                 + "\",\"cause\":" + std::to_string(cause)
                 + ",\"_player\":{\"name\":\"" + snbtEscape(self.getRealName())
                 + "\",\"xuid\":\"" + snbtEscape(self.getXuid())
                 + "\",\"uuid\":\"" + snbtEscape(self.getUuid().asString()) + "\"}}";
        }

        /**
         * 两个重载**都挂**。
         *
         * 哪一个是真正的实现路径、哪一个只是转发，我没有办法在这里确认。
         * 只挂一个而挂错了的后果是**保护静默不生效** —— 这一整个文件存在的
         * 理由就是消掉那种失败。
         *
         * 双挂的代价：如果二参转发给三参，一次攻击会派发两次。放行时两次派发
         * 得到同一个答案（同一个人、同一个坐标），只是白跑一遍；拦截时外层
         * 直接返回、不调 `origin`，内层根本不会触发。
         *
         * `DropItemEvent.cpp` 用的是同一个形状（两个 hook 一个 def）。
         */
        LL_TYPE_INSTANCE_HOOK(
            PlayerAttackHook2,
            ll::memory::HookPriority::Normal,
            Player,
            // 虚函数 → `$` 别名。`static_cast` 仍然要留：`$attack` 也可能
            // 有两个（两个重载都虚的话），有 cast 才不歧义，没歧义时它也无害。
            static_cast<AttackFn2>(&Player::$attack),
            ::ActorHurtResult,
            ::Actor& actor,
            ::SharedTypes::Legacy::ActorDamageCause const& cause)
        {
            auto& def = attackDef();
            if (!def.live())
            {
                return origin(actor, cause);
            }
            if (dispatchHookEventCancellable(def, buildAttackSnbt(*this, actor, static_cast<int>(cause))))
            {
                return refusedHurt();
            }
            return origin(actor, cause);
        }

        LL_TYPE_INSTANCE_HOOK(
            PlayerAttackHook3,
            ll::memory::HookPriority::Normal,
            Player,
            static_cast<AttackFn3>(&Player::attack),
            ::ActorHurtResult,
            ::Actor& actor,
            ::SharedTypes::Legacy::ActorDamageCause const& cause,
            ::Player::AttackParameters const& params)
        {
            auto& def = attackDef();
            if (!def.live())
            {
                return origin(actor, cause, params);
            }
            if (dispatchHookEventCancellable(def, buildAttackSnbt(*this, actor, static_cast<int>(cause))))
            {
                return refusedHurt();
            }
            return origin(actor, cause, params);
        }

        HookEventDef gDef{
            "PlayerAttackTargetEvent",
            []
            {
                // `hook()` 返回 `ll::memory::hookEx` 的状态码：0 = 成功。
                // **两个都要报** —— 一个装失败、另一个成功的话，保护会在
                // 某些攻击路径上生效、另一些上不生效，而那比整个不生效更难查。
                int r2 = PlayerAttackHook2::hook();
                int r3 = PlayerAttackHook3::hook();
                auto& log = bridgeLogger();
                log.info(
                    "[AttackEvent] 安装 detour：attack/2={} (code={})，attack/3={} (code={})",
                    r2 == 0 ? "成功" : "失败", r2,
                    r3 == 0 ? "成功" : "失败", r3
                );
                if (r2 != 0 && r3 != 0)
                {
                    log.error(
                        "[AttackEvent] 两个 detour 都没装上 —— §c「打玩家」和「打生物」"
                        "在地皮上分不开，pvp 旗标拦不住人。最常见原因是本 loader 链接的 "
                        "BDS/LeviLamina 版本和服务器实际跑的不一致。");
                }
            }};
        HookEventDef& attackDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
