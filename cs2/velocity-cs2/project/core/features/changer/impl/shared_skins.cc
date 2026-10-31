module;

#include "platform/platform.h"

#include <external/nlohmann/json.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <algorithm>;
import <array>;
import <chrono>;
import <cmath>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <mutex>;
import <string>;
import <stdexcept>;
import <unordered_map>;
import <unordered_set>;
import <vector>;

namespace features::changer::shared_skins {
namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
using Skin = cosmetic_config::ItemSkinT;
constexpr int kChannel = 0x564c;
constexpr int kReliableSend = 2;
constexpr int kKeychainSettleFrames = 6;
constexpr std::size_t kMaxPacketBytes = 64 * 1024;
constexpr auto kPublishInterval = std::chrono::seconds(2);
constexpr auto kPeerLifetime = std::chrono::seconds(10);
constexpr auto kReceiveInterval = std::chrono::milliseconds(250);

struct Transport {
  void *instance{};
  bool(__cdecl *send)(void *, std::uint64_t, const void *, std::uint32_t, int,
                      int){};
  bool(__cdecl *available)(void *, std::uint32_t *, int){};
  bool(__cdecl *read)(void *, void *, std::uint32_t, std::uint32_t *,
                      std::uint64_t *, int){};
  bool(__cdecl *close)(void *, std::uint64_t, int){};

  bool Initialize() {
    if (instance) return true;
    const auto module = GetModuleHandleW(L"steam_api64.dll");
    if (!module) return false;
    const auto get = reinterpret_cast<void *(__cdecl *)()>(
        GetProcAddress(module, "SteamAPI_SteamNetworking_v006"));
    send = reinterpret_cast<decltype(send)>(
        GetProcAddress(module, "SteamAPI_ISteamNetworking_SendP2PPacket"));
    available = reinterpret_cast<decltype(available)>(GetProcAddress(
        module, "SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
    read = reinterpret_cast<decltype(read)>(
        GetProcAddress(module, "SteamAPI_ISteamNetworking_ReadP2PPacket"));
    close = reinterpret_cast<decltype(close)>(GetProcAddress(
        module, "SteamAPI_ISteamNetworking_CloseP2PChannelWithUser"));
    if (!get || !send || !available || !read || !close) return false;
    instance = get();
    return instance != nullptr;
  }
} transport;

struct Peer {
  std::uint32_t pawn_handle{engine::kInvalidEhandleIndex};
  Clock::time_point received{};
  Clock::time_point last_packet{};
  std::unordered_map<std::uint16_t, Skin> skins;
  std::uint16_t knife{};
  std::uint64_t revision{};
  std::uint16_t agent{};
  std::uint16_t glove{};
  Skin glove_skin;
};

struct OriginalWeapon {
  engine::ItemHandle handle;
  std::uint16_t definition{};
  int subclass{};
  int quality{};
  int paint{};
  int seed{};
  int stat_trak{};
  float wear{};
  bool initialized{};
  bool disallow_soc{};
  bool restore_material{};
  std::string name;
  std::vector<std::byte> attributes;
  std::uint64_t applied_hash{};
  std::uint64_t applied_revision{};
  bool applied{};
  cosmetic_config::SkinKeychainT desired_keychain;
  int keychain_frames{};
  bool has_keychain{};
};

struct OriginalPawn {
  std::uint32_t handle{};
  float spawn_time{};
  std::string model;
  std::uint16_t glove_definition{};
  std::uint32_t glove_item_id{};
  int glove_paint{};
  bool glove_initialized{};
  bool glove_disallow_soc{};
  bool glove_restore_material{};
  std::vector<std::byte> glove_attributes;
  std::uint64_t applied_glove_hash{};
  std::uint16_t applied_glove_definition{};
  bool gloves_applied{};
  bool agent_applied{};
};

std::unordered_map<std::uint64_t, Peer> peers;
std::unordered_map<std::uint32_t, OriginalWeapon> originals;
std::unordered_map<std::uint32_t, OriginalPawn> original_pawns;
std::vector<engine::ItemHandle> restored_keychains;
std::unordered_set<std::uint64_t> visible_users;
std::mutex visible_mutex;
Clock::time_point last_publish{};
Clock::time_point last_receive{};
std::uint64_t local_id{};
bool regenerate_pending{};

void FlushMaterials() {
  if (regenerate_pending && engine::Connected())
    skin_application::RegenerateSkins();
  regenerate_pending = false;
}

engine::Weapon *Resolve(engine::ItemHandle handle) {
  return handle.Valid() ? reinterpret_cast<engine::Weapon *>(
                              systems::g_entities.Lookup(handle.Value()))
                        : nullptr;
}

engine::ItemView *Item(engine::Weapon *weapon) {
  auto *container = weapon ? weapon->MAttributeManager() : nullptr;
  return container ? container->MItem() : nullptr;
}

float ReadFloat(const Json &value, const char *key, float fallback,
                float minimum, float maximum) {
  const float result = value.value(key, fallback);
  if (!std::isfinite(result) || result < minimum || result > maximum)
    throw std::invalid_argument("invalid cosmetic value");
  return result;
}

Skin DecodeSkin(const Json &value) {
  Skin skin;
  skin.enabled = true;
  skin.paint_kit = value.at("paint").get<int>();
  if (skin.paint_kit < 0 ||
      (skin.paint_kit && !g_econ_item_system.FindPaintKit(skin.paint_kit)))
    throw std::invalid_argument("unknown paint kit");
  skin.seed = value.at("seed").get<int>();
  if (skin.seed < 0 || skin.seed > 1000)
    throw std::invalid_argument("invalid seed");
  skin.wear = ReadFloat(value, "wear", 0.0001f, 0.0001f, 1.0f);
  skin.stat_trak_enabled = value.at("stat").get<bool>();
  skin.stat_trak_kills = value.at("kills").get<int>();
  if (skin.stat_trak_kills < 0) throw std::invalid_argument("invalid kills");
  skin.custom_name = value.at("name").get<std::string>();
  if (skin.custom_name.size() > 63 ||
      skin.custom_name.find('\0') != std::string::npos)
    throw std::invalid_argument("invalid name");
  const auto &stickers = value.at("stickers");
  if (!stickers.is_array() || stickers.size() != skin.stickers.size())
    throw std::invalid_argument("invalid sticker count");
  for (std::size_t i = 0; i < skin.stickers.size(); ++i) {
    auto &sticker = skin.stickers[i];
    const auto &entry = stickers[i];
    sticker.enabled = entry.at("enabled").get<bool>();
    sticker.kit = entry.at("kit").get<int>();
    if (sticker.kit < 0 || sticker.kit > 1000000)
      throw std::invalid_argument("invalid sticker");
    sticker.wear = ReadFloat(entry, "wear", 0, 0, 1);
    sticker.scale = ReadFloat(entry, "scale", 1, 0.1f, 5);
    sticker.rotation = ReadFloat(entry, "rotation", 0, -180, 180);
    sticker.offset_x = ReadFloat(entry, "offset_x", 0, -0.5f, 0.5f);
    sticker.offset_y = ReadFloat(entry, "offset_y", 0, -0.5f, 0.5f);
  }
  const auto &keychain = value.at("keychain");
  skin.keychain.enabled = keychain.at("enabled").get<bool>();
  skin.keychain.id = keychain.at("id").get<int>();
  skin.keychain.seed = keychain.at("seed").get<int>();
  if (skin.keychain.id < 0 || skin.keychain.id > 10000 ||
      skin.keychain.seed < 0 || skin.keychain.seed > 100000)
    throw std::invalid_argument("invalid keychain");
  skin.keychain.offset_x = ReadFloat(keychain, "offset_x", 0, -20, 20);
  skin.keychain.offset_y = ReadFloat(keychain, "offset_y", 0, -20, 20);
  skin.keychain.offset_z = ReadFloat(keychain, "offset_z", 0, -20, 20);
  return skin;
}

Json EncodeSkin(std::uint16_t definition, const Skin &skin) {
  return Json{{"def", definition},
              {"paint", skin.paint_kit},
              {"seed", std::clamp(skin.seed, 0, 1000)},
              {"wear", std::clamp(skin.wear, 0.0001f, 1.0f)},
              {"stat", skin.stat_trak_enabled},
              {"kills", std::max(0, skin.stat_trak_kills)},
              {"name", skin.custom_name.substr(0, 63)},
              {"stickers", skin.stickers},
              {"keychain", skin.keychain}};
}

std::string Encode() {
  const auto &config = cosmetic_config::skin_changer;
  Json entries = Json::array();
  const auto append = [&](const auto &map) {
    for (const auto &[definition, skin] : map) {
      if (!config.enabled || !skin.enabled) continue;
      if (knife_application::IsKnifeDefinition(definition) &&
          definition != config.knife_definition)
        continue;
      entries.push_back(EncodeSkin(definition, skin));
    }
  };
  append(config.weapons);
  append(config.knives);
  Json glove = nullptr;
  if (config.enabled && config.glove_definition) {
    const auto found = config.gloves.find(config.glove_definition);
    glove = EncodeSkin(config.glove_definition,
                       found != config.gloves.end() ? found->second : Skin{});
  }
  return Json{{"protocol", "velocity.skins"},
              {"version", 1},
              {"knife", config.enabled ? config.knife_definition : 0},
              {"agent", config.enabled ? config.agent_definition : 0},
              {"glove", std::move(glove)},
              {"skins", std::move(entries)}}
      .dump();
}

void Discover() {
  std::unordered_set<std::uint64_t> present;
  for (const auto &player :
       systems::g_entities.GetByType(systems::Entities::Type::kPlayer)) {
    if (!player.ptr) continue;
    const auto id =
        memory::SafeRead<std::uint64_t>(
            player.ptr + SCHEMA("CBasePlayerController", "m_steamID"_hash))
            .value_or(0);
    // Public-universe individual Steam accounts, excluding bots and ourselves.
    if ((id >> 32) != 0x01100001 || id == local_id) continue;
    present.insert(id);
    const auto pawn =
        memory::SafeRead<std::uint32_t>(
            player.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash))
            .value_or(0xFFFFFFFFu);
    peers[id].pawn_handle = pawn;
  }
  std::erase_if(peers, [&](const auto &entry) {
    if (present.contains(entry.first)) return false;
    transport.close(transport.instance, entry.first, kChannel);
    return true;
  });
}

void Receive(Clock::time_point now) {
  std::array<char, kMaxPacketBytes> buffer;
  std::uint32_t size{};
  for (int count = 0;
       count < 64 && transport.available(transport.instance, &size, kChannel);
       ++count) {
    std::uint64_t sender{};
    std::uint32_t received{};

    if (!transport.read(transport.instance, buffer.data(),
                        static_cast<std::uint32_t>(buffer.size()), &received,
                        &sender, kChannel))
      break;
    const auto found = peers.find(sender);
    if (found == peers.end() || size > buffer.size() || received != size)
      continue;
    auto &peer = found->second;
    if (now - peer.last_packet < kReceiveInterval) continue;
    peer.last_packet = now;
    const auto packet =
        Json::parse(buffer.data(), buffer.data() + received,
                    [](int depth, Json::parse_event_t, Json &) {
                      if (depth > 12)
                        throw std::invalid_argument("packet nesting");
                      return true;
                    });
    if (packet.at("protocol") != "velocity.skins" ||
        packet.at("version") != 1)
      continue;
    const auto knife = packet.at("knife").get<int>();
    const auto *knife_info = g_econ_item_system.FindDef(knife);
    if (knife != 0 &&
        (!knife_info ||
         knife_info->category != EconItemSystem::ItemCategory::kNife))
      continue;
    const int agent = packet.at("agent").get<int>();
    const auto *agent_info = g_econ_item_system.FindDef(agent);
    if (agent && (!agent_info || agent_info->category !=
                                     EconItemSystem::ItemCategory::kAgent))
      continue;
    int glove = 0;
    Skin glove_skin;
    if (!packet.at("glove").is_null()) {
      const auto &entry = packet.at("glove");
      glove = entry.at("def").get<int>();
      const auto *info = g_econ_item_system.FindDef(glove);
      if (!info || info->category != EconItemSystem::ItemCategory::kGlove)
        continue;
      glove_skin = DecodeSkin(entry);
    }
    const auto &entries = packet.at("skins");
    if (!entries.is_array() || entries.size() > 80) continue;
    std::unordered_map<std::uint16_t, Skin> skins;
    for (const auto &entry : entries) {
      const int definition = entry.at("def").get<int>();
      if (definition <= 0 || definition > 32767)
        throw std::invalid_argument("invalid definition");
      const auto *item = g_econ_item_system.FindDef(definition);
      if (!item || (item->category != EconItemSystem::ItemCategory::kGun &&
                    item->category != EconItemSystem::ItemCategory::kNife))
        throw std::invalid_argument("unknown weapon");
      if (!skins
               .emplace(static_cast<std::uint16_t>(definition),
                        DecodeSkin(entry))
               .second)
        throw std::invalid_argument("duplicate weapon");
    }
    peer.skins = std::move(skins);
    peer.knife = static_cast<std::uint16_t>(knife);
    peer.agent = static_cast<std::uint16_t>(agent);
    peer.glove = static_cast<std::uint16_t>(glove);
    peer.glove_skin = std::move(glove_skin);
    peer.received = now;
    ++peer.revision;
  }
}

bool Capture(engine::Weapon *weapon, engine::ItemView *item,
             engine::ItemHandle handle, OriginalWeapon &original) {
  original.handle = handle;
  original.definition = item->MIItemDefinitionIndex();
  original.subclass = weapon->MNSubclassId();
  original.quality = item->MIEntityQuality();
  original.paint = weapon->MNFallbackPaintKit();
  original.seed = weapon->MNFallbackSeed();
  original.wear = weapon->MFlFallbackWear();
  original.stat_trak = weapon->MNFallbackStatTrak();
  original.initialized = item->MBInitialized();
  original.disallow_soc = item->MBDisallowSoc();
  original.restore_material = item->MBRestoreCustomMaterialAfterPrecache();
  original.name.assign(item->MSzCustomName(),
                       strnlen_s(item->MSzCustomName(), 160));
  return attributes::Capture(item, original.attributes);
}

void Restore(const OriginalWeapon &original) {
  auto *weapon = Resolve(original.handle);
  auto *item = Item(weapon);
  if (!item || !attributes::Restore(item, original.attributes)) return;
  if (item->MIItemDefinitionIndex() != original.definition)
    knife_application::Restore(weapon, item, original.definition,
                               original.subclass);
  weapon->MNFallbackPaintKit() = original.paint;
  weapon->MNFallbackSeed() = original.seed;
  weapon->MFlFallbackWear() = original.wear;
  weapon->MNFallbackStatTrak() = original.stat_trak;
  item->MIEntityQuality() = original.quality;
  item->MBInitialized() = original.initialized;
  item->MBDisallowSoc() = original.disallow_soc;
  item->MBRestoreCustomMaterialAfterPrecache() = original.restore_material;
  strncpy_s(item->MSzCustomName(), 161, original.name.c_str(), _TRUNCATE);
  item->MNameDescriptionPtr() = 0;
  skin_application::RefreshWorld(weapon, item);
  regenerate_pending = true;
  restored_keychains.push_back(original.handle);
}

void RestorePawn(OriginalPawn &original, bool gloves, bool agent) {
  auto *pawn = reinterpret_cast<engine::PlayerItems *>(
      systems::g_entities.Lookup(original.handle));
  if (!pawn || pawn->MFlLastSpawnTimeIndex() != original.spawn_time) return;
  if (agent && original.agent_applied) {
    agent_application::SetSharedModel(pawn, original.model);
    original.agent_applied = false;
  }
  if (gloves && original.gloves_applied) {
    auto *item = pawn->MEconGloves();
    if (!attributes::Restore(item, original.glove_attributes)) return;
    item->MIItemDefinitionIndex() = original.glove_definition;
    item->MIItemIdHigh() = original.glove_item_id;
    item->MBInitialized() = original.glove_initialized;
    item->MBDisallowSoc() = original.glove_disallow_soc;
    item->MBRestoreCustomMaterialAfterPrecache() =
        original.glove_restore_material;
    glove_application::RefreshShared(pawn, original.glove_paint);
    original.gloves_applied = false;
  }
}

void ApplyPawn(const Peer &peer, engine::PlayerItems *pawn) {
  auto found = original_pawns.find(peer.pawn_handle);
  if (found != original_pawns.end() &&
      found->second.spawn_time != pawn->MFlLastSpawnTimeIndex()) {
    original_pawns.erase(found);
    found = original_pawns.end();
  }
  if (found == original_pawns.end()) {
    if (!peer.agent && !peer.glove) return;
    OriginalPawn original;
    original.handle = peer.pawn_handle;
    original.spawn_time = pawn->MFlLastSpawnTimeIndex();
    original.model = agent_application::ModelName(pawn);
    if (original.model.empty()) return;
    auto *gloves = pawn->MEconGloves();
    if (!attributes::Capture(gloves, original.glove_attributes)) return;
    original.glove_definition = gloves->MIItemDefinitionIndex();
    original.glove_item_id = gloves->MIItemIdHigh();
    original.glove_paint = attributes::PaintKit(gloves);
    original.glove_initialized = gloves->MBInitialized();
    original.glove_disallow_soc = gloves->MBDisallowSoc();
    original.glove_restore_material =
        gloves->MBRestoreCustomMaterialAfterPrecache();
    found = original_pawns.emplace(peer.pawn_handle, std::move(original)).first;
  }
  auto &original = found->second;
  const auto *agent = g_econ_item_system.FindDef(peer.agent);
  if (agent && (agent->Team() == 0 || agent->Team() == pawn->MITeamNum()) &&
      !agent->model_player.empty()) {
    if (agent_application::ModelName(pawn) != agent->model_player)
      original.agent_applied =
          agent_application::SetSharedModel(pawn, agent->model_player) ||
          original.agent_applied;
  } else {
    RestorePawn(original, false, true);
  }
  if (peer.glove) {
    const auto hash = weapon_cosmetics::ComputeConfigHash(peer.glove_skin);
    if (!original.gloves_applied || original.applied_glove_hash != hash ||
        original.applied_glove_definition != peer.glove ||
        pawn->MEconGloves()->MIItemDefinitionIndex() != peer.glove) {
      if (glove_application::ApplyShared(pawn, peer.glove, peer.glove_skin)) {
        original.gloves_applied = true;
        original.applied_glove_hash = hash;
        original.applied_glove_definition = peer.glove;
      }
    }
  } else {
    RestorePawn(original, true, false);
  }
}

void Apply(Clock::time_point now) {
  std::unordered_set<std::uint32_t> active;
  std::unordered_set<std::uint32_t> active_pawns;
  std::unordered_set<std::uint64_t> users;
  users.insert(local_id);
  for (const auto &[id, peer] : peers) {
    if (peer.received == Clock::time_point{} ||
        now - peer.received > kPeerLifetime)
      continue;
    users.insert(id);
    const auto pawn_address = systems::g_entities.Lookup(peer.pawn_handle);
    auto *pawn = reinterpret_cast<engine::PlayerItems *>(pawn_address);
    if (!pawn || pawn->MIHealth() <= 0) continue;
    active_pawns.insert(peer.pawn_handle);
    ApplyPawn(peer, pawn);
    auto *services = pawn->MPWeaponServices();
    if (!services) continue;
    const auto &handles = services->MHMyWeapons();
    if (handles.Count() < 0 || handles.Count() > 64 || !handles.data) continue;
    for (int index = 0; index < handles.Count(); ++index) {
      const auto handle = handles.Element(index);
      auto *weapon = Resolve(handle);
      auto *item = Item(weapon);
      if (!item || systems::g_entities.Lookup(
                       weapon->MHOwnerEntity().Value()) != pawn_address)
        continue;
      const bool knife =
          knife_application::IsKnifeDefinition(item->MIItemDefinitionIndex());
      const auto definition =
          knife && peer.knife ? peer.knife : item->MIItemDefinitionIndex();
      const auto found = peer.skins.find(definition);
      if (found == peer.skins.end() && !(knife && peer.knife)) continue;
      if (!originals.contains(handle.Value())) {
        OriginalWeapon original;
        if (!Capture(weapon, item, handle, original)) continue;
        originals.emplace(handle.Value(), std::move(original));
      }
      active.insert(handle.Value());
      const bool model_changed =
          knife && peer.knife &&
          knife_application::Apply(weapon, item, peer.knife);
      const Skin skin = found == peer.skins.end() ? Skin{} : found->second;
      const auto kind = knife ? skin_application::ItemKind::kKnife
                              : skin_application::ItemKind::kWeapon;
      const auto hash = weapon_cosmetics::ComputeConfigHash(skin);
      // Network updates may overwrite fallback fields between publications.
      auto &original = originals.at(handle.Value());
      const bool changed = !original.applied || original.applied_hash != hash ||
                           original.applied_revision != peer.revision;
      if (model_changed || changed ||
          skin_application::NeedsUpdate(weapon, item, skin, false, kind)) {
        skin_application::Apply(weapon, item, skin, pawn, kind);
        regenerate_pending = true;
        original.applied_hash = hash;
        original.applied_revision = peer.revision;
        original.applied = true;
        original.desired_keychain =
            knife ? cosmetic_config::SkinKeychainT{} : skin.keychain;
        original.keychain_frames = kKeychainSettleFrames;
      }
    }
  }
  std::erase_if(originals, [&](const auto &entry) {
    if (active.contains(entry.first)) return false;
    Restore(entry.second);
    return true;
  });
  for (auto it = original_pawns.begin(); it != original_pawns.end();) {
    if (active_pawns.contains(it->first)) {
      ++it;
    } else {
      RestorePawn(it->second, true, true);
      it = original_pawns.erase(it);
    }
  }
  std::scoped_lock lock(visible_mutex);
  visible_users.swap(users);
}
}  // namespace

void Run() {
  if (!engine::Connected() || !cosmetic_config::skin_changer.shared) {
    Reset();
    return;
  }
  const auto now = Clock::now();
  if (!transport.instance && now - last_publish < kPublishInterval) return;
  if (!transport.Initialize()) {
    static bool reported = false;
    if (!reported) {
      logging::console::Print(
          xs("[shared_skins] Steam P2P interface unavailable; sharing "
             "disabled\n"));
      reported = true;
    }
    last_publish = now;
    return;
  }
  if (!local_id) {
    if (!steam::User::Initialize()) return;
    local_id = steam::User::GetSteamId();
    if (!local_id) return;
  }
  if (now - last_publish >= kPublishInterval) {
    Discover();
    if (!application::IsInteractiveEditActive()) {
      const auto packet = Encode();
      if (packet.size() <= kMaxPacketBytes) {
        for (const auto &[id, peer] : peers)
          // Sending implicitly accepts the peer's session request. Steam's
          // default relay fallback handles NAT without a hosted server.
          transport.send(transport.instance, id, packet.data(),
                         static_cast<std::uint32_t>(packet.size()),
                         kReliableSend, kChannel);
      }
    }
    last_publish = now;
  }
  if (now - last_receive >= kReceiveInterval) {
    Receive(now);
    last_receive = now;
  }
  Apply(now);
  FlushMaterials();
}

void Reset() {
  for (const auto &[key, original] : originals) Restore(original);
  originals.clear();
  for (auto &[handle, original] : original_pawns)
    RestorePawn(original, true, true);
  original_pawns.clear();
  FlushMaterials();
  if (transport.instance) {
    for (const auto &[id, peer] : peers)
      transport.close(transport.instance, id, kChannel);
  }
  peers.clear();
  local_id = 0;
  last_publish = {};
  last_receive = {};
  std::scoped_lock lock(visible_mutex);
  visible_users.clear();
}

bool IsUser(std::uint64_t steam_id) {
  std::scoped_lock lock(visible_mutex);
  return visible_users.contains(steam_id);
}

void OnEntityRemoved(std::uint32_t handle) {
  // Lookup() resolves an index without checking its serial number. Discard
  // snapshots before the engine can reuse that slot for a different entity.
  originals.erase(handle);
  original_pawns.erase(handle);
  std::erase_if(restored_keychains,
                [handle](auto value) { return value.Value() == handle; });
  for (auto &[id, peer] : peers) {
    if (peer.pawn_handle == handle)
      peer.pawn_handle = engine::kInvalidEhandleIndex;
  }
}

void AfterFrame() {
  for (const auto handle : restored_keychains) {
    if (auto *weapon = Resolve(handle)) weapon->AddKeyChainEntity();
  }
  restored_keychains.clear();
  for (auto &[handle, original] : originals) {
    if (original.keychain_frames <= 0 || --original.keychain_frames > 0)
      continue;
    auto *weapon = Resolve(original.handle);
    auto *item = Item(weapon);
    if (!item) continue;
    const bool enabled =
        original.desired_keychain.enabled && original.desired_keychain.id > 0;
    if (original.has_keychain) weapon->AddKeyChainEntity();
    if (enabled &&
        attributes::SetKeychainId(item, original.desired_keychain.id))
      weapon->AddKeyChainEntity();
    original.has_keychain = enabled;
  }
}
}  // namespace features::changer::shared_skins
