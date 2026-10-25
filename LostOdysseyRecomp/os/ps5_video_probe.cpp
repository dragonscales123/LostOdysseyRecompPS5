// PS5 VideoOut probe (LO_PS5_VIDEO_PROBE=1 in /app0/ps5-env.txt): what the
// console's display output accepts from this title, for HDR. It runs instead
// of the game, writes every result to /app0/boot.txt, and quits.
//
// The Vulkan driver (PS5_Mesa wsi_common_videoout.c) presents 8-bit SDR only:
// it registers B8G8R8A8 (0x8000000000000000) and offers sRGB. Nothing names
// the 64-bit codes of the 10-bit formats. The two known codes match the
// PS4's 32-bit ones moved apart: A8R8G8B8_SRGB 0x80000000 is
// 0x8000000000000000 here and A8B8G8R8_SRGB 0x80002200 is 0x8000000022000000
// (PS5_Vulkan demo_renderer.cpp), so byte 3 moves to byte 7 and byte 1 to
// byte 3. The PS4's 10-bit formats also use byte 2 (A2R10G10B10 0x88060000,
// A2R10G10B10_BT2020_PQ 0x88740000), which goes to a byte no code shows: the
// probe tries each. Registration refuses a format the console does not know.
//
//   1. The output-mode selectors 0-63 (sceVideoOutIsOutputSupported), now
//      that param.json declares HDR and high frame rate (PS5_Vulkan R92
//      found 1 and 15 without the HDR declaration).
//   2. Each candidate format, registered and unregistered on its own.
//   3. For each 10-bit format the console took, 8 s of a test pattern: N white
//      squares at the top (the step number, also in boot.txt), then five grey
//      bars at PQ codes 0, 520, 592, 769 and 1023 (black, ~100, ~200, ~1,000
//      and 10,000 nits). On an HDR output the right bars are far brighter than
//      the 100-nit one; as SDR they are plain greys. Step 1 shows the same
//      bars as 8-bit SDR to compare. Each new output-mode selector found in 1
//      is then tried with the first 10-bit format.
#include "ps5_title.h"

#if LO_PLATFORM_PS5
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <map>
#include <ps5platform/videoout.h>
#include <string>
#include <utility>
#include <vector>

extern "C" {
std::size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, std::size_t length,
                                  std::size_t alignment, int memoryType, int64_t* physicalAddress);
int sceKernelMapDirectMemory(void** address, std::size_t length, int protection, int flags,
                             int64_t physicalAddress, std::size_t alignment);
int sceKernelUsleep(uint32_t microseconds);
// Read-only VideoOut queries for the output-mode layout (build 18). The
// signatures are not documented: each gets a large zeroed buffer and a size,
// and extra arguments are harmless in this calling convention.
int sceVideoOutGetCurrentOutputMode_(int handle, void* mode, std::size_t size);
int sceVideoOutModeSetAny_(void* mode, std::size_t size);
int sceVideoOutGetOutputStatus(int handle, void* status, std::size_t size);
int sceVideoOutGetResolutionStatus(int handle, void* status, std::size_t size);
int sceVideoOutGetDeviceCapabilityInfo_(int handle, void* info, std::size_t size);
int sceVideoOutGetVblankStatus(int handle, void* status, std::size_t size);
// Build 19. On the PS4 a mode is ModeSetAny_ (fills "any" and the size) with
// some fields set, then ConfigureOutputMode_(handle, 0, &mode, options); the
// trailing sizes are passed in case the PS5's form takes them.
int sceVideoOutConfigureOutputMode_(int handle, uint32_t reserved, const void* mode, const void* options,
                                    std::size_t modeSize, std::size_t optionsSize);
}

namespace os::ps5
{
namespace
{
constexpr unsigned kWidth = 3840, kHeight = 2160;
constexpr std::size_t kFrameBytes = 32u << 20; // 30x17 tiles of 64 KiB
constexpr std::size_t kAlignment = 2u << 20;
// The memory type and protection the driver's own framebuffers use
// (wsi_common_videoout.c: PS5_KERNEL_DIRECT_TYPE_CPU, CPU and GPU read/write).
// The demo renderer's write-combined type 3 is refused at registration,
// 0x80290001, even for the SDR format the game presents with.
constexpr int kMemoryType = 12;
constexpr int kProtection = 0x33;
constexpr uint64_t kSdrFormat = UINT64_C(0x8000000000000000);

void Log(const std::string& line) { Breadcrumb(line.c_str()); }

std::string Hex(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "0x%016llx", (unsigned long long)value);
    return text;
}

// The 64 KiB 128x128 tile layout VideoOut scans four-byte pixels from
// (PS5_Vulkan demo_renderer.cpp tiled_byte_offset).
std::size_t TiledOffset(unsigned x, unsigned y)
{
    const uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                            ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                            ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const uint32_t blocksPerRow = (kWidth + 127U) >> 7;
    const uint32_t block = (y >> 7) * blocksPerRow + (x >> 7);
    return (std::size_t(block) << 16) + offset;
}

// Grey at a 10-bit code (alpha opaque); the channel order does not matter.
uint32_t Grey10(uint32_t code) { return (3u << 30) | (code << 20) | (code << 10) | code; }
// The same grey as 8-bit, for the SDR comparison.
uint32_t Grey8(uint32_t code10) { const uint32_t v = code10 * 255 / 1023; return 0xff000000u | (v << 16) | (v << 8) | v; }

// The display reads memory, not the CPU's cache (wsi_common_videoout.c).
void Flush(uint8_t* frame)
{
    for (std::size_t at = 0; at < kFrameBytes; at += 64) _mm_clflush(frame + at);
    _mm_mfence();
}

void Fill(uint8_t* frame, unsigned step, bool tenBit)
{
    static constexpr uint32_t kBars[] = {0, 520, 592, 769, 1023};
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            uint32_t code = kBars[std::min<unsigned>(x * 5 / kWidth, 4)];
            // Step marker: `step` white squares of 120 px along the top.
            if (y >= 80 && y < 200 && x >= 80 && (x - 80) % 200 < 120 && (x - 80) / 200 < step) code = 1023;
            else if (y < 280) code = 0;
            *reinterpret_cast<uint32_t*>(frame + TiledOffset(x, y)) = tenBit ? Grey10(code) : Grey8(code);
        }
    Flush(frame);
}

struct Candidate { uint64_t format; std::string name; };

[[maybe_unused]] std::vector<Candidate> Candidates()
{
    std::vector<Candidate> list{{kSdrFormat, "B8G8R8A8 SDR (known)"},
                                {UINT64_C(0x8000000022000000), "R8G8B8A8 sRGB (known)"}};
    struct Ps4 { uint32_t code; const char* name; };
    static constexpr Ps4 kPs4[] = {{0x88000000u, "A2R10G10B10 sRGB"}, {0x88060000u, "A2R10G10B10"},
                                   {0x88740000u, "A2R10G10B10 BT2020 PQ"}, {0xc1060000u, "A16B16G16R16 float"}};
    for (const auto& f : kPs4) {
        const uint64_t b3 = f.code >> 24, b2 = (f.code >> 16) & 0xff, b1 = (f.code >> 8) & 0xff;
        for (int byte : {6, 5, 4, 2}) {
            const uint64_t code = (b3 << 56) | (b2 << (byte * 8)) | (b1 << 24);
            bool seen = false;
            for (const auto& c : list) seen |= c.format == code;
            if (!seen) list.push_back({code, std::string(f.name) + " (PS4 byte 2 at byte " + std::to_string(byte) + ")"});
        }
        list.push_back({f.code, std::string(f.name) + " (PS4 32-bit code)"});
    }
    return list;
}

// Registers `count` consecutive frames from `frame` as set `set`, buffers
// 0..count-1, as the driver does (wsi_common_videoout.c registers five).
int g_count = 1;
int Register(int handle, int set, void* frame, uint64_t format)
{
    uint8_t attribute[PS5_VIDEO_OUT_ATTRIBUTE_BYTES] = {};
    sceVideoOutSetBufferAttribute2(attribute, format, PS5_VIDEO_OUT_TILING_64KB_R_X, kWidth, kHeight, 0, 0, 0);
    ps5_video_out_buffer buffers[5] = {};
    for (int i = 0; i < g_count; ++i) buffers[i].data = static_cast<uint8_t*>(frame) + std::size_t(i) * kFrameBytes;
    // Each set has its own buffer indices (set * 5 ...), as in the driver: a
    // set still on screen cannot be unregistered, so a later one is another.
    return sceVideoOutRegisterBuffers2(handle, set, set * 5, buffers, g_count, attribute, 0, nullptr);
}

void Show(int handle, int set, unsigned step, const std::string& what)
{
    Log("probe: step " + std::to_string(step) + " on screen for 10 s: " + what);
    if (const int rc = sceVideoOutSubmitFlip(handle, set * 5, PS5_VIDEO_OUT_FLIP_VSYNC, step); rc < 0) {
        char text[64]; std::snprintf(text, sizeof(text), "probe:   flip refused 0x%08x", unsigned(rc)); Log(text);
        return;
    }
    for (int i = 0; i < 10 * 60; ++i) (void)sceVideoOutWaitVblank(handle);
}
} // namespace

// One query's result and the bytes it wrote (up to the last non-zero one).
void Dump(const char* what, int result, const uint8_t* bytes, std::size_t size)
{
    std::size_t used = 0;
    for (std::size_t i = 0; i < size; ++i) if (bytes[i]) used = i + 1;
    char line[160];
    std::snprintf(line, sizeof(line), "probe: %s -> 0x%08x, %zu bytes written", what, unsigned(result), used);
    Log(line);
    for (std::size_t at = 0; at < used; at += 16) {
        std::string row = "probe:   +" + std::to_string(at) + ":";
        for (std::size_t i = at; i < std::min(used, at + 16); ++i) {
            char hex[4]; std::snprintf(hex, sizeof(hex), " %02x", bytes[i]); row += hex;
        }
        Log(row);
    }
}

void DumpOutputQueries(int handle)
{
    alignas(16) static uint8_t buffer[4096];
    auto query = [&](const char* what, auto&& call) {
        std::memset(buffer, 0, sizeof(buffer));
        const int result = call(buffer);
        Dump(what, result, buffer, sizeof(buffer));
    };
    for (const std::size_t size : {std::size_t(0x20), std::size_t(0x40), std::size_t(0x100)}) {
        char what[64];
        std::snprintf(what, sizeof(what), "GetCurrentOutputMode_ size %zu", size);
        query(what, [&](void* b) { return sceVideoOutGetCurrentOutputMode_(handle, b, size); });
        std::snprintf(what, sizeof(what), "ModeSetAny_ size %zu", size);
        query(what, [&](void* b) { return sceVideoOutModeSetAny_(b, size); });
    }
    query("GetOutputStatus", [&](void* b) { return sceVideoOutGetOutputStatus(handle, b, 0x100); });
    query("GetResolutionStatus", [&](void* b) { return sceVideoOutGetResolutionStatus(handle, b, 0x100); });
    query("GetDeviceCapabilityInfo_", [&](void* b) { return sceVideoOutGetDeviceCapabilityInfo_(handle, b, 0x100); });
    query("GetVblankStatus", [&](void* b) { return sceVideoOutGetVblankStatus(handle, b, 0x100); });
}

// The current output mode, with the structure's size field set first.
int CurrentMode(int handle, uint8_t* mode, std::size_t size)
{
    std::memset(mode, 0, size);
    const uint32_t bytes = uint32_t(size);
    std::memcpy(mode, &bytes, 4);
    return sceVideoOutGetCurrentOutputMode_(handle, mode, size);
}

void LogMode(const char* what, int handle)
{
    alignas(16) uint8_t mode[64];
    const int result = CurrentMode(handle, mode, 32);
    Dump(what, result, mode, 32);
}

void Present(int handle, void* frame, unsigned& step, int& previous, uint64_t format, bool tenBit, const std::string& what);

// Asks for output modes that differ from "any" in one byte, and reports what
// the console takes; each taken one gets a 10-bit format search and a pattern.
void ConfigureModes(int handle, void* frame, unsigned& step, int& previous)
{
    alignas(16) uint8_t any[32], options[64] = {};
    (void)sceVideoOutModeSetAny_(any, sizeof(any));
    const uint32_t optionBytes = sizeof(options);
    std::memcpy(options, &optionBytes, 4);
    int optionsUsed = -1;
    for (int variant = 0; variant < 2 && optionsUsed < 0; ++variant) {
        const int result = sceVideoOutConfigureOutputMode_(handle, 0, any, variant ? options : nullptr, sizeof(any), sizeof(options));
        char text[96];
        std::snprintf(text, sizeof(text), "probe: configure 'any' mode, options %s -> 0x%08x", variant ? "zeroed" : "none", unsigned(result));
        Log(text);
        if (result == 0) optionsUsed = variant;
    }
    if (optionsUsed < 0) { Log("probe: ConfigureOutputMode_ refused even 'any'; stopping here"); return; }
    LogMode("current mode after 'any'", handle);
    // PS4 layout guess: bytes 4..7 signal encoding, range, colorimetry, depth.
    for (int byte = 4; byte < 8; ++byte)
        for (uint8_t value = 0; value < 8; ++value) {
            uint8_t mode[32];
            std::memcpy(mode, any, sizeof(mode));
            mode[byte] = value;
            const int result = sceVideoOutConfigureOutputMode_(handle, 0, mode, optionsUsed ? options : nullptr, sizeof(mode), sizeof(options));
            char text[96];
            std::snprintf(text, sizeof(text), "probe: configure byte %d = %u -> 0x%08x", byte, value, unsigned(result));
            Log(text);
            if (result != 0) continue;
            for (int i = 0; i < 120; ++i) (void)sceVideoOutWaitVblank(handle); // the display may resync
            std::snprintf(text, sizeof(text), "current mode with byte %d = %u", byte, value);
            LogMode(text, handle);
            std::string accepted = "probe:   81.. byte 4 now accepted:";
            uint64_t first = 0;
            for (unsigned v = 0; v < 256; ++v) {
                const uint64_t format = (UINT64_C(0x81) << 56) | (uint64_t(v) << 32);
                if (Register(handle, 0, frame, format) == 0) {
                    (void)sceVideoOutUnregisterBuffers(handle, 0);
                    char hex[8]; std::snprintf(hex, sizeof(hex), " %02x", v); accepted += hex;
                    if (v != 0 && v != 6 && !first) first = format;
                }
            }
            Log(accepted);
            Present(handle, frame, step, previous, first ? first : (UINT64_C(0x81) << 56), true,
                    std::string("10-bit pattern, ") + text + (first ? ", new format " + Hex(first) : ""));
            (void)sceVideoOutConfigureOutputMode_(handle, 0, any, optionsUsed ? options : nullptr, sizeof(any), sizeof(options));
        }
    LogMode("current mode at the end", handle);
}

// On screen: sets 1 and 2 in turn, so the one showing is never the one being
// registered; the previous one goes once the next is up.
void Present(int handle, void* frame, unsigned& step, int& previous, uint64_t format, bool tenBit, const std::string& what)
{
    ++step;
    const int set = 1 + int(step % 2);
    Fill(static_cast<uint8_t*>(frame), step, tenBit);
    if (const int result = Register(handle, set, frame, format); result != 0) {
        char text[160];
        std::snprintf(text, sizeof(text), "probe: step %u not shown: %s registration 0x%08x", step, Hex(format).c_str(), unsigned(result));
        Log(text);
        return;
    }
    Show(handle, set, step, what);
    if (previous) (void)sceVideoOutUnregisterBuffers(handle, previous);
    previous = set;
}

void RunVideoOutProbe()
{
    Log("probe: VideoOut HDR probe, build 20 (LO_PS5_VIDEO_PROBE); the game does not start");
    const int handle = sceVideoOutOpen(PS5_VIDEO_OUT_USER_SYSTEM, 0, 0, nullptr);
    {
        char t[64]; std::snprintf(t, sizeof(t), "probe: sceVideoOutOpen -> %d (0x%08x)", handle, unsigned(handle)); Log(t);
    }
    if (handle < 0) return;
    // Build 18: what the console reports about its output, then stop. The
    // format search (builds 16-17: 0x81.. is 10-bit) is LO_PS5_VIDEO_PROBE=2.
    DumpOutputQueries(handle);
    LogMode("current mode (size field set)", handle);
    const bool formatSearch = [] { const char* p = std::getenv("LO_PS5_VIDEO_PROBE"); return p && p[0] == '2'; }();

    // Five frames, as the driver allocates them, before anything else asks
    // the handle a question.
    const std::size_t bytes = 5 * kFrameBytes;
    int64_t physical = -1;
    void* frame = nullptr;
    const int allocated = sceKernelAllocateDirectMemory(0, int64_t(sceKernelGetDirectMemorySize()), bytes, kAlignment,
                                                        kMemoryType, &physical);
    const int mapped = allocated == 0 ? sceKernelMapDirectMemory(&frame, bytes, kProtection, 0, physical, kAlignment) : -1;
    {
        char t[160];
        std::snprintf(t, sizeof(t), "probe: direct memory %zu MiB type %d: allocate 0x%08x physical 0x%llx, map 0x%08x at %p",
                      bytes >> 20, kMemoryType, unsigned(allocated), (unsigned long long)physical, unsigned(mapped), frame);
        Log(t);
    }
    if (allocated != 0 || mapped != 0 || !frame) { (void)sceVideoOutClose(handle); return; }
    std::memset(frame, 0, bytes);
    for (std::size_t at = 0; at < bytes; at += 64) _mm_clflush(static_cast<uint8_t*>(frame) + at);
    _mm_mfence();
    {
        const int rate = sceVideoOutSetFlipRate(handle, 0);
        char t[64]; std::snprintf(t, sizeof(t), "probe: flip rate 0 -> 0x%08x", unsigned(rate)); Log(t);
    }
    // The driver's exact registration, then one buffer.
    int working = 0;
    for (const int count : {5, 1}) {
        g_count = count;
        const int result = Register(handle, 0, frame, kSdrFormat);
        char t[120];
        std::snprintf(t, sizeof(t), "probe: SDR, %d buffer(s) as the driver registers them: 0x%08x", count, unsigned(result));
        Log(t);
        if (result == 0) {
            (void)sceVideoOutUnregisterBuffers(handle, 0);
            if (!working) working = count;
        }
    }
    if (!working) {
        Log("probe: the console refused even the driver's own SDR registration; stopping");
        (void)sceVideoOutClose(handle);
        return;
    }
    g_count = working;

    if (!formatSearch) {
        unsigned step = 0;
        int previous = 0;
        Present(handle, frame, step, previous, kSdrFormat, false, "8-bit SDR bars, for comparison");
        // The PS5's BT.2100 PQ display formats, as titles' own display buffers
        // carry them (AnyPS5 PR #5697): 10-bit class 0x81 with 0x04 in byte 4
        // and 0x07 in byte 5. With HDR "On when supported", a PQ buffer on
        // screen is what switches the output to HDR10.
        for (const auto& [format, order] : {std::pair{UINT64_C(0x8100070400000000), "blue in the low bits"},
                                            std::pair{UINT64_C(0x8100070422000000), "red in the low bits"}}) {
            if (const int result = Register(handle, 0, frame, format); result == 0) {
                (void)sceVideoOutUnregisterBuffers(handle, 0);
                Log("probe: PQ format " + Hex(format) + " (" + order + ") accepted");
            } else {
                char text[96]; std::snprintf(text, sizeof(text), "probe: PQ format %s refused 0x%08x", Hex(format).c_str(), unsigned(result));
                Log(text);
                continue;
            }
            Present(handle, frame, step, previous, format, true, Hex(format) + " PQ (" + order + "), 10-bit HDR pattern");
            LogMode("output status during PQ", handle);
            {
                alignas(16) uint8_t status[64] = {};
                const int result = sceVideoOutGetOutputStatus(handle, status, sizeof(status));
                Dump("GetOutputStatus during PQ", result, status, sizeof(status));
            }
        }
        if (std::getenv("LO_PS5_VIDEO_MODES")) ConfigureModes(handle, frame, step, previous);
        for (int i = 0; i < 2; ++i) (void)sceVideoOutWaitVblank(handle);
        if (previous) (void)sceVideoOutUnregisterBuffers(handle, previous);
        (void)sceVideoOutConfigureOutput(handle, PS5_VIDEO_OUT_MODE_RESTORE, nullptr, nullptr, nullptr);
        (void)sceVideoOutClose(handle);
        Log("probe: done (" + std::to_string(step) + " steps shown); quitting");
        return;
    }
    std::vector<uint32_t> newModes;
    {
        std::string supported = "probe: output modes supported:", refused = "probe: refused:";
        for (uint32_t mode = 0; mode < 64; ++mode) {
            const int rc = sceVideoOutIsOutputSupported(handle, mode, nullptr, nullptr, nullptr);
            char text[32];
            if (rc > 0) {
                std::snprintf(text, sizeof(text), " %u", mode); supported += text;
                if (mode != PS5_VIDEO_OUT_MODE_RESTORE && mode != PS5_VIDEO_OUT_MODE_HIGH_FRAME_RATE) newModes.push_back(mode);
            } else {
                std::snprintf(text, sizeof(text), " %u=%x", mode, unsigned(rc)); refused += text;
            }
        }
        Log(supported); Log(refused);
    }

    // The format code's layout, by search. 0x80290003 is a refused format.
    //   1. each byte alone over the SDR code 0x80.. (byte 7 alone too): build
    //      16 found only byte 3 = 22 (known) and byte 7 = 81;
    //   2. each byte over 0x81..;
    //   3. byte 7 = 80..8f with each value of byte 6.
    std::vector<Candidate> taken;
    auto search = [&](const std::string& label, auto&& formatOf) {
        std::string accepted = "probe: " + label + " accepted:";
        std::map<unsigned, int> errors;
        for (unsigned value = 1; value < 256; ++value) {
            const uint64_t format = formatOf(value);
            const int result = Register(handle, 0, frame, format);
            if (result == 0) {
                (void)sceVideoOutUnregisterBuffers(handle, 0);
                char text[8]; std::snprintf(text, sizeof(text), " %02x", value); accepted += text;
                bool seen = format == kSdrFormat || format == UINT64_C(0x8000000022000000);
                for (const auto& c : taken) seen |= c.format == format;
                if (!seen) taken.push_back({format, label + " = " + std::string(text + 1)});
            } else ++errors[unsigned(result)];
        }
        for (const auto& [code, count] : errors) {
            char text[32]; std::snprintf(text, sizeof(text), " (%d refused %08x)", count, code); accepted += text;
        }
        Log(accepted);
    };
    for (int byte = 0; byte < 8; ++byte)
        search("80.. byte " + std::to_string(byte), [&](unsigned v) {
            return byte == 7 ? uint64_t(v) << 56 : kSdrFormat | (uint64_t(v) << (byte * 8)); });
    for (int byte = 0; byte < 7; ++byte)
        search("81.. byte " + std::to_string(byte), [&](unsigned v) {
            return (UINT64_C(0x81) << 56) | (uint64_t(v) << (byte * 8)); });
    for (unsigned top = 0x80; top <= 0x8f; ++top) {
        char label[32]; std::snprintf(label, sizeof(label), "%02x.. byte 6", top);
        search(label, [&](unsigned v) { return (uint64_t(top) << 56) | (uint64_t(v) << 48); });
    }
    Log("probe: " + std::to_string(taken.size()) + " formats accepted beyond the two known");

    unsigned step = 0;
    int previous = 0;
    auto present = [&](uint64_t format, bool tenBit, const std::string& what) {
        Present(handle, frame, step, previous, format, tenBit, what);
    };
    present(kSdrFormat, false, "8-bit SDR bars, for comparison");
    std::vector<Candidate> shown(taken.begin(), taken.begin() + std::min<size_t>(taken.size(), 6));
    for (const auto& candidate : shown) {
        present(candidate.format, false, Hex(candidate.format) + " (" + candidate.name + ") with 8-bit greys");
        present(candidate.format, true, Hex(candidate.format) + " (" + candidate.name + ") with the 10-bit pattern");
    }
    for (const uint32_t mode : newModes) {
        if (shown.empty()) break;
        const int configured = sceVideoOutConfigureOutput(handle, mode, nullptr, nullptr, nullptr);
        char text[96];
        std::snprintf(text, sizeof(text), "probe: output mode %u configured: 0x%08x", mode, unsigned(configured));
        Log(text);
        if (configured < 0) continue;
        present(shown.front().format, true, "output mode " + std::to_string(mode) + " with " + Hex(shown.front().format));
        (void)sceVideoOutConfigureOutput(handle, PS5_VIDEO_OUT_MODE_RESTORE, nullptr, nullptr, nullptr);
    }
    for (int i = 0; i < 2; ++i) (void)sceVideoOutWaitVblank(handle);
    if (previous) (void)sceVideoOutUnregisterBuffers(handle, previous);
    (void)sceVideoOutClose(handle);
    Log("probe: done (" + std::to_string(step) + " steps shown); quitting");
}
} // namespace os::ps5
#endif
