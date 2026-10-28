#include "ps5_title.h"

#if LO_PLATFORM_PS5
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <csignal>
#include <cerrno>
#include <exception>
#include <typeinfo>
#include <ps5platform/context.h>
#include <ps5platform/fp.h>
#include <ps5platform/klog.h>
#include <ps5platform/heap.h>
#include <ps5platform/kernel.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <os/logger.h>
#if LO_GPU_PLUME
#include <volk.h>
#endif

// RADV's ICD entry point, linked into the title by PS5_Vulkan's recipe.
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" int sceSystemServiceLoadExec(const char* path, const char* const* argv);
extern "C" int sceKernelUsleep(uint32_t microseconds);
extern "C" int sceSystemServiceGetHdrToneMapLuminance(void* luminance);
extern "C" int mspace_mallopt(int parameter, int value);
extern "C" int32_t scePthreadGetaffinity(pthread_t thread, uint64_t* mask);
extern "C" int32_t scePthreadSetaffinity(pthread_t thread, uint64_t mask);

namespace os::ps5
{
[[noreturn]] static void ExitTitle(int status);

void Breadcrumb(const char* line)
{
    static bool started = false;
    const int flags = O_WRONLY | O_CREAT | (started ? O_APPEND : O_TRUNC);
    started = true;
    const int fd = open("/app0/boot.txt", flags, 0666);
    if (fd >= 0)
    {
        (void)write(fd, line, std::strlen(line));
        (void)write(fd, "\n", 1);
        (void)close(fd);
    }
    char text[512];
    std::snprintf(text, sizeof(text), "[Lost Odyssey] %s\n", line);
    (void)sceKernelDebugOutText(0, text);
}

float SystemHdrPeakNits()
{
    // { maxFullFrameToneMapLuminance, maxToneMapLuminance, minToneMapLuminance }
    // as floats; padded in case the console's structure is larger.
    static const float peak = [] {
        float values[16];
        for (auto& value : values) value = -1.0f;
        const int rc = sceSystemServiceGetHdrToneMapLuminance(values);
        char line[192];
        std::snprintf(line, sizeof(line), "hdr: system tone map luminance rc=%#x full_frame=%.1f peak=%.1f min=%.4f next=%.1f",
            unsigned(rc), values[0], values[1], values[2], values[3]);
        Breadcrumb(line);
        std::fprintf(stderr, "%s\n", line);
        const float value = values[1];
        return rc == 0 && value >= 80.0f && value <= 10000.0f ? value : 0.0f;
    }();
    return peak;
}

namespace
{
struct Placement
{
    int mode = 0;
    uint64_t title = 0;   // CPUs the title may use
    uint64_t mainCore = 0; // the main thread's core: lowest CPU and its sibling
};

// Busy work timed on one CPU, alone and then with a spinner on another CPU;
// an SMT sibling shares the core's execution units and slows it clearly.
static double TimedWork(int cpu)
{
    (void)scePthreadSetaffinity(pthread_self(), 1ull << cpu);
    std::this_thread::yield();
    volatile uint64_t value = 1;
    double best = 1e30;
    for (int round = 0; round < 3; ++round)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 4000000; ++i) value = value * 6364136223846793005ull + 1442695040888963407ull;
        best = std::min(best, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    }
    return best;
}

static Placement& GetPlacement()
{
    static Placement placement;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* env = getenv("LO_PS5_PIN");
        placement.mode = env && env[0] >= '0' && env[0] <= '2' ? env[0] - '0' : 1;
        uint64_t mask = 0;
        const int got = scePthreadGetaffinity(pthread_self(), &mask);
        placement.title = got == 0 ? mask : 0;
        if (!placement.mode || !placement.title || __builtin_popcountll(placement.title) < 4)
        {
            LOG_NOTICE("thread placement: mode={} title_cpus={:#x} (rc={:#x}); none applied", placement.mode, placement.title, unsigned(got));
            placement.mode = 0;
            return;
        }
        const int first = __builtin_ctzll(placement.title);
        const double alone = TimedWork(first);
        int sibling = -1;
        double worst = 0;
        std::string timings;
        for (int cpu = first + 1; cpu < 64; ++cpu)
        {
            if (!(placement.title & (1ull << cpu))) continue;
            std::atomic<bool> stop{false};
            std::thread spinner([&] {
                (void)scePthreadSetaffinity(pthread_self(), 1ull << cpu);
                volatile uint64_t x = 0;
                while (!stop.load(std::memory_order_relaxed)) x = x + 1;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const double shared = TimedWork(first);
            stop = true;
            spinner.join();
            timings += fmt::format(" {}:{:.2f}", cpu, shared / alone);
            if (shared / alone > worst) { worst = shared / alone; sibling = cpu; }
        }
        (void)scePthreadSetaffinity(pthread_self(), placement.title);
        placement.mainCore = 1ull << first;
        if (worst > 1.25 && sibling >= 0) placement.mainCore |= 1ull << sibling;
        LOG_NOTICE("thread placement: mode={} title_cpus={:#x} main_core={:#x} slowdown with a spinner on CPU n:{}",
            placement.mode, placement.title, placement.mainCore, timings);
    });
    return placement;
}
}

void LogMemory(const char* why)
{
    struct ps5_heap_stats heap{};
    ps5_heap_stats(&heap);
    int64_t start = 0;
    size_t available = 0;
    const int rc = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 0x10000, &start, &available);
    char line[320];
    std::snprintf(line, sizeof(line),
        "memory (%s): heap mapped=%zu MiB peak=%zu MiB segments=%u arenas=%u libc_fallbacks=%llu; direct memory total=%lld MiB largest_free=%zu MiB (rc=%#x)",
        why, heap.mapped_bytes >> 20, heap.peak_bytes >> 20, heap.segments, heap.arenas, heap.libc_fallbacks,
        (long long)(sceKernelGetDirectMemorySize() >> 20), available >> 20, unsigned(rc));
    const std::string large = LargeAllocationSummary();
    if (std::strcmp(why, "periodic") == 0) LOG_INFO("{}; {}", line, large);
    else { Breadcrumb(line); Breadcrumb(large.c_str()); LOG_NOTICE("{}; {}", line, large); }
}

void StartMemoryLog()
{
    // LO_PS5_MMAP_THRESHOLD_MB=<n>: blocks of n MiB and more get a heap
    // segment of their own (dlmalloc's default is 256 KiB). The title heap
    // holds at most 1024 segments, so many live blocks over the threshold
    // can exhaust it while memory is still free.
    if (const char* env = getenv("LO_PS5_MMAP_THRESHOLD_MB"); env && atoi(env) > 0) {
        const int rc = mspace_mallopt(-3 /* M_MMAP_THRESHOLD */, atoi(env) << 20);
        LOG_NOTICE("memory: large-block threshold {} MiB (rc={})", atoi(env), rc);
    }
    LogMemory("start");
    std::thread([] {
        // Every 30 s, and at once when the large blocks change by 256 MiB.
        for (unsigned tick = 1;; ++tick) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (LargeAllocationsGrew(size_t(256) << 20)) LogMemory("jump");
            else if (tick % 15 == 0) LogMemory("periodic");
        }
    }).detach();
}

void PlaceGuestMainThread()
{
    const auto& placement = GetPlacement();
    if (placement.mode != 2) return;
    const int rc = scePthreadSetaffinity(pthread_self(), placement.mainCore);
    LOG_NOTICE("thread placement: guest main thread on {:#x} rc={:#x}", placement.mainCore, unsigned(rc));
}

void PlaceGuestWorkerThread(uint32_t function, uint32_t flags)
{
    const auto& placement = GetPlacement();
    if (!placement.mode) return;
    const uint64_t mask = placement.title & ~placement.mainCore;
    const int rc = scePthreadSetaffinity(pthread_self(), mask);
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 24)
        LOG_INFO("thread placement: guest thread {:#x} flags={:#x} on {:#x} rc={:#x}", function, flags, mask, unsigned(rc));
}

std::filesystem::path TitleDirectory()
{
    return "/app0";
}

void InitializeTitle()
{
    Breadcrumb("main: started");
    (void)ps5_klog_capture_stderr("[Lost Odyssey] ");
    Breadcrumb("main: stderr to klog");
    ps5_fp_ieee();
    {
        const int rc = chdir("/app0");
        char line[96];
        std::snprintf(line, sizeof(line), "main: chdir /app0 rc=%d errno=%d", rc, rc == 0 ? 0 : errno);
        Breadcrumb(line);
    }
    // The shell's splash covers the title until the title takes it down; a
    // title presenting behind it shows a black screen.
    (void)sceSystemServiceHideSplashScreen();
    Breadcrumb("main: splash hidden");
#if LO_GPU_PLUME
    // plume keeps a loader volk already has (plume_vulkan.cpp, VulkanInterface).
    volkInitializeCustom(reinterpret_cast<PFN_vkGetInstanceProcAddr>(vk_icdGetInstanceProcAddr));
#endif
    Breadcrumb("main: Vulkan entry points from RADV");

    // A title gets no environment. LO_* switches come from /app0/ps5-env.txt
    // (KEY=VALUE per line, # comments), over the PS5 defaults below.
    if (FILE* env = std::fopen("/app0/ps5-env.txt", "r"))
    {
        char line[512];
        while (std::fgets(line, sizeof(line), env))
        {
            line[std::strcspn(line, "\r\n")] = '\0';
            char* equals = std::strchr(line, '=');
            if (line[0] == '#' || !equals) continue;
            *equals = '\0';
            setenv(line, equals + 1, 1);
            char crumb[600];
            std::snprintf(crumb, sizeof(crumb), "main: ps5-env.txt %s=%s", line, equals + 1);
            Breadcrumb(crumb);
        }
        std::fclose(env);
    }
    // The console's RADV counts occlusion coarsely (16 samples per count, not
    // precise; PS5_Vulkan README) and its results read as zero in battle, which
    // makes the game cull what it should draw. Use the counts the command
    // processor writes itself, as on backends without occlusion queries.
    setenv("LO_ZPD_MODE", "fake", 0);
    if (const char* probe = std::getenv("LO_PS5_VIDEO_PROBE"); probe && (probe[0] == '1' || probe[0] == '2'))
    {
        RunVideoOutProbe();
        ExitTitle(0);
    }
}

// A title ends by asking the shell to close it: exit() faults in a libkernel
// stub and returning from _start jumps to 0, and the console then reports a
// crash over a run that worked (PS5_VulkanTemplate, platform-contracts.md).
// The shell closes the title asynchronously, so this never returns.
[[noreturn]] static void ExitTitle(int status)
{
    std::fprintf(stderr, "exit status %d: asking the shell to close the title\n", status);
    std::fflush(nullptr);
    (void)sceSystemServiceLoadExec("exit", nullptr);
    for (;;)
        sceKernelUsleep(100000);
}
}

// The first code of a run, ahead of the runtime's own static constructors
// (guest memory is set up in one), so boot.txt shows that the title loaded.
// Until the runtime's crash handler takes over (after its log exists), a fatal
// signal writes where it happened to boot.txt and klog: the signal, the fault
// address, rip and rsp, the address of Breadcrumb (the load bias anchor), and
// the stack words that look like return addresses into the title. Then the
// default action runs, so the console still writes its crash report.
static constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGSYS, SIGABRT, SIGTRAP};

static void EarlyCrash(int signal, siginfo_t* info, void* context)
{
    const auto* uc = static_cast<const ucontext_t*>(context);
    const uintptr_t anchor = reinterpret_cast<uintptr_t>(&os::ps5::Breadcrumb);
    char line[256];
    std::snprintf(line, sizeof(line), "CRASH signal=%d code=%d addr=%p rip=0x%lx rsp=0x%lx anchor(Breadcrumb)=0x%lx",
                  signal, info ? info->si_code : 0, info ? info->si_addr : nullptr,
                  (unsigned long)uc->uc_mcontext.mc_rip, (unsigned long)uc->uc_mcontext.mc_rsp,
                  (unsigned long)anchor);
    os::ps5::Breadcrumb(line);
    const auto* stack = reinterpret_cast<const uintptr_t*>(uc->uc_mcontext.mc_rsp);
    int found = 0;
    for (int i = 0; i < 512 && found < 24; ++i)
    {
        const uintptr_t value = stack[i];
        // The title is loaded at 0x400000; its code is well inside 256 MiB.
        if (value >= 0x400000 && value < 0x10400000)
        {
            std::snprintf(line, sizeof(line), "  stack[%d]=0x%lx", i, (unsigned long)value);
            os::ps5::Breadcrumb(line);
            ++found;
        }
    }
    struct sigaction fallback{};
    fallback.sa_handler = SIG_DFL;
    sigaction(signal, &fallback, nullptr);
}

// An uncaught exception ends in abort(); name it first.
[[noreturn]] static void Terminate()
{
    char line[400];
    const char* what = "no exception";
    const char* type = "";
    if (auto current = std::current_exception())
    {
        try { std::rethrow_exception(current); }
        catch (const std::exception& e) { what = e.what(); type = typeid(e).name(); }
        catch (...) { what = "non-std exception"; }
    }
    std::snprintf(line, sizeof(line), "TERMINATE: uncaught %s: %s", type, what);
    os::ps5::Breadcrumb(line);
    os::ps5::LogMemory("terminate");
    std::abort();
}

__attribute__((constructor(101))) static void FirstConstructor()
{
    os::ps5::Breadcrumb("title loaded: static constructors running");
    std::set_terminate(Terminate);
    struct sigaction action{};
    action.sa_sigaction = EarlyCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : kFatalSignals)
        sigaction(signal, &action, nullptr);
}

// The CRT (PS5_Vulkan's app_crt.cpp) calls this after main returns.
extern "C" void catchReturnFromMain(int status)
{
    os::ps5::ExitTitle(status);
}

// ps5/link-title.sh links with --wrap=exit --wrap=_exit, so a quit from deep
// inside the runtime (or SDL, or a library) takes the same way out.
extern "C" [[noreturn]] void __wrap_exit(int status)
{
    os::ps5::ExitTitle(status);
}

extern "C" [[noreturn]] void __wrap__exit(int status)
{
    os::ps5::ExitTitle(status);
}

// std::_Exit, which the runtime's quit paths call.
extern "C" [[noreturn]] void __wrap__Exit(int status)
{
    os::ps5::ExitTitle(status);
}
#endif
