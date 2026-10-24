#ifndef VELOCITY_PLATFORM_PLATFORM_H_
#define VELOCITY_PLATFORM_PLATFORM_H_

// macros
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Standard header units precede Windows and third-party declarations.
import <typeinfo>;
import <algorithm>;
import <array>;
import <atomic>;
import <bit>;
import <cctype>;
import <charconv>;
import <chrono>;
import <cmath>;
import <cstddef>;
import <cstdint>;
import <cstdio>;
import <cstring>;
import <deque>;
import <filesystem>;
import <format>;
import <fstream>;
import <functional>;
import <initializer_list>;
import <limits>;
import <memory>;
import <mutex>;
import <numbers>;
import <numeric>;
import <optional>;
import <random>;
import <ranges>;
import <shared_mutex>;
import <span>;
import <string>;
import <string_view>;
import <system_error>;
import <thread>;
import <type_traits>;
import <unordered_map>;
import <unordered_set>;
import <utility>;
import <variant>;
import <vector>;

// windows
#ifndef PHNT_VERSION
#define PHNT_VERSION PHNT_THRESHOLD
#endif  // !PHNT_VERSION

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-enum-forward-reference"
#pragma clang diagnostic ignored "-Wpragma-pack"
#endif

#include <external/phnt/phnt_windows.h>

#include <external/phnt/phnt.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// dependencies
#include <external/lz4/lz4.h>
#include <external/zydis/zydis.h>

#include <external/xdraw/xui/xui.hpp>

#define JM_XORSTR_DISABLE_AVX_INTRINSICS
#include <external/bc7.hpp>
#include <external/poly2d.hpp>
#include <external/xorstr.hpp>

// declarations
extern "C" int __stdcall _CRT_INIT(HMODULE, DWORD, LPVOID);

#endif  // VELOCITY_PLATFORM_PLATFORM_H_
