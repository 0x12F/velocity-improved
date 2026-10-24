module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <algorithm>;
import <bit>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::changer {
namespace {
struct EconItemAttributeT {
  char pad_0000[0x30];
  std::uint16_t def_index;
  char pad_0032[2];
  float value;
  float initial_value;
  std::int32_t refundable_currency;
  bool set_bonus;
  char pad_0041[7];
};

// CUtlVector<CEconItemAttribute> layout used by
// engine::attribute_list::m_Attributes.
struct AttributeVectorT {
  std::int32_t size;
  std::int32_t pad;
  std::uintptr_t data;
  std::int32_t allocation_count;
  std::int32_t grow_size_and_flags;
};

constexpr std::int32_t kUtlVectorFixedFlags =
    static_cast<std::int32_t>(0x40000000u);

constexpr std::uint16_t kAttrPaint = 6;
constexpr std::uint16_t kAttrSeed = 7;
constexpr std::uint16_t kAttrWear = 8;

constexpr std::uint16_t kAttrKillEater = 80;
constexpr std::uint16_t kAttrKillEaterScoreType = 81;

constexpr std::uint16_t kAttrStickerSlot0Id = 113;
constexpr std::uint16_t kAttrStickerSlotStride = 4;

constexpr std::uint16_t kStickerIdOffset = 0;
constexpr std::uint16_t kStickerWearOffset = 1;
constexpr std::uint16_t kStickerScaleOffset = 2;
constexpr std::uint16_t kStickerRotationOffset = 3;

constexpr std::uint16_t kAttrStickerSlot0OffsetX = 278;
constexpr std::uint16_t kAttrStickerOffsetStride = 2;

constexpr std::uint16_t kAttrStickerSlot0Schema = 290;

// Keychain slot-0 attribute definitions from the econ schema.
constexpr std::uint16_t kAttrKeychainSlot0Id = 299;
constexpr std::uint16_t kAttrKeychainSlot0OffsetX = 300;
constexpr std::uint16_t kAttrKeychainSlot0OffsetY = 301;
constexpr std::uint16_t kAttrKeychainSlot0OffsetZ = 302;
constexpr std::uint16_t kAttrKeychainSlot0Seed = 303;

AttributeVectorT *GetAttributeVector(engine::ItemView *item) {
  if (!item) return nullptr;

  engine::AttributeList *attribute_list = item->MAttributeList();

  if (!attribute_list) return nullptr;

  return reinterpret_cast<AttributeVectorT *>(&attribute_list->MAttributes());
}

EconItemAttributeT *GetAttributes(AttributeVectorT *vector) {
  if (!vector || vector->size <= 0 || !vector->data) return nullptr;

  return reinterpret_cast<EconItemAttributeT *>(vector->data);
}

void SetFloatAttribute(EconItemAttributeT &attribute,
                       std::uint16_t definition_index, float value) {
  attribute.def_index = definition_index;
  attribute.value = value;
  attribute.initial_value = value;
}

void SetIntegerAttribute(EconItemAttributeT &attribute,
                         std::uint16_t definition_index, std::int32_t value) {
  const float raw_value = std::bit_cast<float>(value);

  attribute.def_index = definition_index;
  attribute.value = raw_value;
  attribute.initial_value = raw_value;
}

bool IsEnabledSticker(const attributes::StickerData &sticker) {
  return sticker.enabled && sticker.kit > 0;
}

bool IsEnabledKeychain(const attributes::KeychainData *keychain) {
  return keychain && keychain->enabled && keychain->id > 0;
}

bool HasCustomStickerOffset(const attributes::StickerData &sticker) {
  return sticker.offset_x != 0.0f || sticker.offset_y != 0.0f;
}

std::size_t GetStickerAttributeCount(const attributes::StickerArray *stickers) {
  if (!stickers) return 0;

  std::size_t count = 0;

  for (const auto &sticker : *stickers) {
    if (!IsEnabledSticker(sticker)) continue;

    count += 4;

    if (HasCustomStickerOffset(sticker)) count += 3;
  }

  return count;
}

void AddStickerBaseAttributes(EconItemAttributeT *attributes,
                              std::size_t &index,
                              const attributes::StickerArray &stickers) {
  for (std::size_t slot = 0; slot < stickers.size(); ++slot) {
    const auto &sticker = stickers[slot];

    if (!IsEnabledSticker(sticker)) continue;

    const std::uint16_t base_definition = static_cast<std::uint16_t>(
        kAttrStickerSlot0Id +
        static_cast<std::uint16_t>(slot) * kAttrStickerSlotStride);

    SetIntegerAttribute(attributes[index++], base_definition + kStickerIdOffset,
                        static_cast<std::int32_t>(sticker.kit));
    SetFloatAttribute(attributes[index++], base_definition + kStickerWearOffset,
                      std::clamp(sticker.wear, 0.0f, 1.0f));
    SetFloatAttribute(attributes[index++],
                      base_definition + kStickerScaleOffset,
                      std::clamp(sticker.scale, 0.1f, 5.0f));
    SetFloatAttribute(attributes[index++],
                      base_definition + kStickerRotationOffset,
                      std::clamp(sticker.rotation, -180.0f, 180.0f));
  }
}

void AddStickerOffsetAttributes(EconItemAttributeT *attributes,
                                std::size_t &index,
                                const attributes::StickerArray &stickers) {
  for (std::size_t slot = 0; slot < stickers.size(); ++slot) {
    const auto &sticker = stickers[slot];

    if (!IsEnabledSticker(sticker) || !HasCustomStickerOffset(sticker))
      continue;

    const std::uint16_t offset_x_definition = static_cast<std::uint16_t>(
        kAttrStickerSlot0OffsetX +
        static_cast<std::uint16_t>(slot) * kAttrStickerOffsetStride);
    const sticker_placement::Vec2 offset =
        sticker_placement::ClampOffsets(sticker.offset_x, sticker.offset_y);

    SetFloatAttribute(attributes[index++], offset_x_definition, offset.x);
    SetFloatAttribute(attributes[index++],
                      static_cast<std::uint16_t>(offset_x_definition + 1),
                      offset.y);
  }
}

void AddStickerSchemaAttributes(EconItemAttributeT *attributes,
                                std::size_t &index,
                                const attributes::StickerArray &stickers) {
  for (std::size_t slot = 0; slot < stickers.size(); ++slot) {
    const auto &sticker = stickers[slot];

    if (!IsEnabledSticker(sticker) || !HasCustomStickerOffset(sticker))
      continue;

    const std::uint16_t schema_definition = static_cast<std::uint16_t>(
        kAttrStickerSlot0Schema + static_cast<std::uint16_t>(slot));
    SetIntegerAttribute(attributes[index++], schema_definition, 0);
  }
}

void AddKeychainAttributes(EconItemAttributeT *attributes, std::size_t &index,
                           const attributes::KeychainData &keychain) {
  SetIntegerAttribute(attributes[index++], kAttrKeychainSlot0Id,
                      static_cast<std::int32_t>(keychain.id));
  SetFloatAttribute(attributes[index++], kAttrKeychainSlot0OffsetX,
                    keychain.offset_x);
  SetFloatAttribute(attributes[index++], kAttrKeychainSlot0OffsetY,
                    keychain.offset_y);
  SetFloatAttribute(attributes[index++], kAttrKeychainSlot0OffsetZ,
                    keychain.offset_z);
  SetIntegerAttribute(
      attributes[index++], kAttrKeychainSlot0Seed,
      static_cast<std::int32_t>(std::clamp(keychain.seed, 0, 100000)));
}
}  // namespace

void attributes::Create(engine::ItemView *item, int paint_kit, float wear,
                        int seed, int stat_trak, int stat_trak_score_type,
                        const StickerArray *stickers,
                        const KeychainData *keychain) {
  if (!item || !engine::allocate_attributes) return;

  AttributeVectorT *attribute_vector = GetAttributeVector(item);

  if (!attribute_vector || attribute_vector->size != 0 ||
      attribute_vector->data != 0)
    return;

  const bool has_paint_kit = paint_kit > 0;
  const bool has_stat_trak = stat_trak >= 0;
  const bool has_keychain = IsEnabledKeychain(keychain);

  const std::size_t paint_attribute_count = has_paint_kit ? 3 : 0;
  const std::size_t stat_trak_attribute_count = has_stat_trak ? 2 : 0;
  const std::size_t sticker_attribute_count =
      GetStickerAttributeCount(stickers);
  const std::size_t keychain_attribute_count = has_keychain ? 5 : 0;

  const std::size_t attribute_count =
      paint_attribute_count + stat_trak_attribute_count +
      sticker_attribute_count + keychain_attribute_count;

  if (attribute_count == 0) return;

  // Keep a small fixed-capacity margin; this list is mutated only by this
  // manager.
  constexpr std::size_t kExtraAttributeCapacity = 8;
  const std::size_t attribute_capacity =
      attribute_count + kExtraAttributeCapacity;

  auto *attributes =
      static_cast<EconItemAttributeT *>(engine::allocate_attributes(
          attribute_capacity * sizeof(EconItemAttributeT)));

  if (!attributes) return;

  std::memset(attributes, 0, attribute_capacity * sizeof(EconItemAttributeT));

  std::size_t index = 0;

  if (has_paint_kit) {
    SetFloatAttribute(attributes[index++], kAttrPaint,
                      static_cast<float>(paint_kit));
    SetFloatAttribute(attributes[index++], kAttrSeed,
                      static_cast<float>(seed >= 0 ? seed : 0));
    SetFloatAttribute(attributes[index++], kAttrWear,
                      wear >= 0.0f ? wear : 0.01f);
  }

  if (has_stat_trak) {
    SetIntegerAttribute(attributes[index++], kAttrKillEater,
                        static_cast<std::int32_t>(stat_trak));
    SetIntegerAttribute(attributes[index++], kAttrKillEaterScoreType,
                        static_cast<std::int32_t>(stat_trak_score_type));
  }

  if (stickers) {
    AddStickerBaseAttributes(attributes, index, *stickers);
    AddStickerOffsetAttributes(attributes, index, *stickers);
    AddStickerSchemaAttributes(attributes, index, *stickers);
  }

  if (has_keychain) AddKeychainAttributes(attributes, index, *keychain);

  attribute_vector->size = static_cast<std::int32_t>(attribute_count);
  attribute_vector->pad = 0;
  attribute_vector->data = reinterpret_cast<std::uintptr_t>(attributes);
  attribute_vector->allocation_count =
      static_cast<std::int32_t>(attribute_capacity);
  attribute_vector->grow_size_and_flags = kUtlVectorFixedFlags;
}

bool attributes::SyncStickers(engine::ItemView *item,
                              const StickerArray &stickers) {
  if (!item || !engine::allocate_attributes || !engine::free_attributes)
    return false;

  AttributeVectorT *attribute_vector = GetAttributeVector(item);

  if (!attribute_vector || attribute_vector->size < 0) return false;

  EconItemAttributeT *attributes = GetAttributes(attribute_vector);

  if (attribute_vector->size > 0 && !attributes) return false;

  constexpr std::size_t kMaxStickerAttributes = kStickerSlotCount * 7;
  std::array<EconItemAttributeT, kMaxStickerAttributes> desired{};
  std::size_t desired_count = 0;

  AddStickerBaseAttributes(desired.data(), desired_count, stickers);
  AddStickerOffsetAttributes(desired.data(), desired_count, stickers);
  AddStickerSchemaAttributes(desired.data(), desired_count, stickers);

  if (desired_count == 0) return true;

  std::array<bool, kMaxStickerAttributes> found{};
  std::size_t missing_count = 0;

  for (std::size_t wanted = 0; wanted < desired_count; ++wanted) {
    for (std::int32_t existing = 0; existing < attribute_vector->size;
         ++existing) {
      if (attributes[existing].def_index != desired[wanted].def_index) continue;

      attributes[existing].value = desired[wanted].value;
      attributes[existing].initial_value = desired[wanted].initial_value;
      found[wanted] = true;
      break;
    }

    if (!found[wanted]) ++missing_count;
  }

  if (missing_count == 0) return true;

  const std::size_t required_count =
      static_cast<std::size_t>(attribute_vector->size) + missing_count;

  if (attributes && attribute_vector->allocation_count >=
                        static_cast<std::int32_t>(required_count)) {
    std::size_t index = static_cast<std::size_t>(attribute_vector->size);

    for (std::size_t wanted = 0; wanted < desired_count; ++wanted) {
      if (!found[wanted]) attributes[index++] = desired[wanted];
    }

    attribute_vector->size = static_cast<std::int32_t>(required_count);
    return true;
  }

  constexpr std::size_t kExtraAttributeCapacity = 8;
  const std::size_t new_capacity = required_count + kExtraAttributeCapacity;

  auto *new_attributes = static_cast<EconItemAttributeT *>(
      engine::allocate_attributes(new_capacity * sizeof(EconItemAttributeT)));

  if (!new_attributes) return false;

  std::memset(new_attributes, 0, new_capacity * sizeof(EconItemAttributeT));

  if (attributes && attribute_vector->size > 0) {
    std::memcpy(new_attributes, attributes,
                static_cast<std::size_t>(attribute_vector->size) *
                    sizeof(EconItemAttributeT));
  }

  std::size_t index = static_cast<std::size_t>(attribute_vector->size);

  for (std::size_t wanted = 0; wanted < desired_count; ++wanted) {
    if (!found[wanted]) new_attributes[index++] = desired[wanted];
  }

  void *old_data = reinterpret_cast<void *>(attribute_vector->data);

  attribute_vector->size = static_cast<std::int32_t>(required_count);
  attribute_vector->pad = 0;
  attribute_vector->data = reinterpret_cast<std::uintptr_t>(new_attributes);
  attribute_vector->allocation_count = static_cast<std::int32_t>(new_capacity);
  attribute_vector->grow_size_and_flags = kUtlVectorFixedFlags;

  if (old_data) engine::free_attributes(old_data);

  return true;
}

void attributes::Remove(engine::ItemView *item) {
  if (!item) return;

  AttributeVectorT *attribute_vector = GetAttributeVector(item);

  if (!attribute_vector ||
      (attribute_vector->size == 0 && attribute_vector->data == 0))
    return;

  void *data = reinterpret_cast<void *>(attribute_vector->data);

  attribute_vector->size = 0;
  attribute_vector->pad = 0;
  attribute_vector->data = 0;
  attribute_vector->allocation_count = 0;
  attribute_vector->grow_size_and_flags = 0;

  if (data && engine::free_attributes) engine::free_attributes(data);
}

bool attributes::SetKeychainId(engine::ItemView *item, int id) {
  AttributeVectorT *attribute_vector = GetAttributeVector(item);
  EconItemAttributeT *attributes = GetAttributes(attribute_vector);

  if (!attribute_vector || !attributes) return false;

  for (std::int32_t i = 0; i < attribute_vector->size; ++i) {
    EconItemAttributeT &attribute = attributes[i];

    if (attribute.def_index != kAttrKeychainSlot0Id) continue;

    SetIntegerAttribute(attribute, kAttrKeychainSlot0Id,
                        static_cast<std::int32_t>(id));
    return true;
  }

  return false;
}

}  // namespace features::changer
