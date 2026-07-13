# Gui — 表单界面

> 状态：✅ 已支持。
>
> **接口来源**：本页对应 LeviLamina 自带（不是裸 Bedrock 协议）的表单封装：`ll::form::SimpleForm`、`ll::form::CustomForm`、`ll::form::ModalForm`（均在 `ll/api/form/`）。这三个类本来就是构建器风格（每个方法返回自身，可以链式调用）。Rust 侧封装为 `SimpleFormBuilder` / `CustomFormBuilder` / `ModalFormBuilder`，方法名取 LSE 风格的简短形式（`button` / `content` / `send` 等，不带 `append_`/`set_` 前缀）。

## SimpleForm — 按钮表单

`SimpleFormBuilder`：一列可点击的按钮，玩家点其中一个。链式构建，最后 `send`。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `SimpleFormBuilder::new(title)` | 新建并设置标题 | `SimpleForm::SimpleForm` |
| `.content(text)` | 设置正文 | `SimpleForm::setContent` |
| `.header(text)` / `.label(text)` / `.divider()` | 追加标题行 / 文本行 / 分隔线 | `SimpleForm::appendHeader` / `appendLabel` / `appendDivider` |
| `.button(text)` | 追加一个按钮 | `SimpleForm::appendButton` |
| `.button_with_image(text, image, image_type)` | 追加带图标的按钮 | `SimpleForm::appendButton`（带图片参数的重载） |
| `.send(player, cb)` | 发送给玩家；`cb: FnOnce(FormResponse)` | `SimpleForm::sendTo` |

结果 `FormResponse`：点了按钮为 `Button(index)`（按声明顺序的下标），玩家关闭为 `Cancelled { reason }`。

## CustomForm — 自定义表单

`CustomFormBuilder`：输入框、开关、下拉、滑块等控件的组合。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `CustomFormBuilder::new(title)` | 新建并设置标题 | `CustomForm::CustomForm` |
| `.submit(text)` | 设置提交按钮文字 | `CustomForm::setSubmitButton` |
| `.header(text)` / `.label(text)` / `.divider()` | 同 SimpleForm | `CustomForm::appendHeader` 等 |
| `.input(name, label, placeholder, default)` | 文本输入框 | `CustomForm::appendInput` |
| `.toggle(name, label, default)` | 开关 | `CustomForm::appendToggle` |
| `.dropdown(name, label, options, default_index)` | 下拉框 | `CustomForm::appendDropdown` |
| `.slider(name, label, min, max, step, default)` | 滑块 | `CustomForm::appendSlider` |
| `.step_slider(name, label, steps, default_index)` | 步进滑块（在给定的字符串档位间切换） | `CustomForm::appendStepSlider` |
| `.send(player, cb)` | 发送；`cb: FnOnce(FormResponse)` | `CustomForm::sendTo` |

结果 `FormResponse::Custom(map)`：按控件 `name` 取值的 `HashMap<String, FormValue>`；`FormValue` 为 `Int`（开关 0/1、下拉索引、步进档位索引）/ `Float`（滑块）/ `Text`（输入框）。玩家关闭为 `Cancelled { reason }`。

## ModalForm — 二选一表单

`ModalFormBuilder`：正文 + 上下两个按钮。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ModalFormBuilder::new(title, content)` | 新建 | `ModalForm::ModalForm` |
| `.upper(text)` / `.lower(text)` | 设置上 / 下按钮文字 | 对应的 `setXxx` |
| `.send(player, cb)` | 发送；`cb: FnOnce(FormResponse)` | `ModalForm::sendTo` |

结果 `FormResponse::Modal { upper }`：`upper == true` 表示选了上（主）按钮，`false` 表示下按钮。玩家关闭为 `Cancelled { reason }`。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `FormResponse` | 表单结果枚举：`Button(index)`（SimpleForm）/ `Custom(map)`（CustomForm）/ `Modal { upper }`（ModalForm）/ `Cancelled { reason }`（玩家关闭，`reason` 为原始取消码，-1 表示客户端未说明） |
| `FormValue` | `CustomForm` 单个控件的值：`Int(i64)` / `Float(f64)` / `Text(String)` |
