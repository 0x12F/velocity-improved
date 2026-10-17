#ifndef VELOCITY_PCH_PCH_H_
#define VELOCITY_PCH_PCH_H_

// macros
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// windows
#ifndef PHNT_VERSION
#define PHNT_VERSION PHNT_THRESHOLD
#endif  // !PHNT_VERSION

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-enum-forward-reference"
#endif

#include <external/phnt/phnt_windows.h>

#include <external/phnt/phnt.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <external/inline-syscall/inline_syscall.hpp>

// types
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <variant>

// containers
#include <array>
#include <deque>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// memory
#include <memory>

// algorithms
#include <algorithm>
#include <numeric>
#include <ranges>

// concurrency
#include <atomic>
#include <mutex>
#include <shared_mutex>

// dependencies
#include <external/lz4/lz4.h>
#include <external/zydis/zydis.h>

#include <external/xdraw/xui/xui.hpp>

#define JM_XORSTR_DISABLE_AVX_INTRINSICS
#include <external/bc7.hpp>
#include <external/poly2d.hpp>
#include <external/xorstr.hpp>

// project utilities
#include "utilities/addresses/addresses.h"
#include "utilities/animation.h"
#include "utilities/cstypes.h"
#include "utilities/fnv1a.h"

// declarations
extern "C" int __stdcall _CRT_INIT(HMODULE, DWORD, LPVOID);

#endif  // VELOCITY_PCH_PCH_H_
