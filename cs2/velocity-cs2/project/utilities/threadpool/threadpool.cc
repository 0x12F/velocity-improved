module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace threadpool {

namespace detail {

namespace offsets {

constexpr std::size_t kRefcount{0x08};
constexpr std::size_t kStatus{0x10};
constexpr std::size_t kFlagsWord{0x14};
constexpr std::size_t kPriority{0x15};
constexpr std::size_t kNameSetFlag{0x16};
constexpr std::size_t kPoolOwner{0x18};
constexpr std::size_t kCompletionEvent{0x20};
constexpr std::size_t kCompletionCounter{0x28};
constexpr std::size_t kNameBuffer{0x30};
constexpr std::size_t kSboBuffer{0x50};
constexpr std::size_t kCallableImpl{0x88};

}  // namespace offsets

constexpr std::size_t kJobAllocSize{144};
constexpr std::size_t kNameBufferLen{31};
constexpr std::size_t kSboBufferSize{56};
constexpr std::size_t kVtiAddJob{16};

inline std::uintptr_t std_function_job_vtable{0};
inline Pool g_pool{};

}  // namespace detail

Job::Job(std::uintptr_t ptr, bool add_reference) : m_ptr_(ptr) {
  if (this->m_ptr_ && add_reference) {
    this->AddRef();
  }
}

Job::~Job() {
  if (this->m_ptr_) {
    this->DoRelease();
  }
}

Job::Job(const Job& o) : m_ptr_(o.m_ptr_) {
  if (this->m_ptr_) {
    this->AddRef();
  }
}

Job& Job::operator=(const Job& o) {
  if (this != &o) {
    if (this->m_ptr_) {
      this->DoRelease();
    }

    this->m_ptr_ = o.m_ptr_;

    if (this->m_ptr_) {
      this->AddRef();
    }
  }

  return *this;
}

Job::Job(Job&& o) noexcept : m_ptr_(o.m_ptr_) { o.m_ptr_ = 0; }

Job& Job::operator=(Job&& o) noexcept {
  if (this != &o) {
    if (this->m_ptr_) {
      this->DoRelease();
    }

    this->m_ptr_ = o.m_ptr_;
    o.m_ptr_ = 0;
  }

  return *this;
}

JobStatus Job::Status() const {
  return static_cast<JobStatus>(*reinterpret_cast<volatile int*>(
      this->m_ptr_ + detail::offsets::kStatus));
}

bool Job::Complete() const { return this->Status() == JobStatus::kIdle; }

void Job::Wait() const {
  if (!this->m_ptr_) {
    return;
  }

  auto event_pp = *reinterpret_cast<HANDLE**>(
      this->m_ptr_ + detail::offsets::kCompletionEvent);
  if (event_pp && *event_pp) {
    WaitForSingleObject(*event_pp, INFINITE);
    return;
  }

  auto counter = *reinterpret_cast<volatile int**>(
      this->m_ptr_ + detail::offsets::kCompletionCounter);
  if (counter) {
    while (this->Status() != JobStatus::kIdle) {
      auto snapshot{*counter};
      WaitOnAddress((volatile void*)counter, &snapshot, sizeof(int), 1);
    }

    return;
  }

  while (!this->Complete()) {
    _mm_pause();
  }
}

void Job::AddRef() const { memory::CallVfunc<long>(this->m_ptr_, 1); }

void Job::DoRelease() const { memory::CallVfunc<long>(this->m_ptr_, 2); }

void Pool::AddJob(std::uintptr_t j) const {
  memory::CallVfunc<void>(this->m_instance_, detail::kVtiAddJob, j);
}

std::uintptr_t MakeJob(std::function<void()>&& func, JobPriority priority,
                       const char* debug_name) {
  auto raw = reinterpret_cast<std::uintptr_t>(
      new std::uint8_t[detail::kJobAllocSize]());
  if (!raw) {
    return 0;
  }

  *reinterpret_cast<std::uintptr_t*>(raw) = detail::std_function_job_vtable;
  *reinterpret_cast<std::int32_t*>(raw + detail::offsets::kRefcount) = 1;
  *reinterpret_cast<volatile int*>(raw + detail::offsets::kStatus) =
      static_cast<int>(JobStatus::kReset);
  *reinterpret_cast<std::uint8_t*>(raw + detail::offsets::kPriority) =
      static_cast<std::uint8_t>(priority);

  auto dest_sbo = reinterpret_cast<void*>(raw + detail::offsets::kSboBuffer);
  auto dest_impl =
      reinterpret_cast<std::uintptr_t*>(raw + detail::offsets::kCallableImpl);

  alignas(16) std::uint8_t temp[sizeof(std::function<void()>)]{};
  auto fn = new (temp) std::function<void()>(std::move(func));

  auto src_sbo = reinterpret_cast<std::uint8_t*>(fn);
  auto src_impl =
      *reinterpret_cast<std::uintptr_t*>(src_sbo + detail::kSboBufferSize);

  if (src_impl) {
    auto src_addr = reinterpret_cast<std::uintptr_t>(src_sbo);
    auto is_sbo =
        (src_impl >= src_addr && src_impl < src_addr + detail::kSboBufferSize);

    std::memcpy(dest_sbo, src_sbo, detail::kSboBufferSize);

    if (is_sbo) {
      *dest_impl =
          reinterpret_cast<std::uintptr_t>(dest_sbo) + (src_impl - src_addr);
    } else {
      *dest_impl = src_impl;
    }

    *reinterpret_cast<std::uintptr_t*>(src_sbo + detail::kSboBufferSize) = 0;
  }

  fn->~function();

  if (debug_name) {
    auto dst = reinterpret_cast<char*>(raw + detail::offsets::kNameBuffer);
    std::size_t i{0};

    while (i < detail::kNameBufferLen - 1 && debug_name[i]) {
      dst[i] = debug_name[i];
      ++i;
    }

    dst[i] = '\0';

    *reinterpret_cast<std::uint8_t*>(raw + detail::offsets::kNameSetFlag) = 1;
  }

  return raw;
}

bool Initialize() {
  const auto tier0 = MODULE_BASE("tier0.dll");
  if (!tier0) {
    return false;
  }

  detail::g_pool = Pool(
      memory::Read<std::uintptr_t>(MODULE_EXPORT("tier0.dll:g_pThreadPool")));
  if (!detail::g_pool.Get()) {
    return false;
  }

  detail::std_function_job_vtable =
      memory::FindVtableByRtti(tier0, xs("CStdFunctionJob"));
  if (!detail::std_function_job_vtable) {
    return false;
  }

  /*const auto api_set = GetModuleHandleA (xs
  ("api-ms-win-core-synch-l1-2-0.dll")); if (api_set) { detail::wait_on_address
  = reinterpret_cast<std::uintptr_t>(GetProcAddress (api_set, xs
  ("WaitOnAddress")));
  }*/

  return true;
}

Job Run(std::function<void()> func, JobPriority priority) {
  auto raw = MakeJob(std::move(func), priority);
  if (!raw) {
    return {};
  }

  Job handle(raw, false);
  detail::g_pool.AddJob(raw);
  return handle;
}

void RunSync(std::function<void()> func, JobPriority priority) {
  auto handle = Run(std::move(func), priority);
  if (handle) {
    handle.Wait();
  }
}

void ParallelFor(int begin, int end, const std::function<void(int, int)>& body,
                 int min_chunk_size, JobPriority priority) {
  if (begin >= end) {
    return;
  }

  const int total{end - begin};
  const int max_chunks{
      std::max(static_cast<int>(std::thread::hardware_concurrency()), 1)};

  auto chunk_size{(total + max_chunks - 1) / max_chunks};
  if (chunk_size < min_chunk_size) {
    chunk_size = min_chunk_size;
  }

  const auto num_chunks{(total + chunk_size - 1) / chunk_size};

  if (num_chunks <= 1) {
    body(begin, end);
    return;
  }

  std::vector<Job> jobs;
  jobs.reserve(static_cast<std::size_t>(num_chunks) - 1);

  for (auto i = 0; i < num_chunks - 1; ++i) {
    const auto cb{begin + i * chunk_size};
    const auto ce{std::min(cb + chunk_size, end)};

    jobs.push_back(Run([&body, cb, ce]() { body(cb, ce); }, priority));
  }

  body(begin + (num_chunks - 1) * chunk_size, end);

  for (auto& j : jobs) {
    j.Wait();
  }
}

void RunBatch(std::span<std::function<void()>> tasks, JobPriority priority) {
  if (tasks.empty()) {
    return;
  }

  std::vector<Job> jobs;
  jobs.reserve(tasks.size());

  for (auto& task : tasks) {
    jobs.push_back(Run(std::move(task), priority));
  }

  for (auto& j : jobs) {
    j.Wait();
  }
}

}  // namespace threadpool
