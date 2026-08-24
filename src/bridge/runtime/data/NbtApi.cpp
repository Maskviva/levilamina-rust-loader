/**
 * bridge/NbtApi.cpp — binary NBT conversions (ABI v5 §I).
 *
 * The SNBT object model itself lives entirely in Rust (levilamina::nbt);
 * only binary formats need the engine's codec, so only these two calls
 * cross the boundary.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>
#include <string_view>

#include "mc/deps/nbt/CompoundTag.h"

namespace levi_rs::bridge
{
    bool api_nbt_snbt_to_binary(LeviRsStr snbt, int32_t fmt, void* ctx, LeviRsBytesSink sink)
    {
        if (!sink) return false;
        auto tag = CompoundTag::fromSnbt(std::string_view{snbt});
        if (!tag) return false;
        std::string bytes;
        if (fmt == 1)
        {
            bytes = tag->toNetworkNbt();
        }
        else
        {
            bytes = tag->toBinaryNbt(/*isLittleEndian*/ true);
        }
        sink(ctx, reinterpret_cast<uint8_t const*>(bytes.data()), bytes.size());
        return true;
    }

    bool api_nbt_binary_to_snbt(uint8_t const* data, size_t len, int32_t fmt, void* ctx, LeviRsStrSink sink)
    {
        if (!sink || !data) return false;
        std::string_view view{reinterpret_cast<char const*>(data), len};
        if (fmt == 1)
        {
            auto tag = CompoundTag::fromNetworkNbt(std::string{view});
            if (!tag) return false;
            sink(ctx, tag->toSnbt(SnbtFormat::Minimize));
            return true;
        }
        auto tag = CompoundTag::fromBinaryNbt(view, /*isLittleEndian*/ true);
        if (!tag) return false;
        sink(ctx, tag->toSnbt(SnbtFormat::Minimize));
        return true;
    }
} // namespace levi_rs::bridge
