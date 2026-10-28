// PS5: who holds the large C++ allocations (os/ps5_title.h, LogMemory).
//
// The title heap grew from about 0.5 GiB to 3.6 GiB during a cutscene and the
// title then ran out of memory. This replaces the global operator new/delete
// with thin wrappers over malloc/free that remember each block of 256 KiB or
// more with its caller's return address, so the memory log can name the code
// holding the most. Smaller blocks cost one malloc_usable_size on delete.
#include <os/platform.h>

#if LO_PLATFORM_PS5
#include "ps5_title.h"
#include <os/logger.h>
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

extern "C" size_t malloc_usable_size(void* pointer);

namespace
{
constexpr size_t kLarge = 256 * 1024;

struct Block { uintptr_t caller; size_t bytes; };

// Plain malloc-backed containers: the tracker must not call operator new.
template <class T> struct RawAllocator
{
    using value_type = T;
    RawAllocator() = default;
    template <class U> RawAllocator(const RawAllocator<U>&) {}
    T* allocate(size_t n) { if (auto* p = static_cast<T*>(std::malloc(n * sizeof(T)))) return p; std::abort(); }
    void deallocate(T* p, size_t) { std::free(p); }
    template <class U> bool operator==(const RawAllocator<U>&) const { return true; }
};
using BlockMap = std::unordered_map<void*, Block, std::hash<void*>, std::equal_to<void*>,
    RawAllocator<std::pair<void* const, Block>>>;

std::mutex& TrackerMutex() { static std::mutex mutex; return mutex; }
BlockMap& Blocks() { static BlockMap* blocks = new (std::malloc(sizeof(BlockMap))) BlockMap(); return *blocks; }
size_t g_live = 0, g_peak = 0, g_reported = 0;

void Record(void* pointer, size_t bytes, uintptr_t caller)
{
    if (!pointer || bytes < kLarge) return;
    std::lock_guard lock(TrackerMutex());
    Blocks()[pointer] = {caller, bytes};
    g_live += bytes;
    g_peak = std::max(g_peak, g_live);
}

void Forget(void* pointer)
{
    if (!pointer || malloc_usable_size(pointer) < kLarge) return;
    std::lock_guard lock(TrackerMutex());
    auto& blocks = Blocks();
    if (auto it = blocks.find(pointer); it != blocks.end()) {
        g_live -= it->second.bytes;
        blocks.erase(it);
    }
}

void* Allocate(size_t bytes, uintptr_t caller)
{
    void* pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer) throw std::bad_alloc();
    Record(pointer, bytes, caller);
    return pointer;
}

void* AllocateAligned(size_t bytes, size_t alignment, uintptr_t caller)
{
    void* pointer = nullptr;
    if (posix_memalign(&pointer, std::max(alignment, sizeof(void*)), bytes ? bytes : 1) != 0 || !pointer)
        throw std::bad_alloc();
    Record(pointer, bytes, caller);
    return pointer;
}

void Release(void* pointer)
{
    Forget(pointer);
    std::free(pointer);
}
}

namespace os::ps5
{
// The largest holders, by caller, as "caller:MiB" (title addresses; subtract
// the load bias the log's anchor gives to look them up).
std::string LargeAllocationSummary()
{
    std::vector<std::pair<size_t, uintptr_t>> top;
    size_t live = 0, peak = 0, count = 0;
    {
        std::lock_guard lock(TrackerMutex());
        std::unordered_map<uintptr_t, size_t, std::hash<uintptr_t>, std::equal_to<uintptr_t>,
            RawAllocator<std::pair<const uintptr_t, size_t>>> byCaller;
        for (const auto& [pointer, block] : Blocks()) byCaller[block.caller] += block.bytes;
        for (const auto& [caller, bytes] : byCaller) top.emplace_back(bytes, caller);
        live = g_live; peak = g_peak; count = Blocks().size();
    }
    std::sort(top.begin(), top.end(), std::greater<>());
    if (top.size() > 8) top.resize(8);
    std::string text = fmt::format("new>=256K live={} MiB peak={} MiB blocks={} anchor={:#x}:", live >> 20, peak >> 20,
        count, reinterpret_cast<uintptr_t>(&LargeAllocationSummary));
    for (const auto& [bytes, caller] : top) text += fmt::format(" {:#x}:{}M", caller, bytes >> 20);
    return text;
}

bool LargeAllocationsGrew(size_t step)
{
    std::lock_guard lock(TrackerMutex());
    const size_t change = g_live > g_reported ? g_live - g_reported : g_reported - g_live;
    if (change < step) return false;
    g_reported = g_live;
    return true;
}
}

#define LO_CALLER reinterpret_cast<uintptr_t>(__builtin_return_address(0))
void* operator new(size_t bytes) { return Allocate(bytes, LO_CALLER); }
void* operator new[](size_t bytes) { return Allocate(bytes, LO_CALLER); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept
{
    try { return Allocate(bytes, LO_CALLER); } catch (...) { return nullptr; }
}
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept
{
    try { return Allocate(bytes, LO_CALLER); } catch (...) { return nullptr; }
}
void* operator new(size_t bytes, std::align_val_t alignment) { return AllocateAligned(bytes, size_t(alignment), LO_CALLER); }
void* operator new[](size_t bytes, std::align_val_t alignment) { return AllocateAligned(bytes, size_t(alignment), LO_CALLER); }
void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return AllocateAligned(bytes, size_t(alignment), LO_CALLER); } catch (...) { return nullptr; }
}
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return AllocateAligned(bytes, size_t(alignment), LO_CALLER); } catch (...) { return nullptr; }
}
void operator delete(void* pointer) noexcept { Release(pointer); }
void operator delete[](void* pointer) noexcept { Release(pointer); }
void operator delete(void* pointer, size_t) noexcept { Release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { Release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { Release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { Release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { Release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { Release(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { Release(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { Release(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { Release(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { Release(pointer); }
#endif
