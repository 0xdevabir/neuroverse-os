// neuro/proc/thread.hpp
//
// Thread — kernel-side thread object, per README §4.4.
//
// Phase E is a host-side scaffold: each Thread owns a std::jthread that
// runs the user's callable. Real kernel threads (ring-0 entry, signal
// stacks, KSA) arrive in Phase 1.
//
// State machine:
//   Ready -> Running -> { Waiting | Zombie | Terminated }
//   Waiting threads re-enter Ready on wake().
//   Zombie threads hold their stack and exit status until join().

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "neuro/core/kobject.hpp"
#include "neuro/proc/process.hpp"
#include "neuro/sec/cap_space.hpp"

namespace neuro::proc {

enum class ThreadState : std::uint8_t {
    Ready       = 0,
    Running     = 1,
    Waiting     = 2,
    Zombie      = 3,
    Terminated  = 4,
};

inline const char* to_string(ThreadState s) noexcept {
    switch (s) {
        case ThreadState::Ready:       return "ready";
        case ThreadState::Running:     return "running";
        case ThreadState::Waiting:     return "waiting";
        case ThreadState::Zombie:      return "zombie";
        case ThreadState::Terminated:  return "terminated";
    }
    return "unknown";
}

class Thread : public neuro::core::KObject {
public:
    using Entry = std::function<void(Thread&)>;

    struct Attr {
        std::string         name;
        std::uint32_t       cpu_affinity   = 0;     // bitmask (host: ignored)
        std::uint8_t        priority       = 100;   // 0..255, lower = higher prio
        std::optional<std::size_t> stack_bytes;      // 0 = default
    };

    // Construction registers the thread with its owning Process's
    // capability space and binds the entry function.
    Thread(Process& owner, Attr a, Entry fn);

    ~Thread() override;

    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;

    // Lifecycle.
    void start();
    void join();
    bool join_for(std::chrono::milliseconds d);

    // Wake from Waiting -> Ready.
    void wake() noexcept;

    [[nodiscard]] ThreadState state() const noexcept;

    [[nodiscard]] const std::string& name()     const noexcept { return attr_.name; }
    [[nodiscard]] std::uint32_t     affinity() const noexcept { return attr_.cpu_affinity; }
    [[nodiscard]] std::uint8_t      priority() const noexcept { return attr_.priority; }
    [[nodiscard]] Process&          owner()    const noexcept { return *owner_; }

    // Capability view onto the thread: anyone holding this with
    // CapRight::Signal may wake() it.
    [[nodiscard]] neuro::sec::CapabilitySpace& caps() noexcept;

private:
    void run_loop();

    Attr                                  attr_;
    Process*                              owner_;
    Entry                                 entry_;
    std::thread                           os_thread_;
    mutable std::mutex                    mu_;
    std::condition_variable               cv_;
    std::atomic<ThreadState>              state_{ThreadState::Ready};
    std::atomic<bool>                     done_{false};
};

}  // namespace neuro::proc