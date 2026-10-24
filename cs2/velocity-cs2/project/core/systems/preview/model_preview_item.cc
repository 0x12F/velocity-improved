module;

#include "platform/platform.h"

#include <Windows.h>

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <algorithm>;
import <array>;
import <atomic>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <filesystem>;
import <limits>;
import <mutex>;
import <string>;
import <string_view>;
import <type_traits>;
import <utility>;
import <vector>;

namespace {
constexpr ULONGLONG kRetryIntervalMs = 500;

// Settle rapid UI edits before consuming the next temporary preview-item slot.
constexpr ULONGLONG kRequestSettleMs = 35;

using PreviewBlockCtorFn = void*(__fastcall*)(void*, std::int64_t, char);
using ParseFromArrayFn = bool(__fastcall*)(void*, const void*, int);
using PreviewBlockDtorFn = void(__fastcall*)(void*);
using CreatePreviewItemFn = void*(__fastcall*)(const void*, std::uint64_t*);

PreviewBlockCtorFn g_preview_block_ctor = nullptr;
ParseFromArrayFn g_parse_from_array = nullptr;
PreviewBlockDtorFn g_preview_block_dtor = nullptr;
CreatePreviewItemFn g_create_preview_item = nullptr;

std::atomic_bool g_initialized{false};
std::atomic_bool g_result_locked{false};
std::atomic_uint64_t g_latest_revision{0};

struct PendingRequest {
  std::vector<std::uint8_t> protobuf;
  std::uint16_t definition_index = 0;
  std::string model_path;
  bool counter_terrorist = false;
  int paint_kit = 0;
  std::uint64_t revision = 0;
  bool complex_cosmetics = false;
  features::changer::attributes::StickerArray stickers{};
  systems::model_preview::Presentation presentation =
      systems::model_preview::Presentation::kWeapon;
};

std::mutex g_state_mutex;
PendingRequest g_pending{};
systems::preview_item::Result g_result{};
std::uint64_t g_processed_revision = 0;
std::uint64_t g_last_attempt_revision = 0;
ULONGLONG g_last_attempt_at = 0;
ULONGLONG g_pending_changed_at = 0;

class ProtoWriter {
 public:
  explicit ProtoWriter(std::size_t reserve = 128) { m_data_.reserve(reserve); }

  void UInt32(std::uint32_t field, std::uint32_t value) {
    Tag(field, 0);
    Varint(value);
  }

  void UInt64(std::uint32_t field, std::uint64_t value) {
    Tag(field, 0);
    Varint(value);
  }

  void Float(std::uint32_t field, float value) {
    Tag(field, 5);

    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    m_data_.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
    m_data_.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFu));
    m_data_.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFu));
    m_data_.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFu));
  }

  void String(std::uint32_t field, const std::string& value) {
    if (value.empty()) return;

    Tag(field, 2);
    Varint(value.size());
    m_data_.insert(m_data_.end(), value.begin(), value.end());
  }

  void Message(std::uint32_t field, const ProtoWriter& message) {
    if (message.m_data_.empty()) return;

    Tag(field, 2);
    Varint(message.m_data_.size());
    m_data_.insert(m_data_.end(), message.m_data_.begin(),
                   message.m_data_.end());
  }

  [[nodiscard]] const std::vector<std::uint8_t>& Data() const {
    return m_data_;
  }

  [[nodiscard]] std::vector<std::uint8_t> Take() { return std::move(m_data_); }

 private:
  void Tag(std::uint32_t field, std::uint32_t wire_type) {
    Varint((static_cast<std::uint64_t>(field) << 3) | wire_type);
  }

  void Varint(std::uint64_t value) {
    while (value >= 0x80) {
      m_data_.push_back(static_cast<std::uint8_t>(value) | 0x80u);
      value >>= 7;
    }

    m_data_.push_back(static_cast<std::uint8_t>(value));
  }

 private:
  std::vector<std::uint8_t> m_data_;
};

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void AddSticker(ProtoWriter& outer, std::size_t slot,
                const systems::model_preview::Sticker& sticker) {
  if (!sticker.enabled || sticker.kit <= 0) return;

  ProtoWriter message(64);

  message.UInt32(1, static_cast<std::uint32_t>(slot));
  message.UInt32(2, static_cast<std::uint32_t>(sticker.kit));
  message.Float(3, std::clamp(sticker.wear, 0.0f, 1.0f));
  message.Float(4, std::clamp(sticker.scale, 0.1f, 5.0f));
  message.Float(5, std::clamp(sticker.rotation, -180.0f, 180.0f));

  if (sticker.offset_x != 0.0f || sticker.offset_y != 0.0f) {
    const features::changer::sticker_placement::Vec2 offset =
        features::changer::sticker_placement::ClampOffsets(sticker.offset_x,
                                                           sticker.offset_y);
    message.Float(7, offset.x);
    message.Float(8, offset.y);
  }

  // CEconItemPreviewDataBlock.stickers = field 12.
  outer.Message(12, message);
}

void AddKeychain(ProtoWriter& outer,
                 const systems::model_preview::Keychain& keychain) {
  if (!keychain.enabled || keychain.id <= 0) return;

  ProtoWriter message(64);

  // Current CEconItemPreviewDataBlock reuses its nested Sticker message for
  // keychains. The native client populator consumes the keychain list
  // separately.
  message.UInt32(1, 0);
  message.UInt32(2, static_cast<std::uint32_t>(keychain.id));

  if (keychain.offset_x != 0.0f) message.Float(7, keychain.offset_x);

  if (keychain.offset_y != 0.0f) message.Float(8, keychain.offset_y);

  if (keychain.offset_z != 0.0f) message.Float(9, keychain.offset_z);

  // The nested message's "pattern" field is the integer seed consumed by the
  // keychain preview path.
  message.UInt32(
      10, static_cast<std::uint32_t>(std::clamp(keychain.seed, 0, 100000)));

  // Field 11 is uint32 "highlight_reel" in the current proto and is not the
  // same type as the skinchanger's float highlight control, so highlight is
  // deliberately not serialized here.

  // CEconItemPreviewDataBlock.keychains = field 20.
  outer.Message(20, message);
}

bool HasComplexCosmetics(const systems::model_preview::Request& request) {
  if (request.keychain.enabled && request.keychain.id > 0) return true;

  for (const auto& sticker : request.stickers) {
    if (sticker.enabled && sticker.kit > 0) return true;
  }

  return false;
}

features::changer::attributes::StickerArray BuildStickerAttributes(
    const systems::model_preview::Request& request) {
  features::changer::attributes::StickerArray stickers{};

  for (std::size_t i = 0; i < stickers.size(); ++i) {
    const systems::model_preview::Sticker& source = request.stickers[i];
    auto& destination = stickers[i];

    destination.enabled = source.enabled;
    destination.kit = source.kit;
    destination.wear = source.wear;
    destination.scale = source.scale;
    destination.rotation = source.rotation;
    destination.offset_x = source.offset_x;
    destination.offset_y = source.offset_y;
  }

  return stickers;
}

std::vector<std::uint8_t> BuildPreviewData(
    const systems::model_preview::Request& request) {
  ProtoWriter writer(256);

  // CEconItemPreviewDataBlock:
  // 3 defindex, 4 paintindex, 5 rarity, 7 paintwear(uint32 float bits),
  // 8 paintseed, 9/10 StatTrak, 11 customname, 12 stickers, 20 keychains.
  writer.UInt32(3, request.definition_index);

  if (request.paint_kit > 0) {
    writer.UInt32(4, static_cast<std::uint32_t>(request.paint_kit));

    if (request.rarity > 0)
      writer.UInt32(5, static_cast<std::uint32_t>(request.rarity));

    const float wear = std::clamp(request.wear, 0.0001f, 1.0f);
    writer.UInt32(7, FloatBits(wear));
    writer.UInt32(
        8, static_cast<std::uint32_t>(std::clamp(request.seed, 0, 1000)));
  }

  if (request.stat_trak_enabled) {
    // Score type 0 is the standard weapon kill counter.
    writer.UInt32(9, 0);
    writer.UInt32(
        10, static_cast<std::uint32_t>(std::max(request.stat_trak_kills, 0)));
  }

  writer.String(11, request.custom_name);

  for (std::size_t slot = 0; slot < request.stickers.size(); ++slot)
    AddSticker(writer, slot, request.stickers[slot]);

  AddKeychain(writer, request.keychain);

  return writer.Take();
}
}  // namespace

bool systems::preview_item::Initialize() {
  Shutdown();

  g_create_preview_item = reinterpret_cast<CreatePreviewItemFn>(
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::create_preview_item)));
  g_preview_block_ctor = reinterpret_cast<PreviewBlockCtorFn>(
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_block_ctor)));
  g_parse_from_array =
      reinterpret_cast<ParseFromArrayFn>(reinterpret_cast<std::uint8_t*>(
          PATTERN(patterns::preview_parse_from_array)));
  g_preview_block_dtor = reinterpret_cast<PreviewBlockDtorFn>(
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_block_dtor)));

  if (!g_create_preview_item || !g_preview_block_ctor || !g_parse_from_array ||
      !g_preview_block_dtor) {
    Shutdown();
    return false;
  }

  g_initialized.store(true, std::memory_order_release);
  return true;
}

void systems::preview_item::Shutdown() {
  g_initialized.store(false, std::memory_order_release);
  g_result_locked.store(false, std::memory_order_release);
  g_latest_revision.store(0, std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);

    g_pending = {};
    g_result = {};
    g_processed_revision = 0;
    g_last_attempt_revision = 0;
    g_last_attempt_at = 0;
    g_pending_changed_at = 0;
  }

  g_preview_block_ctor = nullptr;
  g_parse_from_array = nullptr;
  g_preview_block_dtor = nullptr;
  g_create_preview_item = nullptr;
}

void systems::preview_item::Request(
    const systems::model_preview::Request& request) {
  if (!g_initialized.load(std::memory_order_acquire) ||
      request.definition_index == 0)
    return;

  std::vector<std::uint8_t> protobuf = BuildPreviewData(request);

  std::lock_guard<std::mutex> lock(g_state_mutex);

  if (g_pending.definition_index == request.definition_index &&
      g_pending.model_path == request.model_path &&
      g_pending.counter_terrorist == request.counter_terrorist &&
      g_pending.paint_kit == request.paint_kit &&
      g_pending.presentation == request.presentation &&
      g_pending.protobuf == protobuf) {
    return;
  }

  g_pending.protobuf = std::move(protobuf);
  g_pending.definition_index = request.definition_index;
  g_pending.model_path = request.model_path;
  g_pending.counter_terrorist = request.counter_terrorist;
  g_pending.paint_kit = request.paint_kit;
  g_pending.complex_cosmetics = HasComplexCosmetics(request);
  g_pending.stickers = BuildStickerAttributes(request);
  g_pending.presentation = request.presentation;
  ++g_pending.revision;
  g_pending_changed_at = GetTickCount64();
  g_latest_revision.store(g_pending.revision, std::memory_order_release);
}

void systems::preview_item::ProcessPending() {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  // The real weapon gets priority over the standalone preview. Its material/HUD
  // refresh can temporarily disturb the Panorama root/custom-material pipeline.
  // Keep only the newest preview request queued until that refresh has
  // finished.
  if (features::changer::application::IsUpdatePending()) return;

  if (g_result_locked.load(std::memory_order_acquire)) return;

  PendingRequest request{};

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);

    if (g_pending.revision == 0 || g_pending.revision == g_processed_revision) {
      return;
    }

    const ULONGLONG now = GetTickCount64();

    if (g_pending_changed_at != 0 &&
        now - g_pending_changed_at < kRequestSettleMs) {
      return;
    }

    if (g_last_attempt_revision == g_pending.revision &&
        now - g_last_attempt_at < kRetryIntervalMs) {
      return;
    }

    g_last_attempt_revision = g_pending.revision;
    g_last_attempt_at = now;
    request = g_pending;
  }

  if (request.protobuf.empty()) return;

  alignas(16) std::array<std::uint8_t, 0x100> preview_block{};

  g_preview_block_ctor(preview_block.data(), 0, 0);

  const bool parsed =
      g_parse_from_array(preview_block.data(), request.protobuf.data(),
                         static_cast<int>(request.protobuf.size()));

  std::uint64_t item_id = 0;
  void* item_view = nullptr;

  if (parsed) item_view = g_create_preview_item(preview_block.data(), &item_id);

  bool stickers_synced = true;

  if (parsed && item_view &&
      request.presentation == systems::model_preview::Presentation::kWeapon) {
    stickers_synced = features::changer::attributes::SyncStickers(
        reinterpret_cast<features::changer::engine::ItemView*>(item_view),
        request.stickers);
  }

  g_preview_block_dtor(preview_block.data());

  if (!parsed || !item_view || item_id == 0 || !stickers_synced) return;

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);

    g_result.item_id = item_id;
    g_result.definition_index = request.definition_index;
    g_result.model_path = request.model_path;
    g_result.counter_terrorist = request.counter_terrorist;
    g_result.paint_kit = request.paint_kit;
    g_result.revision = request.revision;
    g_result.complex_cosmetics = request.complex_cosmetics;
    g_result.presentation = request.presentation;
    g_result.valid = true;

    g_processed_revision = request.revision;
    g_result_locked.store(true, std::memory_order_release);
  }
}

void systems::preview_item::ReleaseForNextRequest() {
  g_result_locked.store(false, std::memory_order_release);
}

bool systems::preview_item::IsResultLocked() {
  return g_result_locked.load(std::memory_order_acquire);
}

std::uint64_t systems::preview_item::GetLatestRevision() {
  return g_latest_revision.load(std::memory_order_acquire);
}

systems::preview_item::Result systems::preview_item::GetResult() {
  std::lock_guard<std::mutex> lock(g_state_mutex);

  Result result = g_result;
  const std::uint64_t latest_revision =
      g_latest_revision.load(std::memory_order_acquire);

  // If a newer committed UI state arrived while this temp item was locked, do
  // not build/promote the obsolete intermediate item. Keep the previous good
  // frame while ProcessPending coalesces directly to the newest protobuf
  // revision.
  result.current = result.valid && result.revision != 0 &&
                   result.revision == latest_revision;

  return result;
}
