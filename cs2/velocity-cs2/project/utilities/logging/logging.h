#ifndef VELOCITY_UTILITIES_LOGGING_LOGGING_H_
#define VELOCITY_UTILITIES_LOGGING_LOGGING_H_

#include <format>
#include <string_view>

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

#endif  // VELOCITY_UTILITIES_LOGGING_LOGGING_H_
