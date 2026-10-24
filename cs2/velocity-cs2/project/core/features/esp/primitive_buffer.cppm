module;

#include "platform/platform.h"

#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.esp.primitive_buffer;

import <cstddef>;
import <cstdint>;
import <optional>;
import <string_view>;
import <type_traits>;

export import :utilities.memory.memory;

export {
namespace features::esp::detail {

inline constexpr std::size_t kPrimitiveSize{0x70};
inline constexpr std::size_t kPrimitiveMaterialOffset{0x20};
inline constexpr std::size_t kPrimitiveMaterialCopyOffset{0x28};
inline constexpr std::size_t kPrimitiveColorOffset{0x50};
inline constexpr std::size_t kPrimitiveDrawOrderOffset{0x58};
inline constexpr std::size_t kPrimitiveFlagsOffset{0x62};
inline constexpr std::uint16_t kPrimitiveDrawLast{0x8};

struct MeshPrimitive {
  std::byte pad_00[0x18]{};
  std::uintptr_t scene_object{};
  std::uintptr_t material{};
  std::uintptr_t material_copy{};
  std::byte pad_30[0x20]{};
  std::uint32_t color{};
  float opacity{};
  std::int32_t draw_order{};
  std::byte pad_5c[0x6]{};
  std::uint16_t flags{};
  std::uint16_t flags_2{};
  std::byte pad_66[0xA]{};
};

static_assert(sizeof(MeshPrimitive) == kPrimitiveSize);
static_assert(offsetof(MeshPrimitive, material) == kPrimitiveMaterialOffset);
static_assert(offsetof(MeshPrimitive, material_copy) ==
              kPrimitiveMaterialCopyOffset);
static_assert(offsetof(MeshPrimitive, color) == kPrimitiveColorOffset);
static_assert(offsetof(MeshPrimitive, draw_order) == kPrimitiveDrawOrderOffset);
static_assert(offsetof(MeshPrimitive, flags) == kPrimitiveFlagsOffset);

// GeneratePrimitives writes to a fixed array and then a separate overflow
// array. Treating the two as one allocation corrupts renderer memory.
struct PrimitiveOutputBuffer {
  std::uintptr_t fixed_data{};
  std::int32_t fixed_capacity{};
  std::int32_t fixed_count{};
  std::int32_t overflow_count{};
  std::uint32_t reserved_14{};
  std::uintptr_t overflow_data{};
  std::int32_t overflow_capacity{};
  std::uint32_t overflow_allocation_flags{};

  [[nodiscard]] int Count() const noexcept {
    if (fixed_count < 0 || overflow_count < 0 || fixed_capacity < 0 ||
        overflow_capacity < 0 || fixed_count > fixed_capacity ||
        overflow_count > overflow_capacity || (fixed_count && !fixed_data) ||
        (overflow_count && !overflow_data)) {
      return -1;
    }

    constexpr auto kSanePrimitiveLimit{1 << 20};
    if (fixed_count > kSanePrimitiveLimit - overflow_count) {
      return -1;
    }

    return fixed_count + overflow_count;
  }

  [[nodiscard]] std::uintptr_t At(int index) const noexcept {
    const auto total = this->Count();
    if (index < 0 || total < 0 || index >= total) {
      return 0;
    }

    if (index < fixed_count) {
      return fixed_data + static_cast<std::size_t>(index) * kPrimitiveSize;
    }

    return overflow_data +
           static_cast<std::size_t>(index - fixed_count) * kPrimitiveSize;
  }
};

static_assert(sizeof(PrimitiveOutputBuffer) == 0x28);
static_assert(offsetof(PrimitiveOutputBuffer, fixed_count) == 0xC);
static_assert(offsetof(PrimitiveOutputBuffer, overflow_count) == 0x10);
static_assert(offsetof(PrimitiveOutputBuffer, overflow_data) == 0x18);

[[nodiscard]] inline std::optional<PrimitiveOutputBuffer> ReadPrimitiveBuffer(
    std::uintptr_t address) {
  if (!address) {
    return std::nullopt;
  }

  const auto result = memory::SafeRead<PrimitiveOutputBuffer>(address);
  if (!result || result->Count() < 0) {
    return std::nullopt;
  }

  return result;
}

inline void ReplacePrimitive(std::uintptr_t primitive, std::uintptr_t material,
                             const xdraw::color& color) {
  if (!primitive || !material) {
    return;
  }

  (void)memory::SafeWrite<std::uintptr_t>(primitive + kPrimitiveMaterialOffset,
                                          material);
  (void)memory::SafeWrite<std::uintptr_t>(
      primitive + kPrimitiveMaterialCopyOffset, material);
  (void)memory::SafeWrite<std::uint32_t>(primitive + kPrimitiveColorOffset,
                                         color);
}

inline void MarkPrimitiveLast(std::uintptr_t primitive) {
  if (!primitive) {
    return;
  }

  const auto flags =
      memory::SafeRead<std::uint16_t>(primitive + kPrimitiveFlagsOffset);
  if (flags) {
    (void)memory::SafeWrite<std::uint16_t>(
        primitive + kPrimitiveFlagsOffset,
        static_cast<std::uint16_t>(*flags | kPrimitiveDrawLast));
  }

  const auto order =
      memory::SafeRead<std::int32_t>(primitive + kPrimitiveDrawOrderOffset);
  if (order && *order < INT32_MAX) {
    (void)memory::SafeWrite<std::int32_t>(primitive + kPrimitiveDrawOrderOffset,
                                          *order + 1);
  }
}

}  // namespace features::esp::detail
}  // export
