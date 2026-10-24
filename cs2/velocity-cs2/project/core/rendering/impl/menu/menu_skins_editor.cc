module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;

namespace rendering {
namespace {
using ItemSystem = features::changer::EconItemSystem;
namespace backend = features::changer::runtime;

struct EditorState {
  bool expanded{true};
  int item_index{};
  int section{};
  int sticker_slot{};
  int sticker_index{};
  int last_subtab{-1};
  std::int16_t definition{};
  std::int16_t last_browsing_definition{};
  settings::Changer::AppliedSkin skin;
  settings::Changer::AppliedSkin saved_skin;
  std::string sticker_search;
  float yaw{};
  float pitch{};
  float zoom{1.0f};
  bool rotating{};
};

EditorState editor;

class ClippedClicks {
 public:
  explicit ClippedClicks(const xui::rect& bounds)
      : input_(xui::ctx().input),
        clicked_(input_.mouse_clicked),
        double_clicked_(input_.mouse_double_clicked),
        right_clicked_(input_.rmb_clicked) {
    // Native widgets test their own bounds, including portions scrolled
    // outside this child. Those portions must not consume browser clicks.
    if (!input_.in_rect(bounds))
      input_.mouse_clicked = input_.mouse_double_clicked = input_.rmb_clicked =
          false;
  }

  ~ClippedClicks() {
    input_.mouse_clicked = clicked_;
    input_.mouse_double_clicked = double_clicked_;
    input_.rmb_clicked = right_clicked_;
  }

  ClippedClicks(const ClippedClicks&) = delete;
  ClippedClicks& operator=(const ClippedClicks&) = delete;

 private:
  xui::input_state& input_;
  bool clicked_;
  bool double_clicked_;
  bool right_clicked_;
};

bool Toggle(std::string_view name, bool& value) {
  const auto label = std::format("{}: {}", name, value ? "on" : "off");
  if (!xui::button(label, xui::layout::item_width(), 22.0f)) return false;
  value = !value;
  return true;
}

const std::vector<const ItemSystem::ItemDef*>& ItemsForTab(int tab) {
  const auto& econ = features::changer::g_econ_item_system;
  switch (tab) {
    case 1:
      return econ.Knives();
    case 2:
      return econ.Gloves();
    case 3:
      return econ.Agents();
    default:
      return econ.Guns();
  }
}

void EquipItem(const ItemSystem::ItemDef& item) {
  auto& skins = settings::g_changer.skins.data;
  if (item.category == ItemSystem::ItemCategory::kNife ||
      item.category == ItemSystem::ItemCategory::kGlove) {
    const auto& items = item.category == ItemSystem::ItemCategory::kNife
                            ? features::changer::g_econ_item_system.Knives()
                            : features::changer::g_econ_item_system.Gloves();
    for (const auto* other : items)
      if (other->def_index != item.def_index) skins.erase(other->def_index);
  }
  skins[item.def_index] = editor.skin;
  editor.saved_skin = editor.skin;
}

void DrawStickers() {
  static constexpr const char* kSlots[]{"1", "2", "3", "4", "5"};
  xui::combo("sticker slot", editor.sticker_slot, kSlots, 5);
  auto& sticker = editor.skin.stickers[editor.sticker_slot];
  Toggle("sticker", sticker.enabled);
  xui::text_input("search stickers", editor.sticker_search, 64, "search...");

  const auto& kits = backend::StickerKits();
  std::vector<const char*> names{"none"};
  std::vector<int> ids{0};
  auto search = editor.sticker_search;
  std::ranges::transform(search, search.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const auto& kit : kits) {
    auto name = kit.name;
    std::ranges::transform(name, name.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (kit.id == sticker.kit || search.empty() ||
        name.find(search) != std::string::npos) {
      names.push_back(kit.name.c_str());
      ids.push_back(kit.id);
    }
  }
  // combo overlays hold the selection address until dismissed.
  const auto combo_id = xui::make_id("sticker kit");
  if (!xui::overlays::find(combo_id)) {
    const auto found = std::ranges::find(ids, sticker.kit);
    editor.sticker_index =
        found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
  }
  if (xui::combo("sticker kit", editor.sticker_index, names.data(),
                 static_cast<int>(names.size())) &&
      editor.sticker_index >= 0 &&
      editor.sticker_index < static_cast<int>(ids.size())) {
    sticker.kit = ids[editor.sticker_index];
    sticker.enabled = sticker.kit != 0;
  }
  xui::slider_float("sticker wear", sticker.wear, 0.0f, 1.0f, "%.3f");
  xui::slider_float("sticker scale", sticker.scale, 0.1f, 5.0f);
  xui::slider_float("sticker rotation", sticker.rotation, -180.0f, 180.0f,
                    "%.0f");
  xui::slider_float("sticker offset x", sticker.offset_x, -0.5f, 0.5f, "%.3f");
  xui::slider_float("sticker offset y", sticker.offset_y, -0.5f, 0.5f, "%.3f");
}

void DrawKeychain() {
  auto& keychain = editor.skin.keychain;
  Toggle("charm", keychain.enabled);
  xui::slider_int("charm id", keychain.id, 0, 10000);
  xui::slider_int("charm seed", keychain.seed, 0, 100000);
  xui::slider_float("charm offset x", keychain.offset_x, -20.0f, 20.0f, "%.3f");
  xui::slider_float("charm offset y", keychain.offset_y, -20.0f, 20.0f, "%.3f");
  xui::slider_float("charm offset z", keychain.offset_z, -20.0f, 20.0f, "%.3f");
}

void DrawItemOptions(const ItemSystem::ItemDef& item) {
  if (item.category == ItemSystem::ItemCategory::kAgent) {
    xui::text("equip agents in the browser below", tokens::col_text_dim);
    return;
  }
  static constexpr const char* kSections[]{"finish", "stickers", "charm"};
  const bool gun = item.category == ItemSystem::ItemCategory::kGun;
  xui::combo("options", editor.section, kSections, gun ? 3 : 1);
  if (!gun) editor.section = 0;

  if (editor.section == 1)
    DrawStickers();
  else if (editor.section == 2)
    DrawKeychain();
  else {
    xui::slider_float("wear##editor", editor.skin.wear, 0.0001f, 1.0f, "%.4f");
    xui::slider_int("seed##editor", editor.skin.seed, 0, 1000);
    if (item.category != ItemSystem::ItemCategory::kGlove) {
      Toggle("stattrak", editor.skin.stattrak);
      xui::slider_int("stattrak kills", editor.skin.stattrak_kills, 0, 999999);
      xui::text_input("name tag", editor.skin.custom_name, 63,
                      "custom name...");
    }
    if (xui::button("apply item", xui::layout::item_width(), 22.0f))
      EquipItem(item);
    if (xui::button("refresh skins", xui::layout::item_width(), 22.0f))
      backend::ForceUpdate();
  }
  if (editor.skin != editor.saved_skin) EquipItem(item);
}

void DrawPreview(const ItemSystem::ItemDef& item, const xui::rect& area) {
  const bool editing = xui::ctx().active_slider != 0 ||
                       xui::ctx().active_text_input != 0 ||
                       xui::ctx().active_slider_edit != 0;
  backend::PreviewItem(
      item.def_index, features::changer::MakeSkinConfiguration(editor.skin),
      item.category == ItemSystem::ItemCategory::kAgent, editing);

  auto& draw = xui::draw::current();
  const auto& input = xui::ctx().input;
  const bool hovered = input.in_rect(area) && !xui::ctx().overlay_blocking();
  if (hovered && input.mouse_clicked) editor.rotating = true;
  if (!input.mouse_down) editor.rotating = false;
  if (editor.rotating) {
    editor.yaw += input.mouse_delta_x() * 0.5f;
    editor.pitch =
        std::clamp(editor.pitch + input.mouse_delta_y() * 0.5f, -80.0f, 80.0f);
    systems::model_preview::SetRotation(editor.yaw, editor.pitch);
  }
  if (hovered)
    editor.zoom =
        std::clamp(editor.zoom + input.scroll_delta * 0.1f, 0.5f, 3.0f);
  if (hovered && input.mouse_double_clicked) {
    editor.yaw = editor.pitch = 0.0f;
    editor.zoom = 1.0f;
    systems::model_preview::SetRotation(0.0f, 0.0f);
  }

  draw.rect_filled(area.x, area.y, area.w, area.h, tokens::col_card,
                   xdraw::corner_radius{6.0f});
  const auto frame = systems::model_preview::GetFrame();
  if (frame.valid && frame.texture && frame.width && frame.height) {
    const float scale =
        std::min(area.w / frame.width, area.h / frame.height) * editor.zoom;
    const float width = frame.width * scale;
    const float height = frame.height * scale;
    draw.push_clip(area.x, area.y, area.w, area.h);
    draw.image(area.x + (area.w - width) * 0.5f,
               area.y + (area.h - height) * 0.5f, width, height, frame.texture);
    draw.pop_clip();
  } else {
    const auto status =
        xui::truncate(systems::model_preview::GetStatusText(), area.w - 16.0f);
    draw.text(area.x + 8.0f, area.y + area.h * 0.5f, status,
              tokens::col_text_dim);
  }
  draw.text(area.x + 8.0f, area.bottom() - 20.0f,
            "drag to rotate / scroll to zoom", tokens::col_text_dim);
}
}  // namespace

float Menu::DrawSkinEditor(float x, float y, float width, float height,
                           std::int16_t browsing_definition) const {
  constexpr float kHeaderHeight = 32.0f;
  xui::layout::set_cursor(x - m_x_, y - m_y_);
  if (xui::button(editor.expanded ? "- skin options & preview"
                                  : "+ skin options & preview",
                  width * 0.5f, 24.0f))
    editor.expanded = !editor.expanded;
  xui::layout::same_line();
  xui::checkbox("enabled##skinchanger", settings::g_changer.enabled);
  if (!editor.expanded) {
    backend::ClosePreview();
    return kHeaderHeight;
  }

  const float panel_height = std::min(210.0f, height * 0.52f);
  const float column_width = (width - tokens::gap) * 0.5f;
  const auto& items = ItemsForTab(m_subtab_);
  if (items.empty()) {
    backend::ClosePreview();
    return kHeaderHeight;
  }

  if (editor.last_subtab != m_subtab_) {
    editor.item_index = 0;
    editor.section = 0;
    editor.last_subtab = m_subtab_;
    editor.last_browsing_definition = 0;
  }
  if (browsing_definition != editor.last_browsing_definition) {
    const auto found = std::ranges::find_if(items, [=](const auto* item) {
      return item->def_index == browsing_definition;
    });
    if (found != items.end())
      editor.item_index = static_cast<int>(found - items.begin());
    editor.last_browsing_definition = browsing_definition;
  }
  editor.item_index =
      std::clamp(editor.item_index, 0, static_cast<int>(items.size()) - 1);

  xui::layout::set_cursor(x - m_x_, y + kHeaderHeight - m_y_);
  if (xui::begin_child(std::format("##skin_editor_options_{}", m_subtab_),
                       column_width, panel_height, true)) {
    const ClippedClicks clip_input(xui::layout::current_window()->bounds);
    std::vector<const char*> names;
    for (const auto* item : items)
      names.push_back(item->localized_name.c_str());
    xui::combo("preview item", editor.item_index, names.data(),
               static_cast<int>(names.size()));
    const auto& item = *items[editor.item_index];
    const auto found = settings::g_changer.skins.data.find(item.def_index);
    const auto saved = found != settings::g_changer.skins.data.end()
                           ? found->second
                           : settings::Changer::AppliedSkin{};
    if (editor.definition != item.def_index || editor.saved_skin != saved) {
      editor.skin = editor.saved_skin = saved;
      if (editor.definition != item.def_index) {
        editor.definition = item.def_index;
        editor.yaw = editor.pitch = 0.0f;
        editor.zoom = 1.0f;
        editor.rotating = false;
        systems::model_preview::SetRotation(0.0f, 0.0f);
      }
    }
    DrawItemOptions(item);
    xui::end_child();
  }
  DrawPreview(*items[editor.item_index],
              {x + column_width + tokens::gap, y + kHeaderHeight, column_width,
               panel_height});
  return kHeaderHeight + panel_height + tokens::gap;
}
}  // namespace rendering
