import {defineConfig} from 'vitepress'

export default defineConfig({
    lang: 'zh-CN',
    title: 'levilamina-rs',
    description: '用 Rust 编写 LeviLamina 模组：入门指南、API 参考（对标 LSE 分类）与深入设计',
    themeConfig: {
        nav: [
            {text: '初级开发', link: '/guide/getting-started'},
            {text: 'API 参考', link: '/api/overview'},
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
                        {text: 'API 参考总览', link: '/api/overview'},
                        {text: '高级开发', link: '/advanced/architecture'},
                    ],
                },
            ],
            '/api/': [
                {
                    text: '总览',
                    items: [
                        {text: 'API 参考总览', link: '/api/overview'},
                    ],
                },
                {
                    text: '事件 Event',
                    items: [
                        {text: 'Event — 事件监听', link: '/api/event'},
                    ],
                },
                {
                    text: '游戏 Game',
                    items: [
                        {text: 'Player — 玩家对象', link: '/api/player'},
                        {text: 'Entity — 实体对象', link: '/api/entity'},
                        {text: 'Block — 方块对象', link: '/api/block'},
                        {text: 'Item — 物品对象', link: '/api/item'},
                        {text: 'Container — 容器对象', link: '/api/container'},
                        {text: 'ScoreBoard — 计分板', link: '/api/scoreboard'},
                        {text: 'Objects — 其他对象', link: '/api/objects'},
                        {text: 'World — 世界/扫描', link: '/api/world'},
                        {text: 'Command — 命令', link: '/api/command'},
                        {text: 'Server — 服务端', link: '/api/server'},
                    ],
                },
                {
                    text: '数据 Data',
                    items: [
                        {text: 'Nbt — NBT 读写', link: '/api/nbt'},
                        {text: 'Data — 配置/数据库/经济', link: '/api/data'},
                        {text: 'Money — 经济 (LegacyMoney)', link: '/api/money'},
                    ],
                },
                {
                    text: '界面与系统',
                    items: [
                        {text: 'Gui — 表单', link: '/api/gui'},
                        {text: 'System — 文件/网络/进程', link: '/api/system'},
                    ],
                },
                {
                    text: '运行时',
                    items: [
                        {text: 'Log — 日志', link: '/api/log'},
                        {text: 'Scheduler — 调度', link: '/api/scheduler'},
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
                        {text: 'API 参考总览', link: '/api/overview'},
                        {text: '初级开发', link: '/guide/getting-started'},
                    ],
                },
            ],
        },
        outline: {level: [2, 3], label: '本页目录'},
        docFooter: {prev: '上一页', next: '下一页'},
    },
})