#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/systems/preview/model_preview_item.hpp>


#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>


#include <core/features/changer/econ_item_attribute_manager.hpp>
#include <core/features/changer/sticker_placement.hpp>
#include <core/features/changer/skinchanger.hpp>

namespace
{
    constexpr ULONGLONG RETRY_INTERVAL_MS = 500;

    // Settle rapid UI edits before consuming the next temporary preview-item slot.
    constexpr ULONGLONG REQUEST_SETTLE_MS = 35;

    using PreviewBlockCtorFn = void*(__fastcall*)(void*, std::int64_t, char);
    using ParseFromArrayFn = bool(__fastcall*)(void*, const void*, int);
    using PreviewBlockDtorFn = void(__fastcall*)(void*);
    using CreatePreviewItemFn = void*(__fastcall*)(const void*, std::uint64_t*);

    PreviewBlockCtorFn g_PreviewBlockCtor = nullptr;
    ParseFromArrayFn g_ParseFromArray = nullptr;
    PreviewBlockDtorFn g_PreviewBlockDtor = nullptr;
    CreatePreviewItemFn g_CreatePreviewItem = nullptr;

    std::atomic_bool g_Initialized{ false };
    std::atomic_bool g_ResultLocked{ false };
    std::atomic_uint64_t g_LatestRevision{ 0 };

    struct PendingRequest
    {
        std::vector<std::uint8_t> protobuf;
        std::uint16_t definitionIndex = 0;
        std::string modelPath;
        bool counterTerrorist = false;
        int paintKit = 0;
        std::uint64_t revision = 0;
        bool complexCosmetics = false;
        features::changer::attributes::StickerArray stickers{};
        systems::model_preview::Presentation presentation = systems::model_preview::Presentation::Weapon;
    };

    std::mutex g_StateMutex;
    PendingRequest g_Pending{};
    systems::preview_item::Result g_Result{};
    std::uint64_t g_ProcessedRevision = 0;
    std::uint64_t g_LastAttemptRevision = 0;
    ULONGLONG g_LastAttemptAt = 0;
    ULONGLONG g_PendingChangedAt = 0;

    class ProtoWriter
    {
    public:
        explicit ProtoWriter(std::size_t reserve = 128)
        {
            m_Data.reserve(reserve);
        }

        void UInt32(std::uint32_t field, std::uint32_t value)
        {
            Tag(field, 0);
            Varint(value);
        }

        void UInt64(std::uint32_t field, std::uint64_t value)
        {
            Tag(field, 0);
            Varint(value);
        }

        void Float(std::uint32_t field, float value)
        {
            Tag(field, 5);

            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));

            m_Data.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
            m_Data.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFu));
            m_Data.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFu));
            m_Data.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFu));
        }

        void String(std::uint32_t field, const std::string& value)
        {
            if (value.empty())
                return;

            Tag(field, 2);
            Varint(value.size());
            m_Data.insert(m_Data.end(), value.begin(), value.end());
        }

        void Message(std::uint32_t field, const ProtoWriter& message)
        {
            if (message.m_Data.empty())
                return;

            Tag(field, 2);
            Varint(message.m_Data.size());
            m_Data.insert(m_Data.end(), message.m_Data.begin(), message.m_Data.end());
        }

        [[nodiscard]] const std::vector<std::uint8_t>& Data() const
        {
            return m_Data;
        }

        [[nodiscard]] std::vector<std::uint8_t> Take()
        {
            return std::move(m_Data);
        }

    private:
        void Tag(std::uint32_t field, std::uint32_t wireType)
        {
            Varint((static_cast<std::uint64_t>(field) << 3) | wireType);
        }

        void Varint(std::uint64_t value)
        {
            while (value >= 0x80)
            {
                m_Data.push_back(static_cast<std::uint8_t>(value) | 0x80u);
                value >>= 7;
            }

            m_Data.push_back(static_cast<std::uint8_t>(value));
        }

    private:
        std::vector<std::uint8_t> m_Data;
    };

    std::uint32_t FloatBits(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    void AddSticker(ProtoWriter& outer, std::size_t slot, const systems::model_preview::Sticker& sticker)
    {
        if (!sticker.enabled || sticker.kit <= 0)
            return;

        ProtoWriter message(64);

        message.UInt32(1, static_cast<std::uint32_t>(slot));
        message.UInt32(2, static_cast<std::uint32_t>(sticker.kit));
        message.Float(3, std::clamp(sticker.wear, 0.0f, 1.0f));
        message.Float(4, std::clamp(sticker.scale, 0.1f, 5.0f));
        message.Float(5, std::clamp(sticker.rotation, -180.0f, 180.0f));

        if (sticker.offsetX != 0.0f || sticker.offsetY != 0.0f)
        {
            const features::changer::sticker_placement::Vec2 offset = features::changer::sticker_placement::ClampOffsets(sticker.offsetX, sticker.offsetY);
            message.Float(7, offset.x);
            message.Float(8, offset.y);
        }

        // CEconItemPreviewDataBlock.stickers = field 12.
        outer.Message(12, message);
    }

    void AddKeychain(ProtoWriter& outer, const systems::model_preview::Keychain& keychain)
    {
        if (!keychain.enabled || keychain.id <= 0)
            return;

        ProtoWriter message(64);

        // Current CEconItemPreviewDataBlock reuses its nested Sticker message for
        // keychains. The native client populator consumes the keychain list separately.
        message.UInt32(1, 0);
        message.UInt32(2, static_cast<std::uint32_t>(keychain.id));

        if (keychain.offsetX != 0.0f)
            message.Float(7, keychain.offsetX);

        if (keychain.offsetY != 0.0f)
            message.Float(8, keychain.offsetY);

        if (keychain.offsetZ != 0.0f)
            message.Float(9, keychain.offsetZ);

        // The nested message's "pattern" field is the integer seed consumed by the
        // keychain preview path.
        message.UInt32(10, static_cast<std::uint32_t>(std::clamp(keychain.seed, 0, 100000)));

        // Field 11 is uint32 "highlight_reel" in the current proto and is not the
        // same type as the skinchanger's float highlight control, so highlight is
        // deliberately not serialized here.

        // CEconItemPreviewDataBlock.keychains = field 20.
        outer.Message(20, message);
    }

    bool HasComplexCosmetics(const systems::model_preview::Request& request)
    {
        if (request.keychain.enabled && request.keychain.id > 0)
            return true;

        for (const auto& sticker : request.stickers)
        {
            if (sticker.enabled && sticker.kit > 0)
                return true;
        }

        return false;
    }

    features::changer::attributes::StickerArray BuildStickerAttributes(const systems::model_preview::Request& request)
    {
        features::changer::attributes::StickerArray stickers{};

        for (std::size_t i = 0; i < stickers.size(); ++i)
        {
            const systems::model_preview::Sticker& source = request.stickers[i];
            auto& destination = stickers[i];

            destination.enabled = source.enabled;
            destination.kit = source.kit;
            destination.wear = source.wear;
            destination.scale = source.scale;
            destination.rotation = source.rotation;
            destination.offsetX = source.offsetX;
            destination.offsetY = source.offsetY;
        }

        return stickers;
    }

    std::vector<std::uint8_t> BuildPreviewData(const systems::model_preview::Request& request)
    {
        ProtoWriter writer(256);

        // CEconItemPreviewDataBlock:
        // 3 defindex, 4 paintindex, 5 rarity, 7 paintwear(uint32 float bits),
        // 8 paintseed, 9/10 StatTrak, 11 customname, 12 stickers, 20 keychains.
        writer.UInt32(3, request.definitionIndex);

        if (request.paintKit > 0)
        {
            writer.UInt32(4, static_cast<std::uint32_t>(request.paintKit));

            if (request.rarity > 0)
                writer.UInt32(5, static_cast<std::uint32_t>(request.rarity));

            const float wear = std::clamp(request.wear, 0.0001f, 1.0f);
            writer.UInt32(7, FloatBits(wear));
            writer.UInt32(8, static_cast<std::uint32_t>(std::clamp(request.seed, 0, 1000)));
        }

        if (request.statTrakEnabled)
        {
            // Score type 0 is the standard weapon kill counter.
            writer.UInt32(9, 0);
            writer.UInt32(10, static_cast<std::uint32_t>(std::max(request.statTrakKills, 0)));
        }

        writer.String(11, request.customName);

        for (std::size_t slot = 0; slot < request.stickers.size(); ++slot)
            AddSticker(writer, slot, request.stickers[slot]);

        AddKeychain(writer, request.keychain);

        return writer.Take();
    }
}

bool systems::preview_item::Initialize()
{
    Shutdown();

    g_CreatePreviewItem = reinterpret_cast<CreatePreviewItemFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::create_preview_item)));
    g_PreviewBlockCtor = reinterpret_cast<PreviewBlockCtorFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_block_ctor)));
    g_ParseFromArray = reinterpret_cast<ParseFromArrayFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_parse_from_array)));
    g_PreviewBlockDtor = reinterpret_cast<PreviewBlockDtorFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_block_dtor)));

    if (!g_CreatePreviewItem ||
        !g_PreviewBlockCtor ||
        !g_ParseFromArray ||
        !g_PreviewBlockDtor)
    {
        Shutdown();
        return false;
    }

    g_Initialized.store(true, std::memory_order_release);
    return true;
}

void systems::preview_item::Shutdown()
{
    g_Initialized.store(false, std::memory_order_release);
    g_ResultLocked.store(false, std::memory_order_release);
    g_LatestRevision.store(0, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        g_Pending = {};
        g_Result = {};
        g_ProcessedRevision = 0;
        g_LastAttemptRevision = 0;
        g_LastAttemptAt = 0;
        g_PendingChangedAt = 0;
    }

    g_PreviewBlockCtor = nullptr;
    g_ParseFromArray = nullptr;
    g_PreviewBlockDtor = nullptr;
    g_CreatePreviewItem = nullptr;
}

void systems::preview_item::Request(const systems::model_preview::Request& request)
{
    if (!g_Initialized.load(std::memory_order_acquire) || request.definitionIndex == 0)
        return;

    std::vector<std::uint8_t> protobuf = BuildPreviewData(request);

    std::lock_guard<std::mutex> lock(g_StateMutex);

    if (g_Pending.definitionIndex == request.definitionIndex &&
        g_Pending.modelPath == request.modelPath &&
        g_Pending.counterTerrorist == request.counterTerrorist &&
        g_Pending.paintKit == request.paintKit &&
        g_Pending.presentation == request.presentation &&
        g_Pending.protobuf == protobuf)
    {
        return;
    }

    g_Pending.protobuf = std::move(protobuf);
    g_Pending.definitionIndex = request.definitionIndex;
    g_Pending.modelPath = request.modelPath;
    g_Pending.counterTerrorist = request.counterTerrorist;
    g_Pending.paintKit = request.paintKit;
    g_Pending.complexCosmetics = HasComplexCosmetics(request);
    g_Pending.stickers = BuildStickerAttributes(request);
    g_Pending.presentation = request.presentation;
    ++g_Pending.revision;
    g_PendingChangedAt = GetTickCount64();
    g_LatestRevision.store(g_Pending.revision, std::memory_order_release);
}

void systems::preview_item::ProcessPending()
{
    if (!g_Initialized.load(std::memory_order_acquire))
        return;

    // The real weapon gets priority over the standalone preview. Its material/HUD
    // refresh can temporarily disturb the Panorama root/custom-material pipeline.
    // Keep only the newest preview request queued until that refresh has finished.
    if (features::changer::application::IsUpdatePending())
        return;

    if (g_ResultLocked.load(std::memory_order_acquire))
        return;

    PendingRequest request{};

    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        if (g_Pending.revision == 0 ||
            g_Pending.revision == g_ProcessedRevision)
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();

        if (g_PendingChangedAt != 0 &&
            now - g_PendingChangedAt < REQUEST_SETTLE_MS)
        {
            return;
        }

        if (g_LastAttemptRevision == g_Pending.revision &&
            now - g_LastAttemptAt < RETRY_INTERVAL_MS)
        {
            return;
        }

        g_LastAttemptRevision = g_Pending.revision;
        g_LastAttemptAt = now;
        request = g_Pending;
    }

    if (request.protobuf.empty())
        return;

    alignas(16) std::array<std::uint8_t, 0x100> previewBlock{};

    g_PreviewBlockCtor(previewBlock.data(), 0, 0);

    const bool parsed =
        g_ParseFromArray(
            previewBlock.data(),
            request.protobuf.data(),
            static_cast<int>(request.protobuf.size()));

    std::uint64_t itemId = 0;
    void* itemView = nullptr;

    if (parsed)
        itemView = g_CreatePreviewItem(previewBlock.data(), &itemId);

    bool stickersSynced = true;

    if (parsed && itemView && request.presentation == systems::model_preview::Presentation::Weapon)
    {
        stickersSynced = features::changer::attributes::SyncStickers(
            reinterpret_cast<features::changer::engine::item_view*>(itemView),
            request.stickers);
    }

    g_PreviewBlockDtor(previewBlock.data());

    if (!parsed || !itemView || itemId == 0 || !stickersSynced)
        return;

    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        g_Result.itemId = itemId;
        g_Result.definitionIndex = request.definitionIndex;
        g_Result.modelPath = request.modelPath;
        g_Result.counterTerrorist = request.counterTerrorist;
        g_Result.paintKit = request.paintKit;
        g_Result.revision = request.revision;
        g_Result.complexCosmetics = request.complexCosmetics;
        g_Result.presentation = request.presentation;
        g_Result.valid = true;

        g_ProcessedRevision = request.revision;
        g_ResultLocked.store(true, std::memory_order_release);
    }

}

void systems::preview_item::ReleaseForNextRequest()
{
    g_ResultLocked.store(false, std::memory_order_release);
}

bool systems::preview_item::IsResultLocked()
{
    return g_ResultLocked.load(std::memory_order_acquire);
}

std::uint64_t systems::preview_item::GetLatestRevision()
{
    return g_LatestRevision.load(std::memory_order_acquire);
}

systems::preview_item::Result systems::preview_item::GetResult()
{
    std::lock_guard<std::mutex> lock(g_StateMutex);

    Result result = g_Result;
    const std::uint64_t latestRevision = g_LatestRevision.load(std::memory_order_acquire);

    // If a newer committed UI state arrived while this temp item was locked, do not
    // build/promote the obsolete intermediate item. Keep the previous good frame while
    // ProcessPending coalesces directly to the newest protobuf revision.
    result.current =
        result.valid &&
        result.revision != 0 &&
        result.revision == latestRevision;

    return result;
}
