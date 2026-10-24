module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cctype>;
import <cstddef>;
import <filesystem>;
import <string_view>;
import <system_error>;
import <type_traits>;

namespace features::changer {
namespace {
// Verified against the game's paint_kits loader (client.dll, October 8, 2026).
constexpr std::uintptr_t kPaintKitCountOffset = 0x2A0;
constexpr std::uintptr_t kPaintKitNodesOffset = 0x2A8;
}  // namespace

bool EconItemSystem::Initialize() {
  std::uintptr_t schema{};
  auto schema_ready{false};
  constexpr auto kMaxAttempts{300};
  constexpr auto kRetryDelay{std::chrono::milliseconds(100)};

  // Early injection can precede the schema's item-definition and paint-kit
  // tables. Wait until both are populated, then parse exactly once.
  for (auto attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const auto system =
        memory::Call<std::uintptr_t>(addresses::globals::item_system);
    schema =
        system ? memory::SafeRead<std::uintptr_t>(system + 0x8).value_or(0) : 0;

    if (schema) {
      const auto item_count = memory::SafeRead<int>(schema + 0x128).value_or(0);
      const auto item_array =
          memory::SafeRead<std::uintptr_t>(schema + 0x130).value_or(0);
      const auto paint_count =
          memory::SafeRead<int>(schema + kPaintKitCountOffset).value_or(0);
      const auto paint_nodes =
          memory::SafeRead<std::uintptr_t>(schema + kPaintKitNodesOffset)
              .value_or(0);

      if (item_count > 0 && item_count <= 10000 && item_array &&
          paint_count > 0 && paint_count <= 10000 && paint_nodes) {
        schema_ready = true;
        break;
      }
    }

    if (attempt == 0) {
      logging::console::Print(xs("[econ] waiting for item schema"));
    }

    std::this_thread::sleep_for(kRetryDelay);
  }

  if (!schema_ready) {
    logging::console::Print(xs("[econ] timed out waiting for item schema"));
    return false;
  }

  if (!this->ParseItemDefs(schema)) {
    logging::console::Print(xs("[econ] failed to parse item definitions"));
    return false;
  }

  if (!this->ParsePaintKits(schema)) {
    logging::console::Print(xs("[econ] failed to parse paint kits"));
    return false;
  }

  this->BuildIndices();
  this->ResolveLocalizedNames();

  if (!this->BuildVpkIndex()) {
    logging::console::Print(xs("[econ] failed to build VPK index"));
    return false;
  }

  this->BuildSkinIndex();

  return true;
}

const EconItemSystem::ItemDef* EconItemSystem::FindDef(
    std::int16_t def_index) const {
  const auto it = this->m_def_index_map_.find(def_index);
  if (it == this->m_def_index_map_.end()) {
    return nullptr;
  }

  return &this->m_item_defs_[it->second];
}

const EconItemSystem::PaintKit* EconItemSystem::FindPaintKit(int id) const {
  const auto it = this->m_paint_kit_map_.find(id);
  if (it == this->m_paint_kit_map_.end()) {
    return nullptr;
  }

  return &this->m_paint_kits_[it->second];
}

const EconItemSystem::SkinImage* EconItemSystem::GetSkinImage(
    const std::string& image_inventory) {
  if (image_inventory.empty()) {
    return nullptr;
  }

  std::lock_guard lock(this->m_image_mutex_);

  auto it = this->m_image_cache_.find(image_inventory);
  if (it == this->m_image_cache_.end()) {
    auto entry = std::make_unique<ImageEntry>();
    it = this->m_image_cache_.emplace(image_inventory, std::move(entry)).first;
    this->RequestDecode(image_inventory);
    return nullptr;
  }

  const auto state = it->second->state.load(std::memory_order_acquire);
  if (state == ImageState::kReady) {
    return &it->second->image;
  }

  if (state == ImageState::kDecoded) {
    if (this->FinalizeTexture(*it->second)) {
      return &it->second->image;
    }
  }

  return nullptr;
}

const EconItemSystem::SkinImage* EconItemSystem::GetSkinImage(
    std::int16_t def_index, int paint_kit_id) {
  const auto def = this->FindDef(def_index);
  if (!def) {
    return nullptr;
  }

  const auto pk =
      (paint_kit_id != 0) ? this->FindPaintKit(paint_kit_id) : nullptr;
  const auto path = this->BuildSkinImagePath(def, pk);

  return this->GetSkinImage(path);
}

int EconItemSystem::CombinedRarity(std::int16_t def_index,
                                   int paint_kit_id) const {
  auto weapon_rarity{0};
  auto paint_rarity{0};
  auto category{ItemCategory::kOther};

  if (const auto def = this->FindDef(def_index)) {
    weapon_rarity = def->rarity;
    category = def->category;
  }

  if (const auto pk = this->FindPaintKit(paint_kit_id)) {
    paint_rarity = pk->rarity;
  }

  if (category == ItemCategory::kNife) {
    return 5;
  }

  return std::clamp(weapon_rarity + paint_rarity - 2, 0, 7);
}

void EconItemSystem::FlushSkinImages() {
  {
    std::lock_guard lock(this->m_image_mutex_);
    this->m_image_cache_.clear();
  }

  {
    std::lock_guard lock(this->m_vpk_mutex_);
    this->m_archive_handles_.clear();
  }
}

bool EconItemSystem::ParseItemDefs(std::uintptr_t schema) {
  const auto count = memory::Read<int>(schema + 0x128);
  const auto array = memory::Read<std::uintptr_t>(schema + 0x130);

  if (!array || count <= 0 || count > 10000) {
    return false;
  }

  this->m_item_defs_.reserve(count);

  for (auto i = 0; i < count; i++) {
    const auto entry_base = array + static_cast<std::uintptr_t>(32 * i);
    const auto def_index = memory::Read<int>(entry_base + 16);

    if (def_index < -1) {
      continue;
    }

    const auto def_ptr = memory::Read<std::uintptr_t>(entry_base + 24);
    if (!def_ptr) {
      continue;
    }

    ItemDef item{};
    item.def_index = memory::Read<std::int16_t>(def_ptr + 0x10);
    item.loadout_slot = memory::Read<int>(def_ptr + 0x338);
    item.used_by_classes = memory::Read<std::uint32_t>(def_ptr + 0x368);
    item.rarity = memory::Read<std::uint8_t>(def_ptr + 0x42);

    if (const auto name_ptr = memory::Read<std::uintptr_t>(def_ptr + 0x260);
        name_ptr) {
      item.name = memory::ReadString(name_ptr);
      item.item_class = item.name;
    }

    if (const auto model_ptr = memory::Read<std::uintptr_t>(def_ptr + 0x148);
        model_ptr) {
      item.model_player = memory::ReadString(model_ptr);
    }

    if (const auto img_ptr = memory::Read<std::uintptr_t>(def_ptr + 0xA8);
        img_ptr) {
      item.image_inventory = memory::ReadString(img_ptr);
    }

    if (const auto token_ptr = memory::Read<std::uintptr_t>(def_ptr + 0x70);
        token_ptr) {
      const auto token = memory::ReadString(token_ptr);
      const auto localized = memory::CallVfunc<const char*>(
          addresses::globals::localize, 17, token.c_str());

      if (localized && *localized &&
          std::strcmp(localized, token.c_str()) != 0) {
        item.localized_name = localized;
      } else {
        item.localized_name = item.name;
      }
    } else {
      item.localized_name = item.name;
    }

    item.category = this->Classify(item.item_class.c_str(), item.loadout_slot);
    if (item.category == ItemCategory::kAgent) {
      item.model_player = agent_models::Resolve(
          item.name, item.model_player, item.localized_name,
          [](const std::string& model) {
            const auto compiled_path = model + "_c";
            return addresses::globals::file_system &&
                   memory::CallVfunc<bool>(addresses::globals::file_system, 21,
                                           compiled_path.c_str(), "GAME");
          });
    }
    this->m_item_defs_.push_back(std::move(item));
  }

  return !this->m_item_defs_.empty();
}

bool EconItemSystem::ParsePaintKits(std::uintptr_t schema) {
  const auto count =
      memory::SafeRead<int>(schema + kPaintKitCountOffset).value_or(0);
  const auto nodes =
      memory::SafeRead<std::uintptr_t>(schema + kPaintKitNodesOffset)
          .value_or(0);

  if (!nodes || count <= 0 || count > 10000) {
    return false;
  }

  this->m_paint_kits_.reserve(count);

  for (auto i = 0; i < count; i++) {
    const auto node_base = nodes + static_cast<std::uintptr_t>(32 * i);
    const auto pk_ptr =
        memory::SafeRead<std::uintptr_t>(node_base + 24).value_or(0);

    if (!pk_ptr) {
      continue;
    }

    PaintKit pk{};
    const auto id = memory::SafeRead<int>(pk_ptr + 0x00);
    const auto wear_min = memory::SafeRead<float>(pk_ptr + 0x6C);
    const auto wear_max = memory::SafeRead<float>(pk_ptr + 0x70);
    const auto legacy_model = memory::SafeRead<std::uint8_t>(pk_ptr + 0xAE);
    const auto rarity = memory::SafeRead<int>(pk_ptr + 0x44);
    if (!id || !wear_min || !wear_max || !legacy_model || !rarity ||
        !std::isfinite(*wear_min) || !std::isfinite(*wear_max) ||
        *wear_min < 0.0f || *wear_max > 1.0f || *wear_min > *wear_max) {
      logging::console::Print(
          xs("[econ] invalid paint-kit record; check schema layout"));
      return false;
    }
    pk.id = *id;
    pk.wear_min = *wear_min;
    pk.wear_max = *wear_max;
    pk.legacy_model = *legacy_model != 0;
    pk.rarity = static_cast<std::uint8_t>(*rarity & 0xFF);

    if (const auto name_ptr =
            memory::SafeRead<std::uintptr_t>(pk_ptr + 0x08).value_or(0);
        name_ptr) {
      pk.name = memory::ReadString(name_ptr);
    }

    if (const auto desc_ptr =
            memory::SafeRead<std::uintptr_t>(pk_ptr + 0x10).value_or(0);
        desc_ptr) {
      pk.desc_token = memory::ReadString(desc_ptr);
    }

    if (const auto name_token_ptr =
            memory::SafeRead<std::uintptr_t>(pk_ptr + 0x18).value_or(0);
        name_token_ptr) {
      pk.name_token = memory::ReadString(name_token_ptr);
    }

    if (pk.name.empty()) {
      logging::console::Print(
          xs("[econ] unreadable paint-kit name; check schema layout"));
      return false;
    }

    this->m_paint_kits_.push_back(std::move(pk));
  }

  return !this->m_paint_kits_.empty();
}

void EconItemSystem::BuildIndices() {
  for (auto i = 0ull; i < this->m_item_defs_.size(); i++) {
    const auto& def = this->m_item_defs_[i];
    this->m_def_index_map_[def.def_index] = i;

    switch (def.category) {
      case ItemCategory::kNife:
        this->m_knives_.push_back(&def);
        break;
      case ItemCategory::kGlove:
        this->m_gloves_.push_back(&def);
        break;
      case ItemCategory::kAgent:
        this->m_agents_.push_back(&def);
        break;
      case ItemCategory::kGun:
        this->m_guns_.push_back(&def);
        break;
      default:
        break;
    }
  }

  for (auto i = 0ull; i < this->m_paint_kits_.size(); i++) {
    this->m_paint_kit_map_[this->m_paint_kits_[i].id] = i;
  }
}

void EconItemSystem::ResolveLocalizedNames() {
  auto resolved{0};
  auto fallback{0};

  for (auto& pk : this->m_paint_kits_) {
    if (!pk.name_token.empty()) {
      const auto localized = memory::CallVfunc<const char*>(
          addresses::globals::localize, 17, pk.name_token.c_str());
      if (localized && *localized &&
          std::strcmp(localized, pk.name_token.c_str()) != 0) {
        pk.localized_name = localized;
        resolved++;
        continue;
      }
    }

    pk.localized_name = pk.name;
    fallback++;
  }
}

bool EconItemSystem::BuildVpkIndex() {
  if (this->m_vpk_indexed_) {
    return !this->m_vpk_index_.empty();
  }

  this->m_vpk_indexed_ = true;

  const auto csgo_directory = game_path::CsgoDirectory();
  if (!csgo_directory) {
    return false;
  }

  std::ifstream file(*csgo_directory / L"pak01_dir.vpk", std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  this->m_vpk_directory_ = *csgo_directory;

#pragma pack(push, 1)
  struct VpkHeader {
    std::uint32_t signature;
    std::uint32_t version;
    std::uint32_t tree_size;
    std::uint32_t file_data_section_size;
    std::uint32_t archive_md5_section_size;
    std::uint32_t other_md5_section_size;
    std::uint32_t signature_section_size;
  };

  struct VpkEntry {
    std::uint32_t crc;
    std::uint16_t preload_bytes;
    std::uint16_t archive_index;
    std::uint32_t entry_offset;
    std::uint32_t entry_length;
    std::uint16_t terminator;
  };
#pragma pack(pop)

  VpkHeader header{};
  file.read(reinterpret_cast<char*>(&header), sizeof(header));

  if (header.signature != 0x55AA1234 || header.version != 2) {
    return false;
  }

  const auto tree_end = static_cast<std::streamoff>(sizeof(VpkHeader)) +
                        static_cast<std::streamoff>(header.tree_size);

  while (file.tellg() < tree_end) {
    std::string extension;
    std::getline(file, extension, '\0');

    if (extension.empty()) {
      break;
    }

    const auto is_vtex = extension == xs("vtex_c");

    while (true) {
      std::string dir_path;
      std::getline(file, dir_path, '\0');

      if (dir_path.empty()) {
        break;
      }

      const auto is_econ =
          is_vtex &&
          dir_path.find(xs("panorama/images/econ")) != std::string::npos;

      while (true) {
        std::string filename;
        std::getline(file, filename, '\0');

        if (filename.empty()) {
          break;
        }

        VpkEntry entry{};
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));

        if (entry.preload_bytes > 0) {
          file.seekg(entry.preload_bytes, std::ios::cur);
        }

        if (!is_econ) {
          continue;
        }

        constexpr auto kPrefixLen = std::string_view("panorama/images/").size();
        auto key = dir_path.substr(kPrefixLen) + "/" + filename;

        this->m_vpk_index_[std::move(key)] = VpkFileEntry{
            entry.archive_index, entry.entry_offset, entry.entry_length};
      }
    }
  }

  return !this->m_vpk_index_.empty();
}

void EconItemSystem::BuildSkinIndex() {
  std::unordered_map<std::string, int> pk_by_name;
  pk_by_name.reserve(this->m_paint_kits_.size());

  for (const auto& pk : this->m_paint_kits_) {
    pk_by_name.emplace(pk.name, pk.id);
  }

  std::unordered_map<std::string, std::int16_t> def_by_name;
  def_by_name.reserve(this->m_item_defs_.size());

  for (const auto& d : this->m_item_defs_) {
    if (!d.name.empty()) {
      def_by_name.emplace(d.name, d.def_index);
    }
  }

  constexpr std::string_view kPrefix{"econ/default_generated/"};
  constexpr std::string_view kSuffix{"_light_png"};

  for (const auto& [path, _] : this->m_vpk_index_) {
    if (!path.starts_with(kPrefix) || !path.ends_with(kSuffix)) {
      continue;
    }

    std::string_view stem(path);
    stem.remove_prefix(kPrefix.size());
    stem.remove_suffix(kSuffix.size());

    std::int16_t def_idx{-1};
    std::string_view pk_name;

    for (auto i = stem.find('_', 1); i != std::string_view::npos;
         i = stem.find('_', i + 1)) {
      const auto candidate = std::string(stem.substr(0, i));
      const auto it = def_by_name.find(candidate);

      if (it == def_by_name.end()) {
        continue;
      }

      def_idx = it->second;
      pk_name = stem.substr(i + 1);
    }

    if (def_idx == -1 || pk_name.empty()) {
      continue;
    }

    const auto pk_it = pk_by_name.find(std::string(pk_name));
    if (pk_it == pk_by_name.end()) {
      continue;
    }

    this->m_skins_.push_back({def_idx, pk_it->second});
  }
}

void EconItemSystem::RequestDecode(const std::string& image_inventory) {
  const auto key = image_inventory + xs("_png");

  std::vector<std::byte> data;
  {
    std::lock_guard lock(this->m_vpk_mutex_);
    data = this->ReadVpk(key);
  }

  if (data.empty()) {
    this->m_image_cache_[image_inventory]->state.store(
        ImageState::kFailed, std::memory_order_release);
    return;
  }

  this->m_image_cache_[image_inventory]->state.store(ImageState::kLoading,
                                                     std::memory_order_release);

  threadpool::Run([this, inv = image_inventory, buf = std::move(data)]() {
    std::lock_guard lock(this->m_image_mutex_);

    const auto it = this->m_image_cache_.find(inv);
    if (it == this->m_image_cache_.end()) {
      return;
    }

    if (this->DecodeVtex(std::span<const std::byte>(buf.data(), buf.size()),
                         *it->second)) {
      it->second->state.store(ImageState::kDecoded, std::memory_order_release);
    } else {
      it->second->state.store(ImageState::kFailed, std::memory_order_release);
    }
  });
}

bool EconItemSystem::FinalizeTexture(ImageEntry& entry) {
  const auto device = xdraw::device();
  if (!device) {
    entry.state.store(ImageState::kFailed, std::memory_order_release);
    return false;
  }

  if (entry.mip_buffers.empty()) {
    entry.state.store(ImageState::kFailed, std::memory_order_release);
    return false;
  }

  auto upload_format = entry.format;
  std::vector<std::uint8_t> rgba_pixels;

  if (entry.format == DXGI_FORMAT_BC7_UNORM) {
    rgba_pixels.resize(static_cast<std::size_t>(entry.width) * entry.height *
                       4);
    bc7::decode_image(entry.mip_buffers[0].data(), rgba_pixels.data(),
                      static_cast<int>(entry.width),
                      static_cast<int>(entry.height));
    upload_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }

  const auto upload_data =
      upload_format == DXGI_FORMAT_R8G8B8A8_UNORM && !rgba_pixels.empty()
          ? rgba_pixels.data()
          : entry.mip_buffers[0].data();
  const auto upload_pitch = entry.width * 4;

  D3D11_TEXTURE2D_DESC td{};
  td.Width = entry.width;
  td.Height = entry.height;
  td.MipLevels = 0;
  td.ArraySize = 1;
  td.Format = upload_format;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  if (FAILED(device->CreateTexture2D(&td, nullptr, &tex))) {
    entry.state.store(ImageState::kFailed, std::memory_order_release);
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  device->GetImmediateContext(&ctx);

  ctx->UpdateSubresource(tex.Get(), 0, nullptr, upload_data, upload_pitch, 0);

  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
  sv.Format = upload_format;
  sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sv.Texture2D.MipLevels = static_cast<UINT>(-1);

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  if (FAILED(device->CreateShaderResourceView(tex.Get(), &sv, &srv))) {
    entry.state.store(ImageState::kFailed, std::memory_order_release);
    return false;
  }

  ctx->GenerateMips(srv.Get());

  entry.image.srv = std::move(srv);
  entry.image.width = static_cast<int>(entry.width);
  entry.image.height = static_cast<int>(entry.height);
  entry.mip_buffers.clear();
  entry.mip_buffers.shrink_to_fit();
  entry.state.store(ImageState::kReady, std::memory_order_release);

  return true;
}

EconItemSystem::ItemCategory EconItemSystem::Classify(const char* item_class,
                                                      int loadout_slot) {
  if (loadout_slot == 38) {
    return ItemCategory::kAgent;
  }

  if (loadout_slot == 41) {
    return ItemCategory::kGlove;
  }

  if (std::strncmp(item_class, xs("weapon_knife"), 12) == 0) {
    return ItemCategory::kNife;
  }

  if (std::strncmp(item_class, xs("weapon_"), 7) == 0) {
    if (std::strcmp(item_class, xs("weapon_flashbang")) == 0 ||
        std::strcmp(item_class, xs("weapon_hegrenade")) == 0 ||
        std::strcmp(item_class, xs("weapon_smokegrenade")) == 0 ||
        std::strcmp(item_class, xs("weapon_molotov")) == 0 ||
        std::strcmp(item_class, xs("weapon_decoy")) == 0 ||
        std::strcmp(item_class, xs("weapon_incgrenade")) == 0 ||
        std::strcmp(item_class, xs("weapon_c4")) == 0 ||
        std::strcmp(item_class, xs("weapon_healthshot")) == 0 ||
        std::strcmp(item_class, xs("weapon_taser")) == 0 ||
        std::strcmp(item_class, xs("weapon_knifegg")) == 0) {
      return ItemCategory::kOther;
    }

    return ItemCategory::kGun;
  }

  return ItemCategory::kOther;
}

std::vector<std::byte> EconItemSystem::ReadVpk(const std::string& path) {
  const auto it = this->m_vpk_index_.find(path);
  if (it == this->m_vpk_index_.end()) {
    return {};
  }

  const auto& entry = it->second;
  auto& stream = this->m_archive_handles_[entry.archive_index];

  if (!stream.is_open()) {
    char archive_name[32];
    std::snprintf(archive_name, sizeof(archive_name), xs("pak01_%03d.vpk"),
                  entry.archive_index);

    stream.open(this->m_vpk_directory_ / archive_name, std::ios::binary);
    if (!stream.is_open()) {
      return {};
    }
  }

  stream.seekg(entry.offset);

  std::vector<std::byte> data(entry.length);
  stream.read(reinterpret_cast<char*>(data.data()), entry.length);

  return data;
}

bool EconItemSystem::DecodeVtex(std::span<const std::byte> data,
                                ImageEntry& out) {
  const auto raw = reinterpret_cast<const std::uint8_t*>(data.data());
  const auto size = data.size();

  if (size < 28) {
    return false;
  }

  const auto file_size = *reinterpret_cast<const std::uint32_t*>(raw + 0x00);
  const auto header_version =
      *reinterpret_cast<const std::uint16_t*>(raw + 0x04);
  const auto block_count = *reinterpret_cast<const std::uint32_t*>(raw + 0x0C);

  if (header_version != 12 || block_count == 0 || block_count > 64) {
    return false;
  }

  constexpr auto kBlockHeaderSize{16u};
  constexpr auto kBlockEntrySize{12u};
  constexpr auto kDataFourcc{'D' | ('A' << 8) | ('T' << 16) | ('A' << 24)};

  const std::uint8_t* data_block{nullptr};
  auto data_block_offset{0ull};

  for (auto i = 0u; i < block_count; i++) {
    const auto entry_pos = kBlockHeaderSize + i * kBlockEntrySize;
    if (static_cast<std::size_t>(entry_pos) + kBlockEntrySize > size) {
      break;
    }

    const auto type = *reinterpret_cast<const std::uint32_t*>(raw + entry_pos);
    const auto offset =
        *reinterpret_cast<const std::uint32_t*>(raw + entry_pos + 4);

    if (type != kDataFourcc) {
      continue;
    }

    const auto data_start = entry_pos + 4 + offset;
    if (static_cast<std::size_t>(data_start) + 0x28 > size) {
      return false;
    }

    data_block = raw + data_start;
    data_block_offset = data_start;
    break;
  }

  if (!data_block) {
    return false;
  }

  const auto width = static_cast<std::uint32_t>(
      *reinterpret_cast<const std::uint16_t*>(data_block + 0x14));
  const auto height = static_cast<std::uint32_t>(
      *reinterpret_cast<const std::uint16_t*>(data_block + 0x16));
  const auto format = *reinterpret_cast<const std::uint8_t*>(data_block + 0x1A);
  const auto mip_count = static_cast<std::uint32_t>(
      *reinterpret_cast<const std::uint8_t*>(data_block + 0x1B));
  const auto extra_data_offset =
      *reinterpret_cast<const std::uint32_t*>(data_block + 0x20);
  const auto extra_data_count =
      *reinterpret_cast<const std::uint32_t*>(data_block + 0x24);

  if (width == 0 || height == 0 || mip_count == 0) {
    return false;
  }

  auto dxgi_format{DXGI_FORMAT_UNKNOWN};
  auto block_bytes{0u};
  auto bytes_per_pixel{0u};

  switch (format) {
    case 1:
      dxgi_format = DXGI_FORMAT_BC1_UNORM;
      block_bytes = 8;
      break;
    case 2:
      dxgi_format = DXGI_FORMAT_BC3_UNORM;
      block_bytes = 16;
      break;
    case 4:
      dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      bytes_per_pixel = 4;
      break;
    case 19:
      dxgi_format = DXGI_FORMAT_BC6H_UF16;
      block_bytes = 16;
      break;
    case 20:
      dxgi_format = DXGI_FORMAT_BC7_UNORM;
      block_bytes = 16;
      break;
    case 27:
      dxgi_format = DXGI_FORMAT_BC4_UNORM;
      block_bytes = 8;
      break;
    case 28:
      dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
      bytes_per_pixel = 4;
      break;
    default:
      return false;
  }

  auto calc_mip_size = [&](std::uint32_t w, std::uint32_t h) -> std::uint32_t {
    if (block_bytes > 0) {
      const auto bw = std::max(4u, (w + 3u) & ~3u);
      const auto bh = std::max(4u, (h + 3u) & ~3u);
      return (bw / 4) * (bh / 4) * block_bytes;
    }

    return w * h * bytes_per_pixel;
  };

  constexpr auto kExtraCompressedMipSize{4u};
  auto is_compressed{false};
  const std::uint32_t* compressed_sizes{nullptr};
  auto compressed_sizes_count{0u};

  if (extra_data_count > 0) {
    const auto table_pos = static_cast<std::size_t>(0x20) + extra_data_offset;

    for (auto i = 0u; i < extra_data_count; i++) {
      const auto entry_pos = table_pos + static_cast<std::size_t>(i) * 12;
      if (data_block_offset + entry_pos + 12 > size) {
        return false;
      }

      const auto etype =
          *reinterpret_cast<const std::uint32_t*>(data_block + entry_pos);
      const auto eoff =
          *reinterpret_cast<const std::uint32_t*>(data_block + entry_pos + 4);
      const auto esize =
          *reinterpret_cast<const std::uint32_t*>(data_block + entry_pos + 8);

      if (etype != kExtraCompressedMipSize) {
        continue;
      }

      const auto body_pos = entry_pos + 4 + eoff;
      if (data_block_offset + body_pos + 12 > size || esize < 12) {
        return false;
      }

      const auto int1 =
          *reinterpret_cast<const std::uint32_t*>(data_block + body_pos);
      const auto mips_offset =
          *reinterpret_cast<const std::uint32_t*>(data_block + body_pos + 4);
      const auto mips_count_in_table =
          *reinterpret_cast<const std::uint32_t*>(data_block + body_pos + 8);

      if (int1 > 1 || mips_count_in_table != mip_count) {
        return false;
      }

      const auto array_pos = body_pos + 4 + mips_offset;
      if (data_block_offset + array_pos + mips_count_in_table * 4u > size) {
        return false;
      }

      is_compressed = (int1 == 1);
      compressed_sizes =
          reinterpret_cast<const std::uint32_t*>(data_block + array_pos);
      compressed_sizes_count = mips_count_in_table;
      break;
    }
  }

  const auto pixel_start = static_cast<std::size_t>(file_size);
  if (pixel_start >= size) {
    return false;
  }

  auto on_disk_size_for = [&](std::uint32_t mip_level) -> std::uint32_t {
    const auto mw = std::max(1u, width >> mip_level);
    const auto mh = std::max(1u, height >> mip_level);
    const auto uncompressed = calc_mip_size(mw, mh);

    if (!is_compressed || compressed_sizes == nullptr ||
        mip_level >= compressed_sizes_count) {
      return uncompressed;
    }

    const auto compressed = compressed_sizes[mip_level];
    return (compressed >= uncompressed) ? uncompressed : compressed;
  };

  std::vector<std::vector<std::uint8_t>> mip_buffers(1);
  auto cursor = pixel_start;

  for (auto j = mip_count; j-- > 0u;) {
    const auto on_disk = on_disk_size_for(j);

    if (cursor + on_disk > size) {
      return false;
    }

    if (j == 0) {
      const auto uncompressed = calc_mip_size(width, height);
      mip_buffers[0].resize(uncompressed);

      if (!is_compressed || on_disk >= uncompressed) {
        if (on_disk != uncompressed) {
          return false;
        }

        std::memcpy(mip_buffers[0].data(), raw + cursor, uncompressed);
      } else {
        const auto decoded = LZ4_decompress_safe(
            reinterpret_cast<const char*>(raw + cursor),
            reinterpret_cast<char*>(mip_buffers[0].data()),
            static_cast<int>(on_disk), static_cast<int>(uncompressed));
        if (decoded != static_cast<int>(uncompressed)) {
          return false;
        }
      }
    }

    cursor += on_disk;
  }

  out.mip_buffers = std::move(mip_buffers);
  out.width = width;
  out.height = height;
  out.format = dxgi_format;

  return true;
}

std::string EconItemSystem::BuildSkinImagePath(const ItemDef* def,
                                               const PaintKit* pk) const {
  if (!pk || pk->id == 0) {
    return def->image_inventory;
  }

  std::string_view base = def->image_inventory;
  if (base.empty()) {
    base = def->name;
  }

  auto slash = base.find_last_of('/');
  std::string_view stem =
      (slash == std::string_view::npos) ? base : base.substr(slash + 1);

  return std::string(xs("econ/default_generated/")) + std::string(stem) + "_" +
         pk->name + xs("_light");
}

}  // namespace features::changer
