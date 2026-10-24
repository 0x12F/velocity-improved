module;

#include "platform/platform.h"

export module velocity:utilities.logging.logging;

import <format>;
import <string_view>;

export {
namespace logging {

namespace console {

bool Initialize();

void PrintRaw(const char* text);

template <typename... ArgsT>
void Print(std::string_view fmt, ArgsT&&... args) {
  PrintRaw(std::vformat(fmt, std::make_format_args(args...)).c_str());
}

inline thread_local bool emitting{};

}  // namespace console

namespace popup {

bool Initialize();
void Show(const char* title, const char* message);

}  // namespace popup

}  // namespace logging
}  // export
