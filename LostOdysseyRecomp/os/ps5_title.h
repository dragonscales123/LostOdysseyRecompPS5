#pragma once

#include <os/platform.h>

#if LO_PLATFORM_PS5
#include <filesystem>

// PlayStation 5 homebrew title setup (docs/PS5.md).
namespace os::ps5
{
// The title's own folder while it runs: /data/homebrew/<TITLE_ID>/ mounted at
// /app0. Writable, and what the console's FTP server shows.
std::filesystem::path TitleDirectory();

// Run first in main: standard error to klog (and the console's debug output),
// the IEEE floating-point state other x86-64 systems start in (a title starts
// with flush-to-zero and denormals-are-zero, and the platform's pthread_create
// copies the creator's state to each thread), the working directory set to
// the title folder (a title has none worth trusting), and Vulkan resolved
// through the RADV linked into the title (volk, which plume uses).
void InitializeTitle();

// One line to /app0/boot.txt (opened afresh by the first line of a run,
// written through and closed each time, so it survives a crash) and to klog.
// Safe before main and before the runtime's log exists.
void Breadcrumb(const char* line);

// LO_PS5_VIDEO_PROBE=1: what the display output accepts for HDR, logged to
// boot.txt, with test patterns on screen (os/ps5_video_probe.cpp). The title
// quits afterwards.
void RunVideoOutProbe();

// The peak luminance (nits) set in the console's Settings > Screen and Video >
// Adjust HDR, which titles are meant to tone map to; 0 when unavailable. The
// first call logs the raw answer.
float SystemHdrPeakNits();

// Guest thread placement (LO_PS5_PIN, default 1). The game keeps several
// worker threads spinning; on the console's SMT cores a spinner on the main
// thread's sibling slows it. 0: no placement. 1: every guest thread except
// the main one is kept off the main core (the first core of the title and its
// SMT sibling, found by timing). 2: as 1, and the main thread is held on that
// core. Call PlaceGuestMainThread on the guest main thread before it runs and
// PlaceGuestWorkerThread on each other guest thread.
void PlaceGuestMainThread();
void PlaceGuestWorkerThread(uint32_t function, uint32_t flags);
}
#endif
