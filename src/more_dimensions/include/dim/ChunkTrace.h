#pragma once

namespace more_dimensions
{
    /**
     * 区块生命周期追踪。
     *
     * 只在环境变量 `MORE_DIMENSIONS_TRACE_CHUNK=1` 时才真正安装 hook —— 关掉时
     * 一个 detour 都不装，热路径上没有任何额外开销。
     *
     * 用法（PowerShell）：
     *
     *     $env:MORE_DIMENSIONS_TRACE_CHUNK=1
     *     $env:MORE_DIMENSIONS_TRACE_CHUNK_DIM=3      # 可选，只看某个维度
     *     .\bedrock_server.exe
     *
     * 默认只打自定义维度（id >= 3）。想连主世界一起看，设
     * `MORE_DIMENSIONS_TRACE_CHUNK_DIM=-1`。
     */
    void registerChunkTraceHooks();
    void unregisterChunkTraceHooks();
} // namespace more_dimensions
