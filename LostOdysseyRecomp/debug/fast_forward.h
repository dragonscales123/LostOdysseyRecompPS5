#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>

// One continuous guest clock for mftb and KeTimeStampBundle. Host rendering,
// UI deadlines and I/O waits keep their own unscaled clocks. The audio driver
// follows AudioRate(): it mixes that many guest frames per output frame.
namespace debug_menu::fast_forward {
inline constexpr unsigned Rates[] = {2, 3, 4, 6, 8};
inline constexpr uint64_t InputLeaseNs = 250000000;
enum class Mode { Hold, Toggle };
enum class Button { LT = 0, LB = 1 };
struct Buttons { uint8_t lt = 0, rt = 0, lb = 0, rb = 0; };
struct ButtonInput { uint8_t button, pair; };
inline ButtonInput SelectButton(Button button, const Buttons& buttons) {
    switch (button) {
    case Button::LB: return {buttons.lb, buttons.rb};
    default: return {buttons.lt, buttons.rt};
    }
}
#if defined(__PROSPERO__)
extern "C" uint64_t sceKernelGetTscFrequency(void);
extern "C" uint64_t sceKernelReadTsc(void);
#endif
inline uint64_t NowNs() {
#if defined(__PROSPERO__)
    // The console's invariant TSC: no system call per guest mftb.
    static const uint64_t frequency = [] { const char* e = getenv("LO_GUEST_CLOCK"); return e && e[0] == '0' ? 0 : sceKernelGetTscFrequency(); }();
    if (frequency) {
        const unsigned __int128 ticks = sceKernelReadTsc();
        return uint64_t(ticks * 1000000000u / frequency);
    }
#endif
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Clock {
    uint64_t last = 0, value = 0, lease = 0;
    unsigned rate = 1;
    bool initialized = false, paused = false;
    void Add(uint64_t elapsed, unsigned factor) {
        const auto room = std::numeric_limits<uint64_t>::max() - value;
        value += elapsed > room / factor ? room : elapsed * factor;
    }
    uint64_t Read(uint64_t now) {
        if (!initialized) { initialized = true; last = value = now; }
        if (now < last) return value;
        if (!paused) {
            const auto boostedEnd = std::clamp(lease, last, now);
            Add(boostedEnd - last, rate);
            Add(now - boostedEnd, 1);
        }
        last = now;
        if (now >= lease) rate = 1;
        return value;
    }
};
struct Status { bool enabled = false, active = false; unsigned multiplier = 2; Mode mode = Mode::Hold; bool keepAudioPitch = true; Button button = Button::LT; };
struct Control {
    Clock clock;
    bool enabled = false, armed = false, held = false;
    unsigned multiplier = 2;
    Mode mode = Mode::Hold;
    bool keepAudioPitch = true;
    Button button = Button::LT;
    void Stop(uint64_t now) {
        clock.Read(now); clock.rate = 1; clock.lease = now;
        held = armed = false;
    }
    void Enable(bool value, uint64_t now) { Stop(now); enabled = value; }
    void SetMode(Mode value, uint64_t now) { Stop(now); mode = value; }
    void SetButton(Button value, uint64_t now) {
        Stop(now); button = value >= Button::LT && value <= Button::LB ? value : Button::LT;
    }
    void SetRate(unsigned value, uint64_t now) {
        if (std::find(std::begin(Rates), std::end(Rates), value) == std::end(Rates)) return;
        clock.Read(now); multiplier = value;
        if (clock.rate > 1) clock.rate = multiplier;
    }
    void Pause(bool value, uint64_t now) {
        Stop(now); clock.paused = value;
    }
    void Sample(uint8_t button, uint8_t pair, bool allowed, uint64_t now) {
        clock.Read(now);
        if (!allowed || clock.paused || !enabled) { Stop(now); return; }
        // Require a release after focus/menu/enable/binding changes. Hysteresis
        // prevents trigger noise; paired buttons remain available for shortcuts.
        if (button <= 32) {
            if (mode == Mode::Hold) held = false;
            armed = true;
        } else if (button >= 64 && armed) {
            if (mode == Mode::Hold) held = true;
            else if (pair <= 32) held = !held;
            armed = false;
        }
        clock.rate = held && pair <= 32 ? multiplier : 1;
        clock.lease = now > std::numeric_limits<uint64_t>::max() - InputLeaseNs
            ? std::numeric_limits<uint64_t>::max() : now + InputLeaseNs;
    }
    Status Get(uint64_t now) {
        clock.Read(now);
        return {enabled, clock.rate > 1 && !clock.paused, multiplier, mode, keepAudioPitch, button};
    }
    unsigned ActiveRate() const { return clock.rate > 1 && !clock.paused ? clock.rate : 1; }
};
inline std::mutex mutex;
inline Control control;
// Lock-free reads while the clock runs at 1x (nearly always): game time is
// then host time minus a fixed offset. A sequence count, odd while the locked
// state may change, lets a reader detect a change under it and take the lock;
// a locked change reads the host clock after making the count odd, so it
// never returns a time earlier than a lock-free read that completed before.
// Every mftb comes here, from every guest thread, hundreds of thousands of
// times a second; one shared mutex made them queue behind each other.
inline std::atomic<uint64_t> fastSequence{1};
inline std::atomic<uint64_t> fastOffset{0};
// Caller holds mutex, before changing the clock.
inline void BeginChange() { fastSequence.store(fastSequence.load() | 1); }
// The clock's current rate for the audio driver, read without the lock.
inline std::atomic<unsigned> audioRate{1};
inline std::atomic<bool> audioKeepPitch{true};
// Caller holds mutex.
inline void PublishRate() {
    const Clock& clock = control.clock;
    if (clock.initialized && !clock.paused && clock.rate == 1) {
        BeginChange();
        fastOffset.store(clock.last - clock.value);
        fastSequence.store(fastSequence.load() + 1);
    } else {
        BeginChange();
    }
    const unsigned rate = control.ActiveRate();
    if (audioRate.load(std::memory_order_relaxed) != rate) audioRate.store(rate, std::memory_order_relaxed);
}
inline unsigned AudioRate() { return audioRate.load(std::memory_order_relaxed); }
inline bool KeepAudioPitch() { return audioKeepPitch.load(std::memory_order_relaxed); }
// LO_GUEST_CLOCK=0: the locked clock of build 28 and earlier (for comparison).
inline const bool fastClock = [] { const char* e = getenv("LO_GUEST_CLOCK"); return !(e && e[0] == '0'); }();
inline uint64_t GameTimeNs() {
    const uint64_t sequence = fastSequence.load();
    if (fastClock && !(sequence & 1)) {
        const uint64_t time = NowNs() - fastOffset.load();
        if (fastSequence.load() == sequence) return time;
    }
    std::lock_guard lock(mutex);
    BeginChange();
    const uint64_t value = control.clock.Read(NowNs());
    PublishRate();
    return value;
}
inline Status GetStatus() { std::lock_guard lock(mutex); BeginChange(); const Status status = control.Get(NowNs()); PublishRate(); return status; }
inline void Enable(bool enabled) { std::lock_guard lock(mutex); BeginChange(); control.Enable(enabled, NowNs()); PublishRate(); }
inline void SetMode(Mode mode) { std::lock_guard lock(mutex); BeginChange(); control.SetMode(mode, NowNs()); PublishRate(); }
inline void SetButton(Button button) { std::lock_guard lock(mutex); BeginChange(); control.SetButton(button, NowNs()); PublishRate(); }
inline void SetRate(unsigned rate) { std::lock_guard lock(mutex); BeginChange(); control.SetRate(rate, NowNs()); PublishRate(); }
inline void SetKeepAudioPitch(bool keep) {
    std::lock_guard lock(mutex);
    control.keepAudioPitch = keep;
    audioKeepPitch.store(keep, std::memory_order_relaxed);
}
inline void SetPaused(bool paused) { std::lock_guard lock(mutex); BeginChange(); control.Pause(paused, NowNs()); PublishRate(); }
inline void Release() { std::lock_guard lock(mutex); BeginChange(); control.Stop(NowNs()); PublishRate(); }
inline void Sample(uint8_t button, uint8_t pair, bool allowed) {
    std::lock_guard lock(mutex); BeginChange(); control.Sample(button, pair, allowed, NowNs()); PublishRate();
}
} // namespace debug_menu::fast_forward
