// LoShaderCacheBuilder: builds the PS5 title's built-in shader cache
// (cache/shaders/builtin_v28_spirv.lostore) from the user's own game discs,
// on a PC, so the console (which has no shader compiler) finds every shader
// the runtime would otherwise compile while playing. docs/PS5.md.
//
//   1. default.xex (disc1): decrypted and decompressed as the game loads it
//      (XenonUtils, as tools/xexdump), for the four shaders it holds;
//   2. disc1..disc4: every shader in the .fpd archives (the runtime's resource
//      scanner, decoding every package);
//   3. LoShaderPackTool builtin-cache: the runtime's own shaders plus those it
//      derives from each game shader, compiled with the bundled DXC.
//
// The extracted game shaders go to a temporary folder beside the output and
// are deleted at the end (--keep-sources keeps them); nothing is uploaded.
#include "gpu/shader/resource_scan.h"
#include "gpu/shader/resource_xex.h"
#include "../shader_pack/builtin_requests.h"
#include <file.h>
#include <image.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void Usage()
{
    std::cerr <<
        "Usage: LoShaderCacheBuilder <game folder> [output folder] [--no-motion-replay] [--keep-sources]\n"
        "  <game folder>  the folder holding disc1 (and disc2..disc4), or disc1 itself\n"
        "  output folder  default: shader-cache beside this program\n"
        "  --no-motion-replay  leave out TAA's motion-replay shaders (about 28,000; a\n"
        "                      much smaller file, and TAA then runs without geometric motion)\n"
        "Writes <output>/cache/shaders/builtin_v28_spirv.lostore. Copy the cache folder\n"
        "into the title folder (/data/homebrew/PPSA99212/).\n";
}

static bool WriteFile(const fs::path& path, std::span<const uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out.flush());
}

// Arguments as paths. On Windows, from the UTF-16 command line, so folders
// with any characters work (argv holds the ANSI code page).
static std::vector<fs::path> Arguments(int argc, char** argv)
{
    std::vector<fs::path> out;
#ifdef _WIN32
    int count = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 0; i < count; ++i) out.emplace_back(wide[i]);
        LocalFree(wide);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.push_back(fs::u8path(argv[i]));
    return out;
}

int main(int argc, char** argv)
{
    const auto arguments = Arguments(argc, argv);
    fs::path game, output;
    bool motionReplay = true, keepSources = false, skipScan = false;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string arg = arguments[i].string();
        if (arg == "--no-motion-replay") motionReplay = false;
        else if (arg == "--keep-sources") keepSources = true;
        else if (arg == "--skip-scan") skipScan = true; // testing: executable shaders only
        else if (arg == "-h" || arg == "--help") { Usage(); return 0; }
        else if (game.empty()) game = arguments[i];
        else if (output.empty()) output = arguments[i];
        else { Usage(); return 2; }
    }
    if (game.empty()) { Usage(); return 2; }
    std::error_code ec;
    if (fs::is_directory(game / "disc1", ec)) game /= "disc1";
    if (!fs::is_regular_file(game / "default.xex", ec)) {
        std::cerr << "No default.xex in " << game.string() << ": point this at the folder holding disc1.\n";
        return 1;
    }
    if (output.empty()) output = fs::absolute(arguments[0]).parent_path() / "shader-cache";
    const auto cache = output / "cache" / "shaders";
    const auto sources = output / "game-shaders.tmp";
    fs::create_directories(cache);
    fs::remove_all(sources, ec);
    fs::create_directories(sources);
    // The extracted game shaders never outlive the run, whatever its outcome.
    struct Cleanup {
        fs::path path; bool keep;
        ~Cleanup() { if (!keep) { std::error_code e; fs::remove_all(path, e); } }
    } cleanup{sources, keepSources};
    const auto started = std::chrono::steady_clock::now();
    std::set<std::string> written;
    size_t vertex = 0, pixel = 0;
    const xenos::resources::SourceSink sink = [&](bool isPixel, std::span<const uint8_t> code) {
        const auto name = xenos::resources::SourceName(isPixel, code);
        if (!written.insert(name).second) return;
        (isPixel ? pixel : vertex)++;
        if (!WriteFile(sources / name, code)) throw std::runtime_error("cannot write " + (sources / name).string());
    };

    std::cout << "1/3 Reading " << (game / "default.xex").string() << "\n";
    {
        const auto file = LoadFile(game / "default.xex");
        auto image = Image::ParseImage(file.data(), file.size());
        if (!image.data) { std::cerr << "Could not decrypt default.xex.\n"; return 1; }
        const auto found = xenos::resources::ExtractXexShaders(
            std::span<const uint8_t>(image.data.get(), image.size), sources, sink);
        std::cout << "    " << found << " shaders in the executable" << (found ? "" : " (not the supported edition?)") << "\n";
    }

    std::cout << "2/3 Scanning the discs (about a minute per disc)\n";
    if (!skipScan) try {
        int lastPercent = -10;
        const auto result = xenos::resources::Scan(game, output / "scan-cache.tmp",
            [&](const xenos::resources::ScanProgress& p) {
                if (!p.total || p.unit != xenos::resources::ScanUnit::Entries) return;
                const int percent = int(p.completed * 100 / p.total);
                if (percent >= lastPercent + 10) { lastPercent = percent - percent % 10; std::cout << "    " << lastPercent << "%\n"; }
            }, {}, {}, {}, /*strict=*/true, sink);
        if (!result.error.empty()) { std::cerr << "Scan failed: " << result.error << "\n"; return 1; }
        std::cout << "    " << result.scannedFiles << " archives, " << result.cpxPackages << " packages\n";
    } catch (const std::exception& e) {
        std::cerr << "Scan failed: " << e.what() << "\n";
        return 1;
    }
    fs::remove_all(output / "scan-cache.tmp", ec);
    std::cout << "    " << written.size() << " unique game shaders (" << vertex << " vertex, " << pixel << " pixel)\n";

    std::cout << "3/3 Compiling (this takes a while; the count updates every 2000 shaders)\n";
    std::vector<std::string> args{"LoShaderPackTool", "builtin-cache", cache.string(), "--sources", sources.string(), "--expand"};
    if (!motionReplay) args.push_back("--no-motion-replay");
    std::vector<char*> raw;
    for (auto& arg : args) raw.push_back(arg.data());
    const int status = BuiltinCache(int(raw.size()), raw.data());

    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - started).count();
    if (status != 0) { std::cerr << "Some shaders failed to compile (see above).\n"; return status; }
    const auto store = cache / "builtin_v28_spirv.lostore";
    std::cout << "\nDone in " << minutes << " min: " << store.string() << " (" << fs::file_size(store, ec) / 1048576 << " MiB)\n"
              << "Copy it to /data/homebrew/PPSA99212/cache/shaders/ on the console.\n";
    return 0;
}
