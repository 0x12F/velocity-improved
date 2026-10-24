module;

#include "platform/platform.h"

export module velocity:utilities.cstypes;

import <cmath>;
import <cstdint>;

export {
namespace cstypes {

constexpr auto kTickInterval{0.015625f};

[[nodiscard]] inline int TimeToTicks(float time) noexcept {
  return static_cast<int>(0.5f + time / cstypes::kTickInterval);
}
[[nodiscard]] inline float TicksToTime(int ticks) noexcept {
  return cstypes::kTickInterval * static_cast<float>(ticks);
}

namespace bone_ids {

constexpr auto kHead{7u};
constexpr auto kNeck{6u};
constexpr auto kSpine1{2u};
constexpr auto kSpine2{3u};
constexpr auto kSpine3{4u};
constexpr auto kSpine4{23u};
constexpr auto kPelvis{1u};

constexpr auto kLeftClavicle{8u};
constexpr auto kLeftShoulder{9u};
constexpr auto kLeftElbow{10u};
constexpr auto kLeftHand{11u};

constexpr auto kRightClavicle{12u};
constexpr auto kRightShoulder{13u};
constexpr auto kRightElbow{14u};
constexpr auto kRightHand{15u};

constexpr auto kLeftHip{17u};
constexpr auto kLeftKnee{18u};
constexpr auto kLeftFoot{19u};

constexpr auto kRightHip{20u};
constexpr auto kRightKnee{21u};
constexpr auto kRightFoot{22u};

}  // namespace bone_ids

namespace hit_groups {

constexpr auto kGeneric{0u};
constexpr auto kHead{1u};
constexpr auto kChest{2u};
constexpr auto kStomach{3u};
constexpr auto kLeftarm{4u};
constexpr auto kRightarm{5u};
constexpr auto kLeftleg{6u};
constexpr auto kRightleg{7u};
constexpr auto kNeck{8u};
constexpr auto kGear{10u};

}  // namespace hit_groups

namespace weapon_type {

constexpr auto kNife{0u};
constexpr auto kPistol{1u};
constexpr auto kSmg{2u};
constexpr auto kRifle{3u};
constexpr auto kShotgun{4u};
constexpr auto kSniper{5u};
constexpr auto kLmg{6u};
constexpr auto kC4{7u};
constexpr auto kTaser{8u};
constexpr auto kGrenade{9u};
constexpr auto kEquipment{10u};
constexpr auto kHealthshot{11u};

}  // namespace weapon_type

namespace command_buttons {

constexpr auto kInAttack{1ull << 0};
constexpr auto kInJump{1ull << 1};
constexpr auto kInDuck{1ull << 2};
constexpr auto kInForward{1ull << 3};
constexpr auto kInBack{1ull << 4};
constexpr auto kInUse{1ull << 5};
constexpr auto kInLeft{1ull << 7};
constexpr auto kInRight{1ull << 8};
constexpr auto kInMoveleft{1ull << 9};
constexpr auto kInMoveright{1ull << 10};
constexpr auto kInSecondAttack{1ull << 11};
constexpr auto kInReload{1ull << 13};
constexpr auto kInSprint{1ull << 16};
constexpr auto kInJoyautosprint{1ull << 17};
constexpr auto kInShowscores{1ull << 33};
constexpr auto kInZoom{1ull << 34};
constexpr auto kInLookatweapon{1ull << 35};

}  // namespace command_buttons

namespace move_type {

constexpr std::uint8_t kNone{0};
constexpr std::uint8_t kObsolete{1};
constexpr std::uint8_t kWalk{2};
constexpr std::uint8_t kFly{3};
constexpr std::uint8_t kFlyGravity{4};
constexpr std::uint8_t kVphysics{5};
constexpr std::uint8_t kPush{6};
constexpr std::uint8_t kNoclip{7};
constexpr std::uint8_t kObserver{8};
constexpr std::uint8_t kLadder{9};
constexpr std::uint8_t kCustom{10};
constexpr std::uint8_t kLast{11};

}  // namespace move_type

namespace entity_flags {

constexpr auto kOnGround{1u << 0};
constexpr auto kDucking{1u << 1};
constexpr auto kWaterJump{1u << 3};
constexpr auto kOnTrain{1u << 4};
constexpr auto kInRain{1u << 5};
constexpr auto kFrozen{1u << 6};
constexpr auto kAtControls{1u << 7};
constexpr auto kClient{1u << 8};
constexpr auto kFakeClient{1u << 9};
constexpr auto kInWater{1u << 10};
constexpr auto kHideHudScope{1u << 11};

}  // namespace entity_flags

namespace item_definition_index {

constexpr std::uint16_t kWeaponNone{0};
constexpr std::uint16_t kWeaponDesertEagle{1};
constexpr std::uint16_t kWeaponDualBerettas{2};
constexpr std::uint16_t kWeaponFiveSeven{3};
constexpr std::uint16_t kWeaponGlock18{4};
constexpr std::uint16_t kWeaponAk47{7};
constexpr std::uint16_t kWeaponAug{8};
constexpr std::uint16_t kWeaponAwp{9};
constexpr std::uint16_t kWeaponFamas{10};
constexpr std::uint16_t kWeaponG3sg1{11};
constexpr std::uint16_t kWeaponGalilAr{13};
constexpr std::uint16_t kWeaponM249{14};
constexpr std::uint16_t kWeaponM4a4{16};
constexpr std::uint16_t kWeaponMac10{17};
constexpr std::uint16_t kWeaponP90{19};
constexpr std::uint16_t kWeaponRepulsorDevice{20};
constexpr std::uint16_t kWeaponMp5Sd{23};
constexpr std::uint16_t kWeaponUmp45{24};
constexpr std::uint16_t kWeaponXm1014{25};
constexpr std::uint16_t kWeaponPpBizon{26};
constexpr std::uint16_t kWeaponMag7{27};
constexpr std::uint16_t kWeaponNegev{28};
constexpr std::uint16_t kWeaponSawedOff{29};
constexpr std::uint16_t kWeaponTec9{30};
constexpr std::uint16_t kWeaponZeusX27{31};
constexpr std::uint16_t kWeaponP2000{32};
constexpr std::uint16_t kWeaponMp7{33};
constexpr std::uint16_t kWeaponMp9{34};
constexpr std::uint16_t kWeaponNova{35};
constexpr std::uint16_t kWeaponP250{36};
constexpr std::uint16_t kWeaponRiotShield{37};
constexpr std::uint16_t kWeaponScar20{38};
constexpr std::uint16_t kWeaponSg553{39};
constexpr std::uint16_t kWeaponSsg08{40};
constexpr std::uint16_t kWeaponKnife0{41};
constexpr std::uint16_t kWeaponKnife1{42};
constexpr std::uint16_t kWeaponFlashbang{43};
constexpr std::uint16_t kWeaponHighExplosiveGrenade{44};
constexpr std::uint16_t kWeaponSmokeGrenade{45};
constexpr std::uint16_t kWeaponMolotov{46};
constexpr std::uint16_t kWeaponDecoyGrenade{47};
constexpr std::uint16_t kWeaponIncendiaryGrenade{48};
constexpr std::uint16_t kWeaponC4Explosive{49};
constexpr std::uint16_t kWeaponKevlarVest{50};
constexpr std::uint16_t kWeaponKevlarAndHelmet{51};
constexpr std::uint16_t kWeaponHeavyAssaultSuit{52};
constexpr std::uint16_t kWeaponNoLocalizedName0{54};
constexpr std::uint16_t kWeaponDefuseKit{55};
constexpr std::uint16_t kWeaponRescueKit{56};
constexpr std::uint16_t kWeaponMediShot{57};
constexpr std::uint16_t kWeaponMusicKit{58};
constexpr std::uint16_t kWeaponKnife2{59};
constexpr std::uint16_t kWeaponM4a1S{60};
constexpr std::uint16_t kWeaponUspS{61};
constexpr std::uint16_t kWeaponTradeUpContract{62};
constexpr std::uint16_t kWeaponCz75Auto{63};
constexpr std::uint16_t kWeaponR8Revolver{64};
constexpr std::uint16_t kWeaponTacticalAwarenessGrenade{68};
constexpr std::uint16_t kWeaponBareHands{69};
constexpr std::uint16_t kWeaponBreachCharge{70};
constexpr std::uint16_t kWeaponTablet{72};
constexpr std::uint16_t kWeaponKnife3{74};
constexpr std::uint16_t kWeaponAxe{75};
constexpr std::uint16_t kWeaponHammer{76};
constexpr std::uint16_t kWeaponWrench{78};
constexpr std::uint16_t kWeaponSpectralShiv{80};
constexpr std::uint16_t kWeaponFireBomb{81};
constexpr std::uint16_t kWeaponDiversionDevice{82};
constexpr std::uint16_t kWeaponFragGrenade{83};
constexpr std::uint16_t kWeaponSnowball{84};
constexpr std::uint16_t kWeaponBumpMine{85};

constexpr std::uint16_t kWeaponBayonet{500};
constexpr std::uint16_t kWeaponClassicKnife{503};
constexpr std::uint16_t kWeaponFlipKnife{505};
constexpr std::uint16_t kWeaponGutKnife{506};
constexpr std::uint16_t kWeaponKarambit{507};
constexpr std::uint16_t kWeaponM9Bayonet{508};
constexpr std::uint16_t kWeaponHuntsmanKnife{509};
constexpr std::uint16_t kWeaponFalchionKnife{512};
constexpr std::uint16_t kWeaponBowieKnife{514};
constexpr std::uint16_t kWeaponButterflyKnife{515};
constexpr std::uint16_t kWeaponShadowDaggers{516};
constexpr std::uint16_t kWeaponParacordKnife{517};
constexpr std::uint16_t kWeaponSurvivalKnife{518};
constexpr std::uint16_t kWeaponUrsusKnife{519};
constexpr std::uint16_t kWeaponNavajaKnife{520};
constexpr std::uint16_t kWeaponNomadKnife{521};
constexpr std::uint16_t kWeaponStilettoKnife{522};
constexpr std::uint16_t kWeaponTalonKnife{523};
constexpr std::uint16_t kWeaponSkeletonKnife{525};

}  // namespace item_definition_index

struct TickFraction {
  int tick{};
  float frac{};

  static TickFraction FromValue(float value) {
    auto integer_part{0.0f};
    auto fractional = std::modff(value, &integer_part);

    if (fractional < 0.0f) {
      fractional += 1.0f;

      if (fractional < 1.0f) {
        integer_part -= 1.0f;
      } else {
        fractional = 0.0f;
      }
    }

    TickFraction result{};
    result.tick =
        (integer_part >= -2147483600.0f && integer_part < 2147483600.0f)
            ? static_cast<int>(integer_part)
            : 0;
    result.frac = fractional;

    if (result.frac < 0.0f || result.frac >= 1.0f) {
      result.Normalize();
    }

    return result;
  }

  TickFraction SubtractValue(float value) const {
    auto offset = FromValue(value);
    return this->Subtract(offset);
  }

  void Normalize() {
    auto integer_part{0.0f};
    auto fractional = std::modff(frac, &integer_part);

    if (fractional < 0.0f) {
      fractional += 1.0f;

      if (fractional < 1.0f) {
        integer_part -= 1.0f;
      } else {
        fractional = 0.0f;
      }
    }

    auto extra =
        (integer_part >= -2147483600.0f && integer_part < 2147483600.0f)
            ? static_cast<int>(integer_part)
            : 0;

    if (fractional < 0.0f || fractional >= 1.0f) {
      TickFraction inner{};
      inner.NormalizeRaw(fractional, extra);
      this->tick += inner.tick;
      this->frac = inner.frac;
    } else {
      this->tick += extra;
      this->frac = fractional;
    }
  }

  TickFraction Subtract(const TickFraction& other) const {
    TickFraction result{};
    result.frac = this->frac - other.frac;
    result.tick = this->tick - other.tick;

    if (result.frac < 0.0f) {
      result.frac += 1.0f;

      if (result.frac < 1.0f) {
        result.tick--;
      } else {
        result.frac = 0.0f;
      }
    }

    if (result.frac < 0.0f || result.frac >= 1.0f) {
      result.Normalize();
    }

    return result;
  }

 private:
  void NormalizeRaw(float f, int& extra_tick) {
    auto integer_part{0.0f};
    auto fractional = std::modff(f, &integer_part);

    if (fractional < 0.0f) {
      fractional += 1.0f;

      if (fractional < 1.0f) {
        integer_part -= 1.0f;
      } else {
        fractional = 0.0f;
      }
    }

    const auto extra =
        (integer_part >= -2147483600.0f && integer_part < 2147483600.0f)
            ? static_cast<int>(integer_part)
            : 0;
    this->tick += extra;
    this->frac = fractional;
  }
};

struct StrongHandle {
  const void* binding;
};

struct KeyValues3 {
  std::uint64_t metadata;
  std::uint64_t payload;
};
static_assert(sizeof(KeyValues3) == 0x10);

struct Kv3Id {
  const char* format;
  std::uintptr_t guid_low;
  std::uintptr_t guid_high;
};

struct EventHash {
  std::uintptr_t hash;
  const char* str;
};

}  // namespace cstypes
}  // export
