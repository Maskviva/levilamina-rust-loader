---
layout: home

hero:
  name: levilamina-rs
  text: 用 Rust 编写 LeviLamina 模组
  tagline: 初级开发从零上手 · API 参考对标 LSE 分类 · 高级开发深入架构与 ABI
  actions:
    - theme: brand
      text: 快速开始 →
      link: /guide/getting-started
    - theme: alt
      text: API 参考
      link: /api/overview
    - theme: alt
      text: 高级开发
      link: /advanced/architecture

features:
  - title: 🚀 初级开发
    details: 安装加载器、写出第一个模组、事件 / 命令 / 世界 / 调度四大实战主题——只讲今天就能编译运行的代码，全部示例可直接复制。
    link: /guide/getting-started
  - title: 📖 API 参考
    details: 按 LSE 分类组织的完整 API 面：Event / Player / Entity / Block / World / Command / Nbt / Gui……每个条目标注 ✅已支持 / 🧩规划，并附原生方法对照。
    link: /api/overview
  - title: 🔬 高级开发
    details: 四层架构、ABI 契约与演进规则、内存安全与句柄设计、扩展桥接新增 API 的完整流程、设计取舍记录。
    link: /advanced/architecture
  - title: 事件驱动
    details: 通用订阅 + 唯一后缀匹配，可读取、改写、取消任何事件——包括其他模组发布的事件。
    link: /guide/events
  - title: 安全边界
    details: 句柄是标识符不是指针，悬垂指针在架构上不可能出现；每个 FFI 入口 catch_unwind，模组 panic 不会拖垮服务器。
    link: /advanced/memory-safety
  - title: 诚实的线程模型
    details: 所有回调在服务器线程；Log / Scheduler 线程安全，是 Tokio 等后台任务重返游戏的指定通道。
    link: /guide/logging-scheduling
---
