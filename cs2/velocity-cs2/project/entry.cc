module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <cstdio>;
import <filesystem>;
import <initializer_list>;
import <limits>;
import <string_view>;
import <thread>;
import <type_traits>;

namespace {

std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_previous_exception_filter{};
PVOID g_vectored_exception_handler{};

LONG WINAPI DiagUnhandledExceptionFilter(EXCEPTION_POINTERS* info);

#if defined(DEV)
hooking::Jmp g_minidump_hook{};
hooking::Jmp g_terminate_process_hook{};

BOOL WINAPI DiagMinidumpWriteDetour(
    HANDLE process, DWORD process_id, HANDLE file, unsigned long dump_type,
    diag::MinidumpExceptionInformation* exception, void* user_stream,
    void* callback) {
  const auto original = g_minidump_hook.Original<diag::MinidumpWriteFn>();
  if (!diag::g_writing_minidump && exception && !exception->client_pointers &&
      exception->exception_pointers) {
    diag::RecordCrash(exception->exception_pointers,
                      "game fatal handler (Source 2 caught the exception)");
  }

  return original ? original(process, process_id, file, dump_type, exception,
                             user_stream, callback)
                  : FALSE;
}

bool InstallGameCrashCapture() {
  if (!diag::g_minidump_write) {
    return false;
  }

  if (!hooking::manager::Create(
          {{&g_minidump_hook, reinterpret_cast<void*>(DiagMinidumpWriteDetour),
            "dbghelp!MiniDumpWriteDump",
            reinterpret_cast<std::uintptr_t>(diag::g_minidump_write)}})) {
    diag::Write(diag::Level::kWarning,
                "failed to hook the Source 2 minidump path; "
                "only self/unhandled crashes will be captured");
    return false;
  }

  diag::Write(diag::Level::kInfo, "Source 2 fatal minidump path hooked");
  return true;
}

BOOL WINAPI DiagTerminateProcessDetour(HANDLE process, UINT exit_code) {
  const auto original =
      g_terminate_process_hook.Original<decltype(&TerminateProcess)>();

  if (GetProcessId(process) == GetCurrentProcessId()) {
    char stage[96]{};
    _snprintf_s(stage, sizeof(stage), _TRUNCATE,
                "TerminateProcess requested for CS2; exit_code=0x%08X",
                exit_code);
    diag::CaptureSnapshot(stage);
  }

  return original ? original(process, exit_code) : FALSE;
}

bool InstallTerminationCapture() {
  const auto kernel32 = GetModuleHandleW(L"kernel32.dll");
  const auto terminate_process =
      kernel32 ? GetProcAddress(kernel32, "TerminateProcess") : nullptr;
  if (!terminate_process ||
      !hooking::manager::Create(
          {{&g_terminate_process_hook,
            reinterpret_cast<void*>(DiagTerminateProcessDetour),
            "kernel32!TerminateProcess",
            reinterpret_cast<std::uintptr_t>(terminate_process)}})) {
    diag::Write(diag::Level::kWarning,
                "failed to hook forced process termination");
    return false;
  }

  diag::Write(diag::Level::kInfo, "forced process termination capture hooked");
  return true;
}
#endif

LONG CALLBACK DiagVectoredExceptionFilter(EXCEPTION_POINTERS* info) {
  if (!info || !info->ExceptionRecord ||
      !diag::IsSeriousException(info->ExceptionRecord->ExceptionCode)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (diag::ProbeActive()) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // The host or Steam may replace the single process-wide last-chance
  // filter after injection. Re-arm it at first chance and preserve the
  // displaced handler so the host still receives the crash after us.
  const auto displaced_filter =
      SetUnhandledExceptionFilter(DiagUnhandledExceptionFilter);
  if (displaced_filter != DiagUnhandledExceptionFilter) {
    g_previous_exception_filter.store(displaced_filter,
                                      std::memory_order_release);
  }

  if (diag::IsModuleAddress(info->ExceptionRecord->ExceptionAddress)) {
    diag::RecordCrash(info, diag::g_exception_scope_depth
                                ? diag::g_exception_phase
                                : "first-chance fault in velocity DLL");
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (diag::g_exception_scope_depth == 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const auto code = info->ExceptionRecord->ExceptionCode;
  const auto instruction =
      reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
  const auto accessed = info->ExceptionRecord->NumberParameters > 1
                            ? info->ExceptionRecord->ExceptionInformation[1]
                            : 0;

  HMODULE fault_module{};
  char module_path[MAX_PATH]{"unknown"};
  std::uintptr_t module_base{};
  if (instruction &&
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(instruction),
                         &fault_module)) {
    module_base = reinterpret_cast<std::uintptr_t>(fault_module);
    GetModuleFileNameA(fault_module, module_path, MAX_PATH);
  }

  const auto* module_name = strrchr(module_path, '\\');
  module_name = module_name ? module_name + 1 : module_path;

  char buf[384]{};
  _snprintf_s(
      buf, sizeof(buf), _TRUNCATE,
      "FEATURE EXCEPTION [%s] 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
      diag::g_exception_phase, code, module_name,
      module_base ? static_cast<unsigned long long>(instruction - module_base)
                  : 0ull,
      info->ExceptionRecord->ExceptionAddress,
      reinterpret_cast<void*>(accessed));
  diag::Step(buf);

  return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI DiagUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  if (!info || !info->ExceptionRecord) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const auto instruction =
      reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
  const auto accessed = info->ExceptionRecord->NumberParameters > 1
                            ? info->ExceptionRecord->ExceptionInformation[1]
                            : 0;

  HMODULE fault_module{};
  char module_path[MAX_PATH]{"unknown"};
  std::uintptr_t module_base{};
  if (instruction &&
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(instruction),
                         &fault_module)) {
    module_base = reinterpret_cast<std::uintptr_t>(fault_module);
    GetModuleFileNameA(fault_module, module_path, MAX_PATH);
  }

  const auto* module_name = strrchr(module_path, '\\');
  module_name = module_name ? module_name + 1 : module_path;

  char buf[384]{};
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "UNHANDLED EXCEPTION 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
              info->ExceptionRecord->ExceptionCode, module_name,
              module_base
                  ? static_cast<unsigned long long>(instruction - module_base)
                  : 0ull,
              info->ExceptionRecord->ExceptionAddress,
              reinterpret_cast<void*>(accessed));
  diag::Write(diag::Level::kFatal, buf);
  diag::RecordCrash(info, "unhandled exception");

  const auto previous_filter =
      g_previous_exception_filter.load(std::memory_order_acquire);
  if (previous_filter && previous_filter != DiagUnhandledExceptionFilter) {
    return previous_filter(info);
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(DEV)
#define INIT_FAIL(msg)                     \
  do {                                     \
    diag::Write(diag::Level::kError, msg); \
    return 0;                              \
  } while (0)

#define INIT_WARN(msg) diag::Write(diag::Level::kWarning, msg)
#else
#define INIT_FAIL(msg)                                      \
  do {                                                      \
    diag::Write(diag::Level::kError, msg);                  \
    MessageBoxA(nullptr, xs(msg), xs("..."), MB_ICONERROR); \
    return 0;                                               \
  } while (0)

#define INIT_WARN(msg) INIT_FAIL(msg)
#endif

DWORD WINAPI InitThreadImpl(LPVOID param) {
  const auto module_handle = static_cast<HMODULE>(param);

  diag::Step("stage: thread start");
  diag::InitializeCrashDumps();

  g_previous_exception_filter.store(
      SetUnhandledExceptionFilter(DiagUnhandledExceptionFilter),
      std::memory_order_release);
  g_vectored_exception_handler =
      AddVectoredExceptionHandler(1, DiagVectoredExceptionFilter);
  if (!g_vectored_exception_handler) {
    diag::Writef(
        diag::Level::kError,
        "failed to install vectored exception handler; win32_error=%lu",
        GetLastError());
  } else {
    diag::Write(diag::Level::kInfo, "crash handlers installed");
  }

  diag::Step("stage: coinit");
  const auto coinit_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(coinit_result)) {
    diag::Writef(diag::Level::kWarning,
                 "CoInitializeEx failed; hresult=0x%08lX", coinit_result);
  }

  diag::Step("stage: config");
  config::initialize();
  settings::FinalizeBinds();

  diag::Step("stage: regions");
  security::regions::AddModule(module_handle);

  diag::Step("stage: logging");
  {
    if (!logging::console::Initialize()) {
#if defined(DEV)
      INIT_WARN("failed to initialize console logging.");
#else
      INIT_FAIL("failed to initialize console logging.");
#endif
    }

    if (!logging::popup::Initialize()) {
#if defined(DEV)
      INIT_WARN("failed to initialize popup logging.");
#else
      INIT_FAIL("failed to initialize popup logging.");
#endif
    }
  }

  diag::Step("stage: integrity");
  {
    if (!security::integrity::Initialize()) {
      INIT_FAIL("failed to initialize integrity checks.");
    }

#if defined(DEV)
    InstallGameCrashCapture();
    InstallTerminationCapture();
#endif

    if (!threadpool::Initialize()) {
      INIT_FAIL("failed to initialize thread pool.");
    }

    if (!steam::Http::Initialize()) {
      INIT_FAIL("failed to initialize steam http.");
    }

    discord_webhook::SendTestMessage();

    if (!steam::Friends::Initialize()) {
      INIT_FAIL("failed to initialize steam friends.");
    }

    if (!steam::User::Initialize()) {
      INIT_FAIL("failed to initialize steam user.");
    }

    if (!steam::Utils::Initialize()) {
      INIT_FAIL("failed to initialize steam utils.");
    }
  }

  diag::Step("stage: addresses");
  {
    if (!addresses::modules::Initialize()) {
      INIT_FAIL("failed to initialize module addresses.");
    }

    if (!addresses::globals::Initialize()) {
      INIT_FAIL("failed to initialize global addresses.");
    }

    if (!addresses::functions::Initialize()) {
      INIT_FAIL("failed to initialize function addresses.");
    }
  }

  diag::Step("stage: systems");
  {
    if (!systems::Materials::Initialize()) {
      INIT_FAIL("failed to initialize materials system.");
    }

    if (!systems::Events::Initialize()) {
      INIT_FAIL("failed to initialize event system.");
    }

    if (!systems::g_icons.Initialize()) {
      INIT_FAIL("failed to initialize vpk parse system.");
    }
  }

  diag::Step("stage: econ");
  {
    if (!features::changer::g_econ_item_system.Initialize()) {
      INIT_FAIL("failed to initialize econ item system.");
    }
  }

  if (!features::changer::runtime::Initialize()) {
    INIT_FAIL("failed to initialize skinchanger.");
  }

  diag::Step("stage: hooks");
  {
    if (!hooks::Utility::Initialize()) {
      INIT_FAIL("failed to initialize utility hooks.");
    }

    if (!hooks::Cheat::Initialize()) {
      INIT_FAIL("failed to initialize cheat hooks.");
    }
  }

  diag::Step("stage: cvars");
  {
    if (!addresses::globals::cvar->UnlockAll()) {
      INIT_FAIL("failed to unlock hidden cvars.");
    }
  }

  diag::Step("stage: skyboxes");
  features::world::g_scene.DiscoverSkyboxes();

  diag::Step("stage: done");
  return 1;
}

DWORD DiagExceptionFilter(EXCEPTION_POINTERS* info) {
  char buf[128]{};
  _snprintf_s(buf, sizeof(buf), _TRUNCATE, "EXCEPTION 0x%08lX at 0x%p",
              info->ExceptionRecord->ExceptionCode,
              info->ExceptionRecord->ExceptionAddress);
  diag::Write(diag::Level::kFatal, buf);
  diag::RecordCrash(info, "initialization thread");
  return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI InitThread(LPVOID param) {
  __try {
    return InitThreadImpl(param);
  } __except (DiagExceptionFilter(GetExceptionInformation())) {
    return 0;
  }
}

}  // namespace

extern "C" int __stdcall Entry(HMODULE module_handle, DWORD reason,
                               LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    _CRT_INIT(module_handle, reason, reserved);
    DisableThreadLibraryCalls(module_handle);

    diag::SetModule(module_handle);
    diag::Step("stage: dll attach");
    diag::Step("build: development diagnostics");

    diag::Step("stage: crt done, spawning thread");

    const auto thread =
        CreateThread(nullptr, 0, InitThread, module_handle, 0, nullptr);
    if (!thread) {
      diag::Writef(diag::Level::kError,
                   "failed to create initialization thread; win32_error=%lu",
                   GetLastError());
      return 0;
    }

    CloseHandle(thread);
    return 1;
  } else if (reason == DLL_PROCESS_DETACH) {
#if defined(DEV)
    if (g_vectored_exception_handler) {
      RemoveVectoredExceptionHandler(g_vectored_exception_handler);
      g_vectored_exception_handler = nullptr;
    }

    const auto previous_filter = g_previous_exception_filter.exchange(
        nullptr, std::memory_order_acq_rel);
    const auto current_filter = SetUnhandledExceptionFilter(previous_filter);
    if (current_filter != DiagUnhandledExceptionFilter) {
      SetUnhandledExceptionFilter(current_filter);
    }

    g_terminate_process_hook.Reset();
    g_minidump_hook.Reset();

    features::esp::player::g_chams.Bt().Shutdown();
    features::esp::player::g_chams.Os().Shutdown();

    features::world::g_weather.Release();
    rendering::g_menu.Shutdown();

    systems::Events::Shutdown();
    hooks::Utility::Shutdown();
    hooks::Cheat::Shutdown();
    features::changer::runtime::Shutdown();
    CoUninitialize();
#endif

    diag::Shutdown();

#if defined(DEV)
    _CRT_INIT(module_handle, reason, reserved);
#endif
  }

  return 1;
}
