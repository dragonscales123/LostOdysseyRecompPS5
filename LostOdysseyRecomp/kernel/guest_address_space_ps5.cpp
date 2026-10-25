#include <os/platform.h>
#if LO_PLATFORM_PS5
#include "guest_address_space_layout.h"
#include <os/ps5_title.h>
#include <cstdio>
#include <ps5platform/shm.h>

// PS5 backend (docs/PS5.md). A title's own mappings are charged to a small
// flexible-memory budget (448 MiB to 1 GiB), so the guest space lives in
// direct memory through the PS5 platform layer (ps5platform/shm.h):
//
//   - the whole 4 GiB is one reserved range at 0x10_0000_0000 or above, where
//     the console grants large reservations at the hint (PS5_PayloadSDK
//     platform/docs/PROBE.md, "Virtual space"); no runtime code depends on a
//     particular base;
//   - the virtual view is one direct-memory object mapped once (committing
//     2.5 GiB unit by unit would make some 40,000 separate mappings);
//   - the A view is a second object, mapped again at C, so a write through
//     either is visible through the other.
//
// Pages are 16 KiB, as on Apple Silicon: the E view (A shifted by 4 KiB)
// cannot be expressed with page mappings, so it stays reserved with no access,
// exactly as the macOS backend leaves it (the runtime never hands out E
// addresses; MmGetPhysicalAddress applies the shift arithmetically). The
// guest's first 64 KiB stay unmapped as its null page; its first virtual
// allocation is at 0x100000.
namespace GuestAddressSpace
{
static constexpr uintptr_t kPreferredBase = 0x1000000000ull;
static constexpr size_t kUnit = 0x10000;  // direct-memory unit

static ps5_shm s_virtual{};
static ps5_shm s_physical{};
static uint8_t* s_base = nullptr;

static void Step(const char* what, int rc, const void* address = nullptr)
{
    char line[160];
    std::snprintf(line, sizeof(line), "guest memory: %s rc=0x%08x at %p", what, unsigned(rc), address);
    os::ps5::Breadcrumb(line);
}

static bool MapAt(const ps5_shm& object, uint8_t* address, size_t bytes, int view)
{
    void* mapped = nullptr;
    const int rc = ps5_shm_map(&object, 0, bytes, address, PS5_SHM_READ | PS5_SHM_WRITE, PS5_SHM_FIXED, &mapped);
    Step(view == 0 ? "map virtual" : view == 1 ? "map A" : "map C", rc, mapped);
    if (rc != 0 || mapped != address)
    {
        RecordFailure(FailureOperation::MapView, uint32_t(rc), view, address, bytes, 0);
        return false;
    }
    return true;
}

uint8_t* Allocate()
{
    ClearFailure();
    void* reserved = nullptr;
    int rc = ps5_vrange_reserve(kSize, reinterpret_cast<void*>(kPreferredBase), kUnit, &reserved);
    Step("reserve 4 GiB", rc, reserved);
    if (rc != 0 || !reserved)
    {
        RecordFailure(FailureOperation::ReserveAny, uint32_t(rc), -1,
                      reinterpret_cast<void*>(kPreferredBase), kSize);
        return nullptr;
    }
    auto* base = static_cast<uint8_t*>(reserved);

    // Virtual view, less the null page.
    const size_t virtualBytes = kSizes[0] - kUnit;
    rc = ps5_shm_create(virtualBytes, &s_virtual);
    Step("create virtual (2.5 GiB)", rc);
    if (rc != 0)
    {
        RecordFailure(FailureOperation::CreateBacking, uint32_t(rc), 0, nullptr, virtualBytes);
        ps5_vrange_release(base, kSize);
        return nullptr;
    }
    if (!MapAt(s_virtual, base + kStarts[0] + kUnit, virtualBytes, 0))
    {
        ps5_shm_destroy(&s_virtual);
        ps5_vrange_release(base, kSize);
        return nullptr;
    }

    // Physical memory: one object, viewed at A and at C.
    rc = ps5_shm_create(kSizes[1], &s_physical);
    Step("create physical (512 MiB)", rc);
    if (rc != 0)
    {
        RecordFailure(FailureOperation::CreateBacking, uint32_t(rc), 1, nullptr, kSizes[1]);
        ps5_shm_unmap(base + kStarts[0] + kUnit, virtualBytes, PS5_SHM_KEEP_RESERVED);
        ps5_shm_destroy(&s_virtual);
        ps5_vrange_release(base, kSize);
        return nullptr;
    }
    if (!MapAt(s_physical, base + kStarts[1], kSizes[1], 1) ||
        !MapAt(s_physical, base + kStarts[2], kSizes[2], 2))
    {
        ps5_shm_unmap(base + kStarts[1], kSizes[1], PS5_SHM_KEEP_RESERVED);
        ps5_shm_unmap(base + kStarts[0] + kUnit, virtualBytes, PS5_SHM_KEEP_RESERVED);
        ps5_shm_destroy(&s_physical);
        ps5_shm_destroy(&s_virtual);
        ps5_vrange_release(base, kSize);
        return nullptr;
    }

    s_base = base;
    os::ps5::Breadcrumb("guest memory: ready");
    return base;
}

void Release(uint8_t* base)
{
    if (!base || base != s_base)
        return;
    ps5_shm_unmap(base + kStarts[2], kSizes[2], PS5_SHM_KEEP_RESERVED);
    ps5_shm_unmap(base + kStarts[1], kSizes[1], PS5_SHM_KEEP_RESERVED);
    ps5_shm_unmap(base + kStarts[0] + kUnit, kSizes[0] - kUnit, PS5_SHM_KEEP_RESERVED);
    ps5_shm_destroy(&s_physical);
    ps5_shm_destroy(&s_virtual);
    ps5_vrange_release(base, kSize);
    s_base = nullptr;
}
}
#endif
