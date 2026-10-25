#include "gpu/shader/portable_shader_contract.h"
#include "gpu/shader/portable_shader_pack_location.h"
#include <algorithm>
#include "merge.h"
#include "builtin_requests.h"
#include "gpu/shader/dxc_compiler.h"
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

// The runtime contract hashes the image as parsed, before import binding,
// which is what xexdump writes, so this is the value the game computes.
static std::vector<uint8_t> ReadRuntimeImage(const char* argument) {
    std::ifstream in(std::filesystem::path(reinterpret_cast<const char8_t*>(argument)), std::ios::binary);
    std::vector<uint8_t> image(xenos::portable_pack::RuntimeXexBytes);
    if (!in.read(reinterpret_cast<char*>(image.data()), std::streamsize(image.size())))
        throw std::runtime_error("missing/short decrypted runtime image (use xexdump output)");
    return image;
}

int main(int argc,char** argv) try {
    const auto command = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    if (command == "merge") return Merge(argc, argv);
    if (command == "builtin-cache") return BuiltinCache(argc, argv);
    if (command == "missing" && argc == 6) {
        // Which game shaders of a source folder (vs_/ps_<hash>.bin, as
        // LoShaderScan writes them) the pack lacks, as a merge manifest
        // whose source paths are relative to the manifest's folder.
        namespace pack = xenos::portable_pack;
        const auto image = ReadRuntimeImage(argv[3]);
        const auto contract = pack::RuntimeContract(image,
            xenos::cache::MakeIdentity(xenos::cache::Backend::Vulkan, "", xenos::cache::UboConstantsRequested()));
        pack::Reader reader(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2])), contract);
        const std::filesystem::path sources = reinterpret_cast<const char8_t*>(argv[4]);
        const std::filesystem::path manifest = reinterpret_cast<const char8_t*>(argv[5]);
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(sources)) files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        std::ofstream out(manifest);
        out << "action\tstage\thash\tsource\tprovenance\n";
        size_t present = 0, missing = 0;
        for (const auto& file : files) {
            const auto name = file.filename().string();
            if (name.size() != 23 || (!name.starts_with("vs_") && !name.starts_with("ps_")) || !name.ends_with(".bin")) continue;
            const bool pixel = name.starts_with("ps_");
            const uint64_t hash = std::stoull(name.substr(3, 16), nullptr, 16);
            if (reader.Contains(pixel, hash)) { ++present; continue; }
            ++missing;
            out << "include\t" << name.substr(0, 2) << "\t" << name.substr(3, 16) << "\t"
                << std::filesystem::relative(file, manifest.parent_path()).generic_string() << "\tdisc scan\n";
        }
        std::cout << present << " in the pack, " << missing << " missing (" << manifest.string() << ")\n";
        return 0;
    }
    if (command == "compile-requests" && argc == 4) {
        // Built-in shaders a runtime without a compiler (the PS5) saved in
        // <cache>/requests/: compile each into the cache's host shader store,
        // keyed exactly as the runtime looks it up (docs/PS5.md).
        const std::filesystem::path requests = std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2]));
        const std::string cache = argv[3];
        setenv("LO_SHADER_CACHE_DIR", cache.c_str(), 1);
        int compiled = 0, failed = 0;
        for (const auto& entry : std::filesystem::directory_iterator(requests)) {
            if (entry.path().extension() != ".hlsl") continue;
            // <hash>-<entry>-<profile>-<spirv|dxil>.hlsl
            std::string stem = entry.path().stem().string();
            std::vector<std::string> parts;
            for (int i = 0; i < 3; ++i) {
                const auto dash = stem.rfind('-');
                if (dash == std::string::npos) break;
                parts.insert(parts.begin(), stem.substr(dash + 1));
                stem.resize(dash);
            }
            if (parts.size() != 3) { std::cerr << "skipping " << entry.path() << "\n"; continue; }
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream text; text << in.rdbuf();
            const auto format = parts[2] == "spirv" ? xenos::ShaderBinaryFormat::Spirv : xenos::ShaderBinaryFormat::Dxil;
            const auto result = xenos::CompileCachedHlsl(text.str(), parts[0].c_str(), parts[1].c_str(), format);
            std::cout << (result.ok ? "compiled " : "FAILED   ") << entry.path().filename().string()
                      << " (" << result.bytecode.size() << " bytes)\n";
            (result.ok ? compiled : failed)++;
        }
        std::cout << compiled << " compiled, " << failed << " failed, into " << cache << "\n";
        return failed ? 1 : 0;
    }
    if (command == "contract" && argc == 3) {
        // The contract each renderer's startup download looks up in index.json.
        // "vulkan" also covers Android and Metal on macOS.
        namespace pack = xenos::portable_pack;
        const auto image = ReadRuntimeImage(argv[2]);
        const char* separator = "{\n";
        for (const auto flavor : {pack::Flavor::Vulkan, pack::Flavor::D3D12}) {
            std::cout << separator << "  \"" << pack::FlavorName(flavor) << "\": \""
                      << xenos::resources::Sha256Hex(pack::FlavorContract(image, flavor)) << "\"";
            separator = ",\n";
        }
        std::cout << "\n}\n";
        return 0;
    }
    const bool runtime = command == "verify-runtime";
    if ((runtime ? argc != 4 : argc != 3) ||
        (command != "inspect" && command != "verify" && !runtime)) {
        std::cerr << "Usage: LoShaderPackTool <inspect|verify> pack.lospv|pack.lospd\n"
                     "       LoShaderPackTool verify-runtime pack.lospv|pack.lospd xexdump-image.bin\n"
                     "       LoShaderPackTool contract xexdump-image.bin\n"
                     "       LoShaderPackTool merge baseline.lospv xexdump-image.bin manifest.tsv output-dir\n"
                     "       LoShaderPackTool compile-requests requests-dir shader-cache-dir\n"
                     "       LoShaderPackTool builtin-cache shader-cache-dir [--sources dir]... [--dump dir]\n";
        return 2;
    }
    const bool verified = command != "inspect";
    const auto path = std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2]));
    xenos::portable_pack::Report r;
    if (runtime) {
        const auto format = xenos::portable_pack::Reader::Inspect(path).format;
        const auto image = ReadRuntimeImage(argv[3]);
        const auto backend = format == xenos::portable_pack::PackFormat::Dxil ?
            xenos::cache::Backend::D3D12 : xenos::cache::Backend::Vulkan;
        const auto identity = xenos::cache::MakeIdentity(backend, "", xenos::cache::UboConstantsRequested());
        const auto expected = xenos::portable_pack::RuntimeContract(image, identity, format);
        xenos::portable_pack::Reader reader(path, expected, format);
        reader.VerifyAll();
        r = reader.Info();
    } else r = xenos::portable_pack::Reader::Inspect(path, verified);
    std::cout<<"{\n  \"schema\": "<<xenos::portable_pack::Schema
        <<",\n  \"format\": \""<<(r.format == xenos::portable_pack::PackFormat::Spirv ? "spirv" : "dxil")<<"\""
        <<",\n  \"contract\": \""<<xenos::resources::Sha256Hex(r.contract)<<"\""
        <<",\n  \"records\": "<<r.records<<",\n  \"unique_binaries\": "<<r.uniqueBinaries
        <<",\n  \"blocks\": "<<r.blocks<<",\n  \"failures_omitted\": "<<r.failuresOmitted
        <<",\n  \"reconstructed_hlsl_bytes_omitted\": "<<r.hlslBytesOmitted
        <<",\n  \"diagnostic_bytes_omitted\": "<<r.diagnosticBytesOmitted
        <<",\n  \"binary_bytes_before_dedup\": "<<r.binaryBytes
        <<",\n  \"binary_bytes_after_dedup\": "<<r.uniqueBinaryBytes
        <<",\n  \"compressed_payload_bytes\": "<<r.compressedBytes
        <<",\n  \"index_bytes\": "<<r.indexBytes
        <<",\n  \"file_bytes\": "<<r.fileBytes
        <<",\n  \"file_mib\": "<<std::fixed<<std::setprecision(3)<<double(r.fileBytes)/1048576.0
        <<",\n  \"all_payloads_verified\": "<<(verified?"true":"false")
        <<",\n  \"runtime_compatibility_verified\": "<<(runtime?"true":"false")<<"\n}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"shader pack: "<<e.what()<<'\n';return 1;}
