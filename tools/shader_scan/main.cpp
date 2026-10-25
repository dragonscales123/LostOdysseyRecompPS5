// LoShaderScan: extracts every Xenos shader the runtime's resource scanner
// finds in a disc layout (disc1..disc4 beside each other), without a GPU,
// a compiler or the game running. Writes <out>/source/{vs,ps}_<hash>.bin (the
// renderer-byte FNV-1a names LoShaderPackTool merge expects) and
// <out>/sources.tsv. By default it scans as the runtime does: the built-in
// location index finds known shaders, and any package the index does not
// know is decoded in full. --full decodes every package (LO_SHADER_FULL_SCAN).
#include "gpu/shader/resource_scan.h"
#include <cstdio>
#include <iostream>
#include <map>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    const bool full = argc == 4 && std::string_view(argv[1]) == "--full";
    if (full) { --argc; ++argv; }
    if (argc != 3) {
        std::cerr << "Usage: LoShaderScan [--full] <disc1 folder> <output folder>\n"
                     "  disc2..disc4 next to disc1 are scanned too.\n";
        return 2;
    }
    const fs::path root = argv[1], out = argv[2];
    fs::create_directories(out / "source");
    std::map<std::string, size_t> found;  // name -> bytes
    size_t pixel = 0, vertex = 0;
    const xenos::resources::SourceSink sink = [&](bool isPixel, std::span<const uint8_t> code) {
        const auto name = xenos::resources::SourceName(isPixel, code);
        if (!found.emplace(name, code.size()).second) return;
        (isPixel ? pixel : vertex)++;
        std::FILE* f = std::fopen((out / "source" / name).c_str(), "wb");
        if (!f || std::fwrite(code.data(), 1, code.size(), f) != code.size()) {
            std::cerr << "write failed: " << name << "\n";
            std::exit(1);
        }
        std::fclose(f);
    };
    int lastPercent = -1;
    const auto result = xenos::resources::Scan(root, out / "scan-cache",
        [&](const xenos::resources::ScanProgress& p) {
            if (!p.total) return;
            const int percent = int(p.completed * 100 / p.total);
            if (percent / 10 != lastPercent / 10) { std::cerr << "  " << percent << "%\n"; lastPercent = percent; }
        },
        full ? std::span<const xenos::resources::IndexFile>{} : std::span<const xenos::resources::IndexFile>{xenos::resources::builtin::files},
        full ? std::span<const xenos::resources::CpxIndexPackage>{} : std::span<const xenos::resources::CpxIndexPackage>{xenos::resources::builtin::cpxPackages},
        full ? std::span<const xenos::resources::CpxIndexArchive>{} : std::span<const xenos::resources::CpxIndexArchive>{xenos::resources::builtin::cpxArchives},
        /*strict=*/full, sink);
    if (!result.error.empty()) { std::cerr << "scan error: " << result.error << "\n"; return 1; }
    std::FILE* list = std::fopen((out / "sources.tsv").c_str(), "w");
    for (const auto& [name, bytes] : found) std::fprintf(list, "%s\t%zu\n", name.c_str(), bytes);
    std::fclose(list);
    std::cout << "files scanned: " << result.scannedFiles << ", bytes read: " << result.bytesRead
              << ", indexed files: " << result.indexedFiles << ", CPX packages: " << result.cpxPackages
              << " (indexed " << result.indexedPackages << ", decoded in full " << result.fallbackPackages
              << ", duplicates " << result.duplicatePackages << ")\n"
              << "unique shaders: " << found.size() << " (" << vertex << " vertex, " << pixel << " pixel)\n";
    return 0;
}
