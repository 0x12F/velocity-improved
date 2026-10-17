#include "pch/pch.h"

#include "core/features/misc/misc.h"
#include "core/settings.h"
#include "utilities/addresses/addresses.h"
#include "utilities/memory/memory.h"

namespace features::misc {

void Removals::OnPrepareSceneMaterial(std::uintptr_t material) const {
  if (!settings::g_misc.m_removals.skybox_fog.value) {
    return;
  }

  const auto parameters = memory::Read<std::uintptr_t>(material + 0x20);
  if (!parameters) {
    return;
  }

  const auto count = memory::Read<std::uint32_t>(material + 0x18);

  for (auto i = 0u; i < count; i++) {
    const auto parameter = parameters + (static_cast<std::size_t>(i) * 0x40);
    const auto name_ptr = memory::Read<const char*>(parameter + 0x28);

    if (!name_ptr) {
      continue;
    }

    const auto param_hash = fnv1a::RuntimeHash(name_ptr);
    if (param_hash == "g_flBrightnessExposureBias"_hash ||
        param_hash == "g_flRenderOnlyExposureBias"_hash) {
      memory::Write<float>(parameter, -6.0f);
    }
  }
}

void Removals::OnOverrideView(std::uintptr_t view_setup) const {
  if (!settings::g_misc.m_removals.recoil.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.is_alive || systems::g_local.IsInCinematic() || local.team < 2) {
    return;
  }

  memory::Write<math::Vector3>(view_setup + 0x4b8,
                               systems::g_input.GetViewAngles());
}

}  // namespace features::misc