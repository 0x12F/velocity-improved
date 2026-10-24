module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.changer;

import <atomic>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <fstream>;
import <memory>;
import <mutex>;
import <span>;
import <string>;
import <string_view>;
import <unordered_map>;
import <vector>;

export import :core.settings;
export import :core.systems.systems;

export {
namespace features::changer {

class EconItemSystem {
 public:
  enum class ItemCategory : std::uint8_t {
    kGun,
    kNife,
    kGlove,
    kAgent,
    kOther
  };

  struct PaintKit {
    int id{};
    std::string name{};
    std::string desc_token{};
    std::string name_token{};
    std::string localized_name{};
    float wear_min{};
    float wear_max{};
    bool legacy_model{};
    std::uint8_t rarity{};
  };

  struct ItemDef {
    std::int16_t def_index{};
    std::string item_class{};
    std::string name{};
    std::string localized_name{};
    std::string model_player{};
    std::string image_inventory{};
    int loadout_slot{};
    std::uint32_t used_by_classes{};
    ItemCategory category{};
    std::uint8_t rarity{};

    [[nodiscard]] int Team() const {
      if ((this->used_by_classes & 0xc) == 0xc) {
        return 0;
      }

      if (this->used_by_classes & 4) {
        return 2;
      }

      if (this->used_by_classes & 8) {
        return 3;
      }

      return 0;
    }
  };

  struct SkinEntry {
    std::int16_t def_index{};
    int paint_kit_id{};
  };

  struct SkinImage {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
    int width{};
    int height{};
  };

  [[nodiscard]] bool Initialize();

  [[nodiscard]] const std::vector<PaintKit>& PaintKits() const {
    return this->m_paint_kits_;
  }
  [[nodiscard]] const std::vector<ItemDef>& ItemDefs() const {
    return this->m_item_defs_;
  }

  [[nodiscard]] const std::vector<const ItemDef*>& Knives() const {
    return this->m_knives_;
  }
  [[nodiscard]] const std::vector<const ItemDef*>& Gloves() const {
    return this->m_gloves_;
  }
  [[nodiscard]] const std::vector<const ItemDef*>& Agents() const {
    return this->m_agents_;
  }
  [[nodiscard]] const std::vector<const ItemDef*>& Guns() const {
    return this->m_guns_;
  }
  [[nodiscard]] const std::vector<SkinEntry>& Skins() const {
    return this->m_skins_;
  }

  [[nodiscard]] const ItemDef* FindDef(std::int16_t def_index) const;
  [[nodiscard]] const PaintKit* FindPaintKit(int id) const;
  [[nodiscard]] const SkinImage* GetSkinImage(
      const std::string& image_inventory);
  [[nodiscard]] const SkinImage* GetSkinImage(std::int16_t def_index,
                                              int paint_kit_id);

  [[nodiscard]] int CombinedRarity(std::int16_t def_index,
                                   int paint_kit_id) const;

  void FlushSkinImages();

 private:
  enum class ImageState : std::uint8_t {
    kIdle,
    kLoading,
    kDecoded,
    kReady,
    kFailed
  };

  struct ImageEntry {
    SkinImage image{};
    std::atomic<ImageState> state{ImageState::kIdle};
    std::vector<std::vector<std::uint8_t>> mip_buffers{};
    std::uint32_t width{};
    std::uint32_t height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
  };

  bool ParseItemDefs(std::uintptr_t schema);
  bool ParsePaintKits(std::uintptr_t schema);
  void BuildIndices();
  void ResolveLocalizedNames();
  bool BuildVpkIndex();
  void BuildSkinIndex();

  void RequestDecode(const std::string& image_inventory);
  bool FinalizeTexture(ImageEntry& entry);

  [[nodiscard]] ItemCategory Classify(const char* item_class, int loadout_slot);
  [[nodiscard]] std::vector<std::byte> ReadVpk(const std::string& path);
  [[nodiscard]] bool DecodeVtex(std::span<const std::byte> data,
                                ImageEntry& out);
  [[nodiscard]] std::string BuildSkinImagePath(const ItemDef* def,
                                               const PaintKit* pk) const;

  std::vector<PaintKit> m_paint_kits_{};
  std::vector<ItemDef> m_item_defs_{};

  std::vector<const ItemDef*> m_knives_{};
  std::vector<const ItemDef*> m_gloves_{};
  std::vector<const ItemDef*> m_agents_{};
  std::vector<const ItemDef*> m_guns_{};
  std::vector<SkinEntry> m_skins_{};

  std::unordered_map<std::int16_t, std::size_t> m_def_index_map_{};
  std::unordered_map<int, std::size_t> m_paint_kit_map_{};

  struct VpkFileEntry {
    std::uint16_t archive_index{};
    std::uint32_t offset{};
    std::uint32_t length{};
  };

  std::unordered_map<std::string, VpkFileEntry> m_vpk_index_{};
  bool m_vpk_indexed_{};
  std::filesystem::path m_vpk_directory_{};

  std::unordered_map<std::string, std::unique_ptr<ImageEntry>> m_image_cache_{};
  std::mutex m_image_mutex_{};

  std::unordered_map<std::uint16_t, std::ifstream> m_archive_handles_{};
  std::mutex m_vpk_mutex_{};
};

features::changer::cosmetic_config::ItemSkinT MakeSkinConfiguration(
    const settings::Changer::AppliedSkin& skin);
void PublishConfiguration();

}  // namespace features::changer
}  // export
