# Gui — 表单界面

> 状态：🧩 规划。
>
> **接口来源**：本页对应 LeviLamina 自带（不是裸 Bedrock 协议）的表单封装：`ll::form::SimpleForm`、`ll::form::CustomForm`、`ll::form::ModalForm`（均在 `ll/api/form/`）。这三个类本来就是构建器风格（每个 `append_*`/`set_*` 返回自身，可以链式调用），命名直接沿用，只转成 snake_case。

## SimpleForm — 按钮表单

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `SimpleForm::new(title, content?)` | 新建 | `SimpleForm::SimpleForm` |
| `form.set_title(title)` / `set_content(text)` | 设置标题 / 正文 | `SimpleForm::setTitle` / `setContent` |
| `form.append_header(text)` / `append_label(text)` / `append_divider()` | 追加标题行 / 文本行 / 分隔线 | `SimpleForm::appendHeader` / `appendLabel` / `appendDivider` |
| `form.append_button(text, on_click?)` | 追加按钮，可选每按钮独立回调 | `SimpleForm::appendButton` |
| `form.append_button_with_image(text, image, image_type, on_click?)` | 追加带图标的按钮 | `SimpleForm::appendButton`（带图片参数的重载） |
| `form.send_to(player, on_result?)` | 发送给玩家；若传入 `on_result`，会覆盖各按钮各自的回调 | `SimpleForm::sendTo` |
| `form.send_update(player, on_result?)` | 以"更新"方式重新发送（替换玩家当前显示的同一表单） | `SimpleForm::sendUpdate` |

## CustomForm — 自定义表单

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `CustomForm::new(title)` | 新建 | `CustomForm::CustomForm` |
| `form.set_title(title)` / `set_submit_button(text)` | 设置标题 / 提交按钮文字 | `CustomForm::setTitle` / `setSubmitButton` |
| `form.append_header(text)` / `append_label(text)` / `append_divider()` | 同 SimpleForm | `CustomForm::appendHeader` 等 |
| `form.append_input(name, label, placeholder?, default?, tooltip?)` | 文本输入框 | `CustomForm::appendInput` |
| `form.append_toggle(name, label, default?, tooltip?)` | 开关 | `CustomForm::appendToggle` |
| `form.append_dropdown(name, label, options, default_index?, tooltip?)` | 下拉框 | `CustomForm::appendDropdown` |
| `form.append_slider(name, label, min, max, step?, default?, tooltip?)` | 滑块 | `CustomForm::appendSlider` |
| `form.append_step_slider(name, label, steps, default_index?, tooltip?)` | 步进滑块（在给定的字符串档位间切换） | `CustomForm::appendStepSlider` |
| `form.send_to(player, on_result?)` / `send_update(player, on_result?)` | 发送 / 以更新方式发送 | `CustomForm::sendTo` / `sendUpdate` |

`on_result` 收到的结果是一个按控件 `name` 取值的映射，每个值可能是数字、小数、字符串或"未提交"，对应原生的 `CustomFormResult`（`name → 数值/浮点/字符串` 的可选映射）。

## ModalForm — 二选一表单

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ModalForm::new(title, content, upper_button, lower_button)` | 新建 | `ModalForm::ModalForm` |
| `form.set_title(title)` / `set_content(text)` / `set_upper_button(text)` / `set_lower_button(text)` | 逐项设置 | 对应的 `setXxx` |
| `form.send_to(player, on_result?)` / `send_update(player, on_result?)` | 发送 / 以更新方式发送 | `ModalForm::sendTo` / `sendUpdate` |

结果是"选了上按钮"/"选了下按钮"/"取消"三选一（对应原生 `ModalFormSelectedButton::Upper`/`Lower` 或空），不是简单的布尔值。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `FormCancelReason` | 表单被取消的原因（如玩家直接关闭、正忙等），随结果一起传给回调 |
| `CustomFormResult` | `CustomForm` 提交结果：按控件名取值的映射，未提交时整体为空 |
| `ModalFormResult` | `ModalForm` 提交结果：`Upper` / `Lower` / 空（取消） |
