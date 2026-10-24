module;

#include "platform/platform.h"

module velocity;

import <cstddef>;
import <string_view>;

namespace logging::console {

bool Initialize() {
  // note: this used to resolve tier0's LoggingSystem_Log by hardcoded
  // ordinal, which is stale on current cs2 builds and crashed the game
  // whenever anything was printed. Logging now goes to the structured
  // diagnostics file next to the DLL and the debugger output instead.
  return true;
}

void PrintRaw(const char* text) {
  if (!text) {
    return;
  }

  const bool was_emitting = emitting;
  emitting = true;
  diag::Write(diag::Level::kInfo, text);
  emitting = was_emitting;
}

}  // namespace logging::console
