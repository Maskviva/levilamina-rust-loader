# 运行时加载 / 卸载 / 重载（`/llr`）

## 命令

```
/llr list                          # 重新扫描 mods/ 目录并列出所有 rust mod
/llr load   <mod_name>             # 从 mods/<mod_name>/ 加载并启用
/llr unload <mod_name> [--cascade] # 卸载（默认拒绝有依赖方的情况）
/llr reload <mod_name> [--cascade] # 卸载 + 重新加载（仅限 reload_safe）
```

权限：`Host`，和 `/levirs` 一致。

## 三条设计约定

### 1. reload 不换代码，只重置状态

`reload` 走的是现有路径：`disable` → `on_unload` → `FreeLibrary` →
`LoadLibrary` → `levi_rs_main` → `on_enable`。它重跑初始化、重读配置、清空内存
状态，但**不做 dll 拷贝**，所以不要指望它能加载你刚编译出来的新代码。

开发时要热换新代码，走这条路：

```
/llr unload <name>          # 先卸载，释放对 dll 的占用
  ... 把新编译的 dll 覆盖进 mods/<name>/ ...
/llr list                   # 重新扫描目录，刷新列表
/llr load <name>
```

**为什么这条路也不保证成功**：Windows 的 `FreeLibrary` 是引用计数的，不是硬卸载。
只要 mod 里还有没 join 的线程、没跑完的 TLS 析构器、没释放的 COM 对象，或者 CRT
静态持有着指向该 dll 的指针，映像就不会真正 unmap，下一次 `LoadLibrary` 会返回
**同一个基址** —— 你以为换了新代码，实际还在跑旧的，而且没有任何报错。

loader 会检测这件事：`/llr load` 时如果发现基址和上次 `unload` 前一样，会直接报警告。
看到那条警告就意味着必须重启服务器才能真正生效。

### 2. `reload` 只对显式声明的 mod 开放

在 `manifest.json` 顶层加：

```json
{
    "name": "my-mod",
    "entry": "my_mod.dll",
    "type": "rust",
    "reload_safe": true
}
```

没写这个字段的 mod，`/llr reload` 会直接拒绝，只允许 `unload` / `load`（冷路径，
风险自负）。`--cascade` 时依赖方也必须各自声明 `reload_safe`，不能从后门被顺带
reload。

这个字段对 LeviLamina 本身是**不可见**的：LL 的反序列化器遍历 `ll::mod::Manifest`
的成员去 JSON 里查找，从不遍历 JSON 自己的 key，所以多出来的顶层字段会被安静忽略，
不会影响正常启动加载。（现有 manifest 里的 `"minecraft"` 字段就是这么活着的。）

### 3. `unload` 默认拒绝，级联要显式

如果有别的 mod 在 `dependencies` 里写了目标 mod，`unload` 会拒绝并列出依赖方。
加 `--cascade` 才会按拓扑序先卸依赖方再卸目标。

依赖检查覆盖**所有**已加载的 mod，包括原生 C++ mod —— C++ mod 一样可以依赖 rust mod。
但 `--cascade` 只能卸载本 loader 管的 rust mod；依赖方里出现 C++ mod 时会直接拒绝，
因为我们无权卸载别的 manager 的 mod。

---

## 想被标成 `reload_safe`，mod 必须做到什么

loader 只能管它自己发出去的东西。下面这些是**代码层面强制不了、只能靠 mod 自觉**的：

| 项目 | 要求 |
|---|---|
| 后台线程 | `on_unload` 里必须 join 掉所有 `std::thread::spawn` 出来的线程。dylib 卸载后线程醒来，指令指针落在被回收的内存上，直接 UAF。 |
| 定时器 | 用 `Server::schedule*` 返回的 `TaskId` 在 `on_disable` / `on_unload` 里 `cancel_task`。可以用 `pending_tasks()` 自检是否清干净。 |
| 全局状态 | `Lazy<Mutex<..>>` 之类的全局表必须能被重新初始化。注意 Rust 的 `OnceCell` / `OnceLock` 一旦 set 过，在同一进程里**不会**因为 dll 重载而复位（如果映像没真正卸载的话）。 |
| 打开的 handle | socket、文件、数据库连接全部关闭。 |
| 注册的回调 | 命令、表单、事件监听、packet hook、kvdb 句柄由 loader 统一回收（见 `onRustModGone`），这部分不用你管。 |

### loader 这边已经做了什么

`onRustModGone` 会在卸载时逐个清理：

1. **scheduler**（本次新增）—— 丢弃该 mod 所有待执行任务
2. commands —— 绑定置空，后续调用返回错误而不是野指针
3. forms —— 清空待回调票据
4. kvdb —— 强制关闭遗留句柄并告警
5. hook events —— 解绑事件订阅
6. packet hooks —— 解绑原始包拦截

scheduler 排在最前，因为排队中的任务可能回调进后面任意一个子系统。

### 关于 `schedule` 的 ABI 变化

旧的 `schedule` / `schedule_after` 槽位签名里**没有 mod handle**，所以在原理上就
无法做成卸载安全的 —— loader 根本不知道任务是谁排的。这两个槽位为了 ABI 兼容保留
着，但用它们的 mod 不能标 `reload_safe`。

新增的 `schedule_for` / `schedule_after_for` / `schedule_cancel` /
`schedule_pending_count` 带 mod 归属。`levilamina` crate 的 `Server::schedule` /
`schedule_after` 已经改走新槽位，所以**mod 源码不用改，重新编译一次即可**，只是
返回值从 `()` 变成了 `TaskId`（作为语句调用时无影响）。

`LEVI_RS_ABI_VERSION` 保持 `5`，只有 `struct_size` 变大。
