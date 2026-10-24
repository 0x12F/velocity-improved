module;

#include "platform/platform.h"

export module velocity:utilities.threadpool.threadpool;

import <cstdint>;
import <functional>;
import <span>;

export {
namespace threadpool {

enum class JobPriority : std::uint8_t {
  kLow = 0,
  kNormal = 1,
  kHigh = 2,
  kUrgent = 3,
};

enum class JobStatus : int {
  kIdle = 0,
  kQueued = 1,
  kRunning = 2,
  kAborted = 3,
  kReset = 4,
};

class Job {
 public:
  Job() = default;
  explicit Job(std::uintptr_t ptr, bool add_reference = true);
  ~Job();

  Job(const Job& o);
  Job& operator=(const Job& o);
  Job(Job&& o) noexcept;
  Job& operator=(Job&& o) noexcept;

  [[nodiscard]] bool Valid() const { return this->m_ptr_ != 0; }
  [[nodiscard]] std::uintptr_t Get() const { return this->m_ptr_; }
  [[nodiscard]] explicit operator bool() const { return this->Valid(); }

  [[nodiscard]] JobStatus Status() const;
  [[nodiscard]] bool Complete() const;
  void Wait() const;

 private:
  void AddRef() const;
  void DoRelease() const;

  std::uintptr_t m_ptr_{};
};

class Pool {
 public:
  Pool() = default;
  explicit Pool(std::uintptr_t instance) : m_instance_(instance) {}

  [[nodiscard]] bool Valid() const { return this->m_instance_ != 0; }
  [[nodiscard]] std::uintptr_t Get() const { return this->m_instance_; }

  void AddJob(std::uintptr_t job) const;

 private:
  std::uintptr_t m_instance_{};
};

bool Initialize();

std::uintptr_t MakeJob(std::function<void()>&& func,
                       JobPriority priority = JobPriority::kNormal,
                       const char* debug_name = nullptr);
Job Run(std::function<void()> func,
        JobPriority priority = JobPriority::kNormal);
void RunSync(std::function<void()> func,
             JobPriority priority = JobPriority::kNormal);
void ParallelFor(int begin, int end, const std::function<void(int, int)>& body,
                 int min_chunk_size = 1,
                 JobPriority priority = JobPriority::kNormal);
void RunBatch(std::span<std::function<void()>> tasks,
              JobPriority priority = JobPriority::kNormal);

}  // namespace threadpool
}  // export
