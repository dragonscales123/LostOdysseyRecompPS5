#pragma once

#include <atomic>
#include <os/atomic_ref.h>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

namespace GuestCriticalSection
{
// The SDK storage fields are native integers, but guest code reads these two
// words with lwz. Keep their memory representation big endian, including CAS.
inline uint32_t Encode(uint32_t value)
{
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(value);
    return value;
}

inline void Increment(int32_t& recursion)
{
    recursion = std::bit_cast<int32_t>(Encode(Encode(std::bit_cast<uint32_t>(recursion)) + 1));
}

inline bool TryEnter(int32_t& recursion, uint32_t& owner, uint32_t thread)
{
    os::AtomicRef<uint32_t> owningThread(owner);
    const uint32_t encodedThread = Encode(thread);
    uint32_t previousOwner = 0;
    if (owningThread.compare_exchange_strong(previousOwner, encodedThread) || previousOwner == encodedThread)
    {
        Increment(recursion);
        return true;
    }
    return false;
}

#if defined(__PROSPERO__)
// The console has no futex: a sleeping waiter is woken through a condition
// variable, tens of microseconds later, while the owner (the game holds these
// locks for a few microseconds at a time, thousands of times a second) has
// already taken the lock again. In a battle load the main thread spent half
// its time asleep on these (LO_PROFILE). Spin for about 50 us first, as the
// console's own RtlEnterCriticalSection spin count intends, then sleep.
// LO_CS_SPIN=<pauses> overrides it (0 = sleep at once, as before build 31).
inline const int kEnterSpins = [] {
    const char* env = std::getenv("LO_CS_SPIN");
    return env ? std::max(0, std::atoi(env)) : 2500;
}();
#else
inline constexpr int kEnterSpins = 0;
#endif

inline void Pause()
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

inline void Enter(int32_t& recursion, uint32_t& owner, uint32_t thread)
{
    os::AtomicRef<uint32_t> owningThread(owner);
    const uint32_t encodedThread = Encode(thread);
    for (;;)
    {
        uint32_t previousOwner = 0;
        if (owningThread.compare_exchange_strong(previousOwner, encodedThread) || previousOwner == encodedThread)
        {
            Increment(recursion);
            return;
        }
        bool released = false;
        for (int spin = 0; spin < kEnterSpins; ++spin)
        {
            Pause();
            if (owningThread.load(std::memory_order_relaxed) == 0) { released = true; break; }
        }
        if (released) continue;
        owningThread.wait(previousOwner);
    }
}

inline void Leave(int32_t& recursion, uint32_t& owner)
{
    const uint32_t remaining = Encode(std::bit_cast<uint32_t>(recursion)) - 1;
    recursion = std::bit_cast<int32_t>(Encode(remaining));
    if (remaining != 0)
        return;
    os::AtomicRef<uint32_t> owningThread(owner);
    owningThread.store(0);
    owningThread.notify_one();
}
}
