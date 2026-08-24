# Gui — 表单

三种基岩版原生表单，都是 builder 风格，结果通过回调返回。

回调是 `FnOnce`，**恰好触发一次，在服务器线程上**——或者一次也不触发，如果模组在玩家回应之前被卸载了。

## SimpleForm — 按钮列表

```rust
use levilamina::prelude::*;

SimpleFormBuilder::new("§l主菜单")
    .content("选一个：")
    .button("传送")
    .button_with_image("商店", "textures/items/emerald", "path")
    .button("关闭")
    .send(&player, |resp| {
        match resp {
            FormResponse::Button(0) => { /* 传送 */ }
            FormResponse::Button(1) => { /* 商店 */ }
            _ => {}
        }
    })?;
```

| 方法 | 说明 |
| --- | --- |
| `.content(text)` | 顶部说明文字 |
| `.button(text)` | 一个按钮 |
| `.button_with_image(text, image, image_type)` | 带图标，`image_type` 传 `"path"` 或 `"url"` |
| `.header(text)` / `.label(text)` / `.divider()` | 排版元素 |

::: warning 按钮索引会被排版元素影响吗
不会。`FormResponse::Button(i)` 里的 `i` 只数**按钮**，`header` / `label` / `divider` 不占号。
:::

## CustomForm — 表单控件

```rust
CustomFormBuilder::new("§l设置")
    .input("nick", "昵称", "输入昵称", "")
    .toggle("pvp", "开启 PVP", false)
    .dropdown("mode", "模式", &["和平", "普通", "困难"], 1)
    .slider("radius", "半径", 1.0, 64.0, 1.0, 16.0)
    .step_slider("quality", "画质", &["低", "中", "高"], 2)
    .submit("保存")
    .send(&player, |resp| {
        if let FormResponse::Custom(map) = resp {
            let pvp = map.get("pvp").and_then(|v| v.as_bool()).unwrap_or(false);
            let mode = map.get("mode").and_then(|v| v.as_index()).unwrap_or(0);
        }
    })?;
```

每个控件的第一个参数是**名字**，回调里按名字取值——不是按下标。加删控件不会打乱已有代码。

| 方法 | 值类型 |
| --- | --- |
| `.input(name, text, placeholder, default)` | `Text(String)` |
| `.toggle(name, text, default)` | `Int`（0/1） |
| `.dropdown(name, text, &options, default)` | `Choice { index, text }` |
| `.slider(name, text, min, max, step, default)` | `Float` |
| `.step_slider(name, text, &steps, default)` | `Choice { index, text }` |

`FormValue` 的取值方法：`as_i64()` `as_f64()` `as_bool()` `as_str()` `as_index()`。

`as_index()` 只对下拉和步进滑块有意义，其他一律返回 `None`。

## ModalForm — 两个按钮

```rust
ModalFormBuilder::new("§c确认", "真的要删除这块地皮吗？")
    .upper("确认删除")
    .lower("取消")
    .send(&player, |resp| {
        if let FormResponse::Modal { upper: true } = resp {
            // 确认
        }
    })?;
```

只有两个按钮，不能多也不能少。破坏性操作的二次确认用它。

## FormResponse

```rust
pub enum FormResponse {
    Cancelled { reason: i32 },     // 玩家按了 ESC，或客户端拒绝
    Button(usize),                 // SimpleForm
    Custom(HashMap<String, FormValue>),  // CustomForm
    Modal { upper: bool },         // ModalForm
}
```

::: tip 一定要处理 Cancelled
玩家随时可以按 ESC。如果你的流程依赖表单结果（比如"选完世界才能继续"），`Cancelled` 分支要么什么都不做，要么退回上一级菜单——不要假设一定有结果。
:::

## 表单链

一个表单的回调里再开下一个表单是完全正常的用法：

```rust
SimpleFormBuilder::new("选择世界")
    .button("世界 A")
    .send(&player, move |resp| {
        if let FormResponse::Button(i) = resp {
            SimpleFormBuilder::new("选择操作")
                .button("传送")
                .send(&player, |_| {})
                .ok();
        }
    })?;
```

注意闭包里要用的 `Player` 需要 `move` 进去——`Player` 可以克隆，成本很低。
