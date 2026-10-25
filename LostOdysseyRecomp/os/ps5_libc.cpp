#include <os/platform.h>

#if LO_PLATFORM_PS5
#include <cerrno>
#include <cstddef>
#include <unistd.h>

// libc functions the console's modules do not export to a title and the PS5
// platform layer does not supply, but that linked code names.
extern "C"
{
// XenonUtils' LZX decoder (xex_patcher.cpp).
int ffs(int value)
{
    return __builtin_ffs(value);
}

// FFmpeg's avpriv_tempfile without mkstemp; the XMA decoder never asks for a
// temporary file, and a failure here is reported, not fatal.
char* tempnam(const char*, const char*)
{
    errno = ENOSYS;
    return nullptr;
}

// Only libkernel_sys exports pathconf, which a title does not load (its import
// would be null). libc++'s filesystem asks it for name and path lengths; the
// console's filesystems take FreeBSD's limits.
long pathconf(const char*, int name)
{
    switch (name)
    {
    case _PC_NAME_MAX: return 255;
    case _PC_PATH_MAX: return 1024;
    case _PC_LINK_MAX: return 32767;
    default:
        errno = EINVAL;
        return -1;
    }
}
}
#endif
