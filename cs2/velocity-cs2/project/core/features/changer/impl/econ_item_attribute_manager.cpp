#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/econ_item_attribute_manager.hpp>
#include <core/features/changer/sticker_placement.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    struct EconItemAttribute_t
    {
        char pad_0000[0x30];
        std::uint16_t defIndex;
        char pad_0032[2];
        float value;
        float initialValue;
        std::int32_t refundableCurrency;
        bool setBonus;
        char pad_0041[7];
    };

    // CUtlVector<CEconItemAttribute> layout used by features::changer::engine::attribute_list::m_Attributes.
    struct AttributeVector_t
    {
        std::int32_t size;
        std::int32_t pad;
        std::uintptr_t data;
        std::int32_t allocationCount;
        std::int32_t growSizeAndFlags;
    };

    constexpr std::int32_t UTL_VECTOR_FIXED_FLAGS = static_cast<std::int32_t>(0x40000000u);

    constexpr std::uint16_t ATTR_PAINT = 6;
    constexpr std::uint16_t ATTR_SEED = 7;
    constexpr std::uint16_t ATTR_WEAR = 8;

    constexpr std::uint16_t ATTR_KILL_EATER = 80;
    constexpr std::uint16_t ATTR_KILL_EATER_SCORE_TYPE = 81;

    constexpr std::uint16_t ATTR_STICKER_SLOT_0_ID = 113;
    constexpr std::uint16_t ATTR_STICKER_SLOT_STRIDE = 4;

    constexpr std::uint16_t STICKER_ID_OFFSET = 0;
    constexpr std::uint16_t STICKER_WEAR_OFFSET = 1;
    constexpr std::uint16_t STICKER_SCALE_OFFSET = 2;
    constexpr std::uint16_t STICKER_ROTATION_OFFSET = 3;

    constexpr std::uint16_t ATTR_STICKER_SLOT_0_OFFSET_X = 278;
    constexpr std::uint16_t ATTR_STICKER_OFFSET_STRIDE = 2;

    constexpr std::uint16_t ATTR_STICKER_SLOT_0_SCHEMA = 290;

    // Keychain slot-0 attribute definitions from the econ schema.
    constexpr std::uint16_t ATTR_KEYCHAIN_SLOT_0_ID = 299;
    constexpr std::uint16_t ATTR_KEYCHAIN_SLOT_0_OFFSET_X = 300;
    constexpr std::uint16_t ATTR_KEYCHAIN_SLOT_0_OFFSET_Y = 301;
    constexpr std::uint16_t ATTR_KEYCHAIN_SLOT_0_OFFSET_Z = 302;
    constexpr std::uint16_t ATTR_KEYCHAIN_SLOT_0_SEED = 303;

    AttributeVector_t* GetAttributeVector(features::changer::engine::item_view* item)
    {
        if (!item)
            return nullptr;

        features::changer::engine::attribute_list* attributeList = item->m_AttributeList();

        if (!attributeList)
            return nullptr;

        return reinterpret_cast<AttributeVector_t*>(&attributeList->m_Attributes());
    }

    EconItemAttribute_t* GetAttributes(AttributeVector_t* vector)
    {
        if (!vector || vector->size <= 0 || !vector->data)
            return nullptr;

        return reinterpret_cast<EconItemAttribute_t*>(vector->data);
    }

    void SetFloatAttribute(EconItemAttribute_t& attribute, std::uint16_t definitionIndex, float value)
    {
        attribute.defIndex = definitionIndex;
        attribute.value = value;
        attribute.initialValue = value;
    }

    void SetIntegerAttribute(EconItemAttribute_t& attribute, std::uint16_t definitionIndex, std::int32_t value)
    {
        const float rawValue = std::bit_cast<float>(value);

        attribute.defIndex = definitionIndex;
        attribute.value = rawValue;
        attribute.initialValue = rawValue;
    }

    bool IsEnabledSticker(const features::changer::attributes::StickerData& sticker)
    {
        return sticker.enabled && sticker.kit > 0;
    }

    bool IsEnabledKeychain(const features::changer::attributes::KeychainData* keychain)
    {
        return keychain && keychain->enabled && keychain->id > 0;
    }

    bool HasCustomStickerOffset(const features::changer::attributes::StickerData& sticker)
    {
        return sticker.offsetX != 0.0f || sticker.offsetY != 0.0f;
    }

    std::size_t GetStickerAttributeCount(const features::changer::attributes::StickerArray* stickers)
    {
        if (!stickers)
            return 0;

        std::size_t count = 0;

        for (const auto& sticker : *stickers)
        {
            if (!IsEnabledSticker(sticker))
                continue;

            count += 4;

            if (HasCustomStickerOffset(sticker))
                count += 3;
        }

        return count;
    }

    void AddStickerBaseAttributes(EconItemAttribute_t* attributes, std::size_t& index, const features::changer::attributes::StickerArray& stickers)
    {
        for (std::size_t slot = 0; slot < stickers.size(); ++slot)
        {
            const auto& sticker = stickers[slot];

            if (!IsEnabledSticker(sticker))
                continue;

            const std::uint16_t baseDefinition = static_cast<std::uint16_t>(ATTR_STICKER_SLOT_0_ID + static_cast<std::uint16_t>(slot) * ATTR_STICKER_SLOT_STRIDE);

            SetIntegerAttribute(attributes[index++], baseDefinition + STICKER_ID_OFFSET, static_cast<std::int32_t>(sticker.kit));
            SetFloatAttribute(attributes[index++], baseDefinition + STICKER_WEAR_OFFSET, std::clamp(sticker.wear, 0.0f, 1.0f));
            SetFloatAttribute(attributes[index++], baseDefinition + STICKER_SCALE_OFFSET, std::clamp(sticker.scale, 0.1f, 5.0f));
            SetFloatAttribute(attributes[index++], baseDefinition + STICKER_ROTATION_OFFSET, std::clamp(sticker.rotation, -180.0f, 180.0f));
        }
    }

    void AddStickerOffsetAttributes(EconItemAttribute_t* attributes, std::size_t& index, const features::changer::attributes::StickerArray& stickers)
    {
        for (std::size_t slot = 0; slot < stickers.size(); ++slot)
        {
            const auto& sticker = stickers[slot];

            if (!IsEnabledSticker(sticker) || !HasCustomStickerOffset(sticker))
                continue;

            const std::uint16_t offsetXDefinition = static_cast<std::uint16_t>(ATTR_STICKER_SLOT_0_OFFSET_X + static_cast<std::uint16_t>(slot) * ATTR_STICKER_OFFSET_STRIDE);
            const features::changer::sticker_placement::Vec2 offset = features::changer::sticker_placement::ClampOffsets(sticker.offsetX, sticker.offsetY);

            SetFloatAttribute(attributes[index++], offsetXDefinition, offset.x);
            SetFloatAttribute(attributes[index++], static_cast<std::uint16_t>(offsetXDefinition + 1), offset.y);
        }
    }

    void AddStickerSchemaAttributes(EconItemAttribute_t* attributes, std::size_t& index, const features::changer::attributes::StickerArray& stickers)
    {
        for (std::size_t slot = 0; slot < stickers.size(); ++slot)
        {
            const auto& sticker = stickers[slot];

            if (!IsEnabledSticker(sticker) || !HasCustomStickerOffset(sticker))
                continue;

            const std::uint16_t schemaDefinition = static_cast<std::uint16_t>(ATTR_STICKER_SLOT_0_SCHEMA + static_cast<std::uint16_t>(slot));
            SetIntegerAttribute(attributes[index++], schemaDefinition, 0);
        }
    }

    void AddKeychainAttributes(EconItemAttribute_t* attributes, std::size_t& index, const features::changer::attributes::KeychainData& keychain)
    {
        SetIntegerAttribute(attributes[index++], ATTR_KEYCHAIN_SLOT_0_ID, static_cast<std::int32_t>(keychain.id));
        SetFloatAttribute(attributes[index++], ATTR_KEYCHAIN_SLOT_0_OFFSET_X, keychain.offsetX);
        SetFloatAttribute(attributes[index++], ATTR_KEYCHAIN_SLOT_0_OFFSET_Y, keychain.offsetY);
        SetFloatAttribute(attributes[index++], ATTR_KEYCHAIN_SLOT_0_OFFSET_Z, keychain.offsetZ);
        SetIntegerAttribute(attributes[index++], ATTR_KEYCHAIN_SLOT_0_SEED, static_cast<std::int32_t>(std::clamp(keychain.seed, 0, 100000)));
    }
}

void features::changer::attributes::Create(features::changer::engine::item_view* item, int paintKit, float wear, int seed, int statTrak, int statTrakScoreType, const StickerArray* stickers, const KeychainData* keychain)
{
    if (!item || !features::changer::engine::allocate_attributes)
        return;

    AttributeVector_t* attributeVector = GetAttributeVector(item);

    if (!attributeVector || attributeVector->size != 0 || attributeVector->data != 0)
        return;

    const bool hasPaintKit = paintKit > 0;
    const bool hasStatTrak = statTrak >= 0;
    const bool hasKeychain = IsEnabledKeychain(keychain);

    const std::size_t paintAttributeCount = hasPaintKit ? 3 : 0;
    const std::size_t statTrakAttributeCount = hasStatTrak ? 2 : 0;
    const std::size_t stickerAttributeCount = GetStickerAttributeCount(stickers);
    const std::size_t keychainAttributeCount = hasKeychain ? 5 : 0;

    const std::size_t attributeCount = paintAttributeCount + statTrakAttributeCount + stickerAttributeCount + keychainAttributeCount;

    if (attributeCount == 0)
        return;

    // Keep a small fixed-capacity margin; this list is mutated only by this manager.
    constexpr std::size_t EXTRA_ATTRIBUTE_CAPACITY = 8;
    const std::size_t attributeCapacity = attributeCount + EXTRA_ATTRIBUTE_CAPACITY;

    auto* attributes = static_cast<EconItemAttribute_t*>(
        features::changer::engine::allocate_attributes(attributeCapacity * sizeof(EconItemAttribute_t)));

    if (!attributes)
        return;

    std::memset(attributes, 0, attributeCapacity * sizeof(EconItemAttribute_t));

    std::size_t index = 0;

    if (hasPaintKit)
    {
        SetFloatAttribute(attributes[index++], ATTR_PAINT, static_cast<float>(paintKit));
        SetFloatAttribute(attributes[index++], ATTR_SEED, static_cast<float>(seed >= 0 ? seed : 0));
        SetFloatAttribute(attributes[index++], ATTR_WEAR, wear >= 0.0f ? wear : 0.01f);
    }

    if (hasStatTrak)
    {
        SetIntegerAttribute(attributes[index++], ATTR_KILL_EATER, static_cast<std::int32_t>(statTrak));
        SetIntegerAttribute(attributes[index++], ATTR_KILL_EATER_SCORE_TYPE, static_cast<std::int32_t>(statTrakScoreType));
    }

    if (stickers)
    {
        AddStickerBaseAttributes(attributes, index, *stickers);
        AddStickerOffsetAttributes(attributes, index, *stickers);
        AddStickerSchemaAttributes(attributes, index, *stickers);
    }

    if (hasKeychain)
        AddKeychainAttributes(attributes, index, *keychain);

    attributeVector->size = static_cast<std::int32_t>(attributeCount);
    attributeVector->pad = 0;
    attributeVector->data = reinterpret_cast<std::uintptr_t>(attributes);
    attributeVector->allocationCount = static_cast<std::int32_t>(attributeCapacity);
    attributeVector->growSizeAndFlags = UTL_VECTOR_FIXED_FLAGS;
}

bool features::changer::attributes::SyncStickers(features::changer::engine::item_view* item, const StickerArray& stickers)
{
    if (!item || !features::changer::engine::allocate_attributes || !features::changer::engine::free_attributes)
        return false;

    AttributeVector_t* attributeVector = GetAttributeVector(item);

    if (!attributeVector || attributeVector->size < 0)
        return false;

    EconItemAttribute_t* attributes = GetAttributes(attributeVector);

    if (attributeVector->size > 0 && !attributes)
        return false;

    constexpr std::size_t MAX_STICKER_ATTRIBUTES = STICKER_SLOT_COUNT * 7;
    std::array<EconItemAttribute_t, MAX_STICKER_ATTRIBUTES> desired{};
    std::size_t desiredCount = 0;

    AddStickerBaseAttributes(desired.data(), desiredCount, stickers);
    AddStickerOffsetAttributes(desired.data(), desiredCount, stickers);
    AddStickerSchemaAttributes(desired.data(), desiredCount, stickers);

    if (desiredCount == 0)
        return true;

    std::array<bool, MAX_STICKER_ATTRIBUTES> found{};
    std::size_t missingCount = 0;

    for (std::size_t wanted = 0; wanted < desiredCount; ++wanted)
    {
        for (std::int32_t existing = 0; existing < attributeVector->size; ++existing)
        {
            if (attributes[existing].defIndex != desired[wanted].defIndex)
                continue;

            attributes[existing].value = desired[wanted].value;
            attributes[existing].initialValue = desired[wanted].initialValue;
            found[wanted] = true;
            break;
        }

        if (!found[wanted])
            ++missingCount;
    }

    if (missingCount == 0)
        return true;

    const std::size_t requiredCount = static_cast<std::size_t>(attributeVector->size) + missingCount;

    if (attributes && attributeVector->allocationCount >= static_cast<std::int32_t>(requiredCount))
    {
        std::size_t index = static_cast<std::size_t>(attributeVector->size);

        for (std::size_t wanted = 0; wanted < desiredCount; ++wanted)
        {
            if (!found[wanted])
                attributes[index++] = desired[wanted];
        }

        attributeVector->size = static_cast<std::int32_t>(requiredCount);
        return true;
    }

    constexpr std::size_t EXTRA_ATTRIBUTE_CAPACITY = 8;
    const std::size_t newCapacity = requiredCount + EXTRA_ATTRIBUTE_CAPACITY;

    auto* newAttributes = static_cast<EconItemAttribute_t*>(
        features::changer::engine::allocate_attributes(newCapacity * sizeof(EconItemAttribute_t)));

    if (!newAttributes)
        return false;

    std::memset(newAttributes, 0, newCapacity * sizeof(EconItemAttribute_t));

    if (attributes && attributeVector->size > 0)
    {
        std::memcpy(
            newAttributes,
            attributes,
            static_cast<std::size_t>(attributeVector->size) * sizeof(EconItemAttribute_t));
    }

    std::size_t index = static_cast<std::size_t>(attributeVector->size);

    for (std::size_t wanted = 0; wanted < desiredCount; ++wanted)
    {
        if (!found[wanted])
            newAttributes[index++] = desired[wanted];
    }

    void* oldData = reinterpret_cast<void*>(attributeVector->data);

    attributeVector->size = static_cast<std::int32_t>(requiredCount);
    attributeVector->pad = 0;
    attributeVector->data = reinterpret_cast<std::uintptr_t>(newAttributes);
    attributeVector->allocationCount = static_cast<std::int32_t>(newCapacity);
    attributeVector->growSizeAndFlags = UTL_VECTOR_FIXED_FLAGS;

    if (oldData)
        features::changer::engine::free_attributes(oldData);

    return true;
}

void features::changer::attributes::Remove(features::changer::engine::item_view* item)
{
    if (!item)
        return;

    AttributeVector_t* attributeVector = GetAttributeVector(item);

    if (!attributeVector || (attributeVector->size == 0 && attributeVector->data == 0))
        return;

    void* data = reinterpret_cast<void*>(attributeVector->data);

    attributeVector->size = 0;
    attributeVector->pad = 0;
    attributeVector->data = 0;
    attributeVector->allocationCount = 0;
    attributeVector->growSizeAndFlags = 0;

    if (data && features::changer::engine::free_attributes)
        features::changer::engine::free_attributes(data);
}

bool features::changer::attributes::SetKeychainId(features::changer::engine::item_view* item, int id)
{
    AttributeVector_t* attributeVector = GetAttributeVector(item);
    EconItemAttribute_t* attributes = GetAttributes(attributeVector);

    if (!attributeVector || !attributes)
        return false;

    for (std::int32_t i = 0; i < attributeVector->size; ++i)
    {
        EconItemAttribute_t& attribute = attributes[i];

        if (attribute.defIndex != ATTR_KEYCHAIN_SLOT_0_ID)
            continue;

        SetIntegerAttribute(attribute, ATTR_KEYCHAIN_SLOT_0_ID, static_cast<std::int32_t>(id));
        return true;
    }

    return false;
}
