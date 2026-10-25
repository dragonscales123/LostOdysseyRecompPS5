// LoShaderPackTool builtin-cache: compiles every shader the runtime itself
// compiles through CompileCachedHlsl (presentation, AA, AO, blits, movie,
// motion replay, ...) into a shader cache, so a runtime without DXC (the PS5)
// finds them all without first running into each one. With source folders it
// also adds TAA's motion-replay shaders, which the runtime derives from each
// game shader it replays: one vertex and one pixel program per game shader.
//
// The fixed sources come from the runtime's own headers, and from its
// function-local literals copied verbatim by extract_builtin_sources.py; the
// composition below mirrors each call site. --dump writes each fixed request
// as <runtime key>-<entry>-<profile>-spirv.hlsl so it can be compared with
// the requests a console saved.
#include "builtin_requests.h"
#include "builtin_sources.inc"

#include "gpu/shader/dxc_compiler.h"
#include "gpu/shader/xenos_translator.h"
#include "gpu/shader/motion_replay_hlsl.h"
#include "gpu/shader/ambient_occlusion_hlsl.h"
#include "gpu/shader/rect_list_hlsl.h"
#include "gpu/hdr_scene_shader.h"
#include "gpu/shader/resource_variants.h"
#include "gpu/shader/resource_variant_links.h"
#include "gpu/shader/fsr_alpha_postprocess_hlsl.h"
#include "gpu/bloom_prefilter.h"
#include "gpu/movie_yuv_shader.h"
#include "gpu/edram_transfer.h"
#include "gpu/scene_copy_promotion_shaders.h"
#include "gpu/sr_hybrid_motion.h"
#include "../../thirdparty/smaa/SMAA_source.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
// --no-motion-replay leaves out TAA's per-game-shader replay programs.
bool replayFamily = true;

struct Request {
    std::string source, entry, profile, origin;
};

std::string Smaa()
{
    // smaa_pipeline.h, SmaaPipeline::Init with useVulkan (the PS5 renderer).
    std::string hlsl = kSmaaPrefix;
    std::string smaa = smaaSource;
    const auto linearPos = smaa.find("SamplerState LinearSampler");
    smaa.insert(linearPos, "[[vk::binding(3,0)]] ");
    const auto pointPos = smaa.find("SamplerState PointSampler");
    smaa.insert(pointPos, "[[vk::binding(4,0)]] ");
    hlsl += smaa;
    hlsl += kSmaaPasses;
    return hlsl;
}

std::vector<Request> FixedRequests()
{
    std::vector<Request> r;
    auto add = [&](std::string source, const char* entry, const char* profile, const char* origin) {
        r.push_back({std::move(source), entry, profile, origin});
    };
    // presentation.cpp: the final pass, its compute form, the UI composite, overlays.
    for (const char* entry : {"vertex", "pixel", "gainPixel"})
        add(kPresentSource, entry, entry == std::string("vertex") ? "vs_6_0" : "ps_6_0", "presentation");
    add(std::string(kPresentSource) + "\n#define PRESENT_GROUP_X 16\n#define PRESENT_GROUP_Y 16\n" + kPresentCompute,
        "presentCompute", "cs_6_0", "presentation compute");
    add(kPresentUi, "vertexUi", "vs_6_0", "presentation UI");
    add(kPresentUi, "pixelUi", "ps_6_0", "presentation UI");
    add(kPresentOverlay, "vertexOverlay", "vs_6_0", "overlay");
    add(kPresentOverlay, "pixelOverlay", "ps_6_0", "overlay");
    // temporal_aa.cpp
    add(kTaaSource, "vertex", "vs_6_0", "TAA");
    add(kTaaSource, "pixel", "ps_6_0", "TAA");
    add(kTaaSource, "displayPixel", "ps_6_0", "TAA");
    // renderer.cpp
    add(std::string(kTransferBindings) + gpu::edram_transfer::kTransferHlsl, "main", "ps_6_0", "EDRAM transfer");
    add(kBlitVs, "main", "vs_6_0", "blit");
    add(kBlitPs, "main", "ps_6_0", "blit");
    add(gpu::bloom_prefilter::PixelShader, "main", "ps_6_0", "bloom prefilter");
    add(gpu::scene_copy_promotion::RgbaShader, "main", "ps_6_0", "scene copy promotion");
    add(gpu::scene_copy_promotion::RgbShader, "main", "ps_6_0", "scene copy promotion");
    add(kRectListGs, "main", "gs_6_0", "rect list");
    add(gpu::movie_yuv::PixelShader, "main", "ps_6_0", "movie");
    // smaa_pipeline.h
    const auto smaa = Smaa();
    for (const char* entry : {"edge", "weight", "neighborhood"}) add(smaa, entry, "ps_6_0", "SMAA");
    // ambient_occlusion.cpp
    add(gpu::ao::Shader, "vertex", "vs_6_0", "AO");
    for (const char* entry : {"visibility", "composite", "compositePair"}) add(gpu::ao::Shader, entry, "ps_6_0", "AO");
    // motion_replay_gpu.h: the depth mask, the camera mask and fill, and the
    // replay program for draws without pixel microcode.
    add(kMvMaskShader, "vertex", "vs_6_0", "motion mask");
    add(kMvMaskShader, "pixel", "ps_6_0", "motion mask");
    const std::string mask = std::string(kMvCameraMaskShader) + kMvCameraHlsl + kMvCameraMaskBody;
    const std::string fill = std::string(kMvCameraFillShader) + kMvCameraHlsl + kMvCameraFillBody;
    add(mask, "vertex", "vs_6_0", "camera motion mask");
    add(mask, "pixel", "ps_6_0", "camera motion mask");
    add(fill, "vertex", "vs_6_0", "camera motion fill");
    add(fill, "pixel", "ps_6_0", "camera motion fill");
    add(xenos::motion_replay::Pixel(nullptr), "main", "ps_6_0", "motion replay (no pixel shader)");
    // sr_hybrid_motion_gpu.h, fsr_alpha_postprocess_gpu.h (area mask).
    add(gpu::temporal::kSrHybridMotionShader, "vertex", "vs_6_0", "hybrid motion");
    add(gpu::temporal::kSrHybridMotionShader, "pixel", "ps_6_0", "hybrid motion");
    add(xenos::fsr_alpha_postprocess::AreaMaximum, "main", "ps_6_0", "FSR area mask");
    return r;
}

void SetEnvironment(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// dxc_compiler.cpp CompileCachedHlsl: the built-in store's key.
uint64_t BuiltinKey(const Request& q)
{
    const std::string key = q.source + '\0' + q.entry + '\0' + q.profile + "lo-dxc-vulkan12-dx-layout-v1";
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint8_t byte : key) { hash ^= byte; hash *= 0x100000001b3ull; }
    return hash;
}

// The shaders the runtime derives from one game shader through
// CompileCachedHlsl (renderer.cpp and motion_replay_gpu.h). Each starts from
// the big-endian renderer bytes swapped to host words, as the runtime does.
//   motion replay (TAA): TranslateShader with its default constants, then
//     motion_replay::Vertex/Pixel (motion_replay_gpu.h GetModule);
//   rect list: rect_list::Vertex of the vertex shader (PrepareRectListShader);
//   HDR tone map: hdr_scene::RewriteTonemap of the pixel shader (HdrToneShader).
// The renderer's uboConstants is false on the PS5: its pack and stores use
// push constants (the rect-list request a console saved checks this).
size_t DerivedRequests(const std::string& name, bool pixel, std::span<const uint8_t> bytes, std::vector<Request>& out)
{
    if (bytes.size() < 12 || bytes.size() % 4) return 0;
    std::vector<uint32_t> words(bytes.size() / 4);
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = (uint32_t(bytes[i * 4]) << 24) | (uint32_t(bytes[i * 4 + 1]) << 16) |
                   (uint32_t(bytes[i * 4 + 2]) << 8) | bytes[i * 4 + 3];
    const auto count = uint32_t(words.size());
    const size_t before = out.size();
    const char* profile = pixel ? "ps_6_0" : "vs_6_0";
    const auto translated = xenos::TranslateShader(words.data(), count, pixel);
    auto replay = pixel ? xenos::motion_replay::Pixel(&translated) : xenos::motion_replay::Vertex(translated);
    if (replayFamily && !replay.empty()) out.push_back({std::move(replay), "main", profile, "motion replay " + name});
    if (!pixel) {
        auto rect = xenos::rect_list::Vertex(xenos::TranslateShader(words.data(), count, false, false));
        if (!rect.empty()) out.push_back({std::move(rect), "main", "vs_6_0", "rect list " + name});
    } else {
        auto tone = xenos::TranslateShader(words.data(), count, true, false);
        if (tone.errors.empty() && gpu::hdr_scene::RewriteTonemap(tone.hlsl))
            out.push_back({std::move(tone.hlsl), "main", "ps_6_0", "HDR tone map " + name});
    }
    return out.size() - before;
}

size_t DerivedRequests(const fs::path& file, std::vector<Request>& out)
{
    const auto name = file.filename().string();
    const bool pixel = name.starts_with("ps_");
    if (!pixel && !name.starts_with("vs_")) return 0;
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    return DerivedRequests(name, pixel, bytes, out);
}
} // namespace

int BuiltinCache(int argc, char** argv)
{
    std::vector<std::string> args(argv + 2, argv + argc);
    fs::path dump;
    bool compile = true, expand = false;
    replayFamily = true;
    std::vector<fs::path> sourceDirs;
    std::string cache;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--dump" && i + 1 < args.size()) dump = args[++i];
        else if (args[i] == "--no-compile") compile = false;
        else if (args[i] == "--expand") expand = true;
        else if (args[i] == "--no-motion-replay") replayFamily = false;
        else if (args[i] == "--sources" && i + 1 < args.size()) sourceDirs.push_back(args[++i]);
        else if (cache.empty()) cache = args[i];
        else { cache.clear(); break; }
    }
    if (cache.empty()) {
        std::cerr << "Usage: LoShaderPackTool builtin-cache <shader-cache-dir> [--sources <folder of vs_/ps_*.bin>]... [--dump <dir>]\n";
        return 2;
    }
    SetEnvironment("LO_SHADER_CACHE_DIR", cache);

    auto requests = FixedRequests();
    const size_t fixed = requests.size();
    size_t gameShaders = 0;
    for (const auto& dir : sourceDirs)
        for (const auto& entry : fs::directory_iterator(dir))
            if (entry.path().extension() == ".bin") { ++gameShaders; DerivedRequests(entry.path(), requests); }
    // The renderer's source expansion (renderer.cpp): vertex shaders with
    // patched fetches, made from the folder's bases. They get the same
    // derived shaders as any other vertex shader.
    size_t expanded = 0;
    if (expand)
        for (const auto& dir : sourceDirs) {
            std::set<uint64_t> fixedHashes, seen;
            auto keep = [&](uint64_t hash, std::span<const uint8_t> code) {
                if (!seen.insert(hash).second) return;
                ++expanded;
                std::ostringstream name;
                name << "vs_" << std::hex << std::setw(16) << std::setfill('0') << hash << ".bin (variant)";
                DerivedRequests(name.str(), false, code, requests);
            };
            const auto fixedResult = xenos::resources::variants::GenerateFixedVariants(dir,
                [&](uint64_t hash, std::span<const uint8_t> code) { fixedHashes.insert(hash); keep(hash, code); });
            const auto linked = xenos::resources::variants::GenerateLinkedVariants(dir, keep, {}, {}, &fixedHashes);
            std::cout << "expansion: " << fixedResult.generated << " fixed VS (" << fixedResult.verifiedBases
                      << " bases, " << fixedResult.missingBases << " missing), " << linked.generated << " linked VS\n";
        }
    size_t replay = 0, rect = 0, tone = 0;
    for (size_t i = fixed; i < requests.size(); ++i) {
        const auto& origin = requests[i].origin;
        (origin.starts_with("motion") ? replay : origin.starts_with("rect") ? rect : tone)++;
    }
    std::cout << fixed << " fixed built-in shaders; from " << gameShaders << " game shaders: " << replay
              << " motion-replay, " << rect << " rect-list, " << tone << " HDR tone-map (" << expanded
              << " expanded vertex shaders included)\n";
    std::sort(requests.begin() + fixed, requests.end(), [](const Request& a, const Request& b) { return a.origin < b.origin; });

    if (!dump.empty()) {
        fs::create_directories(dump);
        for (size_t i = 0; i < requests.size(); ++i) {
            const auto& q = requests[i];
            const auto key = BuiltinKey(q);
            std::ostringstream name;
            name << std::hex << std::setw(16) << std::setfill('0') << key << '-' << q.entry << '-' << q.profile << "-spirv.hlsl";
            std::ofstream(dump / name.str(), std::ios::binary) << q.source;
        }
    }

    std::cout.flush();
    if (!compile) return 0;
    std::atomic<size_t> next{0}, compiled{0}, failed{0};
    std::mutex log;
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (size_t i; (i = next++) < requests.size();) {
                const auto& q = requests[i];
                const auto result = xenos::CompileCachedHlsl(q.source, q.entry.c_str(), q.profile.c_str(),
                                                             xenos::ShaderBinaryFormat::Spirv);
                if (result.ok) ++compiled;
                else {
                    ++failed;
                    std::lock_guard lock(log);
                    std::cerr << "FAILED " << q.origin << " " << q.entry << " " << q.profile << ": "
                              << result.errors.substr(0, 400) << "\n";
                }
                const size_t done = compiled + failed;
                if (done % 2000 == 0) { std::lock_guard lock(log); std::cout << "  " << done << "/" << requests.size() << "\n"; }
            }
        });
    for (auto& t : pool) t.join();
    std::cout << compiled << " compiled, " << failed << " failed, into " << cache << "\n";
    return failed ? 1 : 0;
}
