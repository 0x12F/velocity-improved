#include "pch/pch.h"

#include "utilities/hooking/hooking.h"
#include "utilities/logging/logging.h"
#include "utilities/security/security.h"

namespace hooking::manager {

bool Create(const std::initializer_list<Entry>& entries) {
  for (const auto& entry : entries) {
    if (!entry.hook || !entry.detour) {
      logging::console::Print(xs("invalid hook entry: {}"), entry.name);
      return false;
    }

    if (!entry.address) {
      logging::console::Print(xs("failed to hook: {}"), entry.name);
      return false;
    }
  }

  const auto rollback = [&entries]() {
    for (const auto& entry : entries) {
      if (entry.hook) {
        entry.hook->Reset();
      }
    }
  };

  for (const auto& entry : entries) {
    if (!entry.hook->Create(reinterpret_cast<void*>(entry.address),
                            entry.detour)) {
      logging::console::Print(xs("failed to hook: {}"), entry.name);
      rollback();
      return false;
    }

    if (!entry.hook->Enable()) {
      logging::console::Print(xs("failed to enable hook: {}"), entry.name);
      rollback();
      return false;
    }

    security::prologues::Add(entry.address, entry.hook->GetOriginalBytes(),
                             entry.hook->GetOriginalLength());
  }

  return true;
}

}  // namespace hooking::manager
