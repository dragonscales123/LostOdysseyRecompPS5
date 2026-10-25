#if defined(__PROSPERO__)
#include "http.h"
#include "progress.h"

// PS5 homebrew: no HTTP client yet (the console's sceHttp2 is the route a
// later change can take). Every request fails with a reason the callers log,
// so the update check is skipped and the shader pack and pipeline corpus are
// placed by hand (docs/PS5.md).
namespace updater
{
namespace
{
constexpr const char *Unavailable = "network downloads are not available on PS5; copy the file by hand";
}

bool ReadResponse(std::string_view, size_t, std::string &body, std::string &error)
{
    body.clear();
    error = Unavailable;
    return false;
}

bool DownloadFile(std::string_view, const std::filesystem::path &, uint64_t, const DownloadProgress &,
                  std::string &error, bool &cancelled)
{
    cancelled = false;
    error = Unavailable;
    return false;
}

bool Download(std::string_view, const std::filesystem::path &, uint64_t, ProgressWindow &, std::string &error,
              bool &cancelled)
{
    cancelled = false;
    error = Unavailable;
    return false;
}
}
#endif
