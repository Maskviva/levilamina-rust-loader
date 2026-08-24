import {defineConfig} from 'vitepress'

export default defineConfig({
    lang: 'zh-CN',
    title: 'levilamina-rs',
    description: '用 Rust 编写 LeviLamina 模组：入门指南、完整 API 参考与深入设计',
    themeConfig: {
        nav: [
            {text: '初级开发', link: '/guide/getting-started'},
            {text: 'API 参考', link: '/api/core/overview'},
            {text: '高级开发', link: '/advanced/architecture'},
            {text: 'GitHub', link: 'https://github.com/Maskviva/levilamina-rust-loader'},
        ],
        sidebar: {
            '/guide/': [
                {
                    text: '初级开发',
                    items: [
                        {text: '快速开始', link: '/guide/getting-started'},
                        {text: '核心概念', link: '/guide/concepts'},
                        {text: '事件', link: '/guide/events'},
                        {text: '命令', link: '/guide/commands'},
                        {text: '世界与玩家', link: '/guide/world'},
                        {text: '日志与调度', link: '/guide/logging-scheduling'},
                    ],
                },
                {
                    text: '继续深入',
                    items: [
                        {text: 'API 参考总览', link: '/api/core/overview'},
                        {text: '高级开发', link: '/advanced/architecture'},
                    ],
                },
            ],
            '/api/': [
                {
                    text: '总览',
                    items: [
                        {text: 'API 参考总览', link: '/api/core/overview'},
                    ],
                },
                {
                    text: '事件与网络',
                    items: [
                        {text: 'Event — 事件监听', link: '/api/infra/event'},
                        {text: 'Packet — 抓包 / 改包', link: '/api/infra/packet'},
                    ],
                },
                {
                    text: '游戏对象',
                    items: [
                        {text: 'Player — 玩家', link: '/api/actor/player'},
                        {text: 'Actor / Entity — 实体', link: '/api/actor/entity'},
                        {text: 'Block — 方块', link: '/api/world/block'},
                        {text: 'Item — 物品', link: '/api/world/item'},
                        {text: 'Container — 容器', link: '/api/world/container'},
                        {text: 'ScoreBoard — 计分板', link: '/api/infra/scoreboard'},
                    ],
                },
                {
                    text: '世界与服务端',
                    items: [
                        {text: 'Server — 服务端', link: '/api/infra/server'},
                        {text: 'World — 世界扫描', link: '/api/world/world'},
                        {text: 'Command — 命令', link: '/api/actor/command'},
                        {text: 'Dimensions — 自定义维度', link: '/api/world/dimensions'},
                        {text: 'SimPlayer — 模拟玩家', link: '/api/actor/sim'},
                    ],
                },
                {
                    text: '跨模组',
                    items: [
                        {text: 'Bus — 事件总线', link: '/api/core/bus'},
                        {text: 'Service — 服务注册', link: '/api/core/service'}, { text: 'Lane — Rust 高速公路', link: '/api/core/lane' },
                    ],
                },
                {
                    text: '数据',
                    items: [
                        {text: 'Nbt — NBT 读写', link: '/api/world/nbt'},
                        {text: 'Data — 持久化', link: '/api/infra/data'},
                        {text: 'Money — 经济', link: '/api/actor/money'},
                    ],
                },
                {
                    text: '界面与运行时',
                    items: [
                        {text: 'Gui — 表单', link: '/api/actor/gui'},
                        {text: 'Log — 日志', link: '/api/rt/log'},
                        {text: 'Scheduler — 调度', link: '/api/rt/scheduler'},
                        {text: 'System — 系统信息', link: '/api/core/system'},
                        {text: 'Client — 客户端 API', link: '/api/rt/client'},
                        {text: 'Objects — 其他类型', link: '/api/core/objects'},
                    ],
                },
            ],
            '/advanced/': [
                {
                    text: '高级开发',
                    items: [
                        {text: '架构与 ABI 设计', link: '/advanced/architecture'},
                        {text: 'ABI 契约与演进', link: '/advanced/abi'},
                        {text: '内存安全与生命周期', link: '/advanced/memory-safety'},
                        {text: '扩展桥接：新增 API', link: '/advanced/extending'},
                        {text: '设计取舍记录', link: '/advanced/decisions'},
                    ],
                },
                {
                    text: '回到',
                    items: [
                        {text: 'API 参考总览', link: '/api/core/overview'},
                        {text: '初级开发', link: '/guide/getting-started'},
                    ],
                },
            ],
        },
        outline: {level: [2, 3], label: '本页目录'},
        docFooter: {prev: '上一页', next: '下一页'},
        search: {provider: 'local'},
    },
})
