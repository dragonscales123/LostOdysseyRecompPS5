#pragma once

// Host platform identification for the runtime. Every macro is always defined
// as 0 or 1, so platform code tests `#if LO_PLATFORM_POSIX` rather than raw
// compiler macros. Use LO_PLATFORM_POSIX for code that relies only on POSIX
// APIs; use a specific platform only for APIs unique to it (memfd, Mach VM).
#if defined(_WIN32)
#define LO_PLATFORM_WINDOWS 1
#define LO_PLATFORM_LINUX 0
#define LO_PLATFORM_MACOS 0
#define LO_PLATFORM_PS5 0
#elif defined(__PROSPERO__)
// PlayStation 5 homebrew built with the PS5 payload SDK (FreeBSD-based libc,
// static title, no window system; see docs/PS5.md). Tested before __linux__,
// which the SDK does not define either.
#define LO_PLATFORM_WINDOWS 0
#define LO_PLATFORM_LINUX 0
#define LO_PLATFORM_MACOS 0
#define LO_PLATFORM_PS5 1
#elif defined(__linux__)
#define LO_PLATFORM_WINDOWS 0
#define LO_PLATFORM_LINUX 1
#define LO_PLATFORM_MACOS 0
#define LO_PLATFORM_PS5 0
#elif defined(__APPLE__)
#define LO_PLATFORM_WINDOWS 0
#define LO_PLATFORM_LINUX 0
#define LO_PLATFORM_MACOS 1
#define LO_PLATFORM_PS5 0
#else
#error "Unsupported host platform"
#endif

#if defined(__ANDROID__)
#define LO_PLATFORM_ANDROID 1
#else
#define LO_PLATFORM_ANDROID 0
#endif

// Android also uses the Linux/POSIX memory and scheduling implementations.
// Desktop-only process, updater and storage paths test Android separately.
// The PS5 uses the POSIX paths where its libc has them; code that needs what
// the console lacks (processes, getcwd, a window system, dlopen) tests
// LO_PLATFORM_PS5 separately.
#define LO_PLATFORM_POSIX (LO_PLATFORM_LINUX || LO_PLATFORM_MACOS || LO_PLATFORM_PS5)
