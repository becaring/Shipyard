// Host generator for the prebuilt shader pack. For each key it runs what the
// runtime runs before compiling (and what the pack verification regenerates):
// Shipyard_BuildFast3DShader with the consumer's feature decoder and projected
// Prism builder, then Shipyard_LowerShader with the consumer's ABI. Each
// distinct lowered source is written once, named by its FNV-1a 64 hash; stdout
// has "K id0 id1 tp srgb floats name" per key and "F id0 id1 tp srgb error"
// per failure. The consumer's shader_provider.h declares the key type and
// builder; its builder loads the template from Shipyard_ShaderPackTemplate.
//
// usage: shader_pack_sources <template> <keys> <out-dir> <abi>
// keys: one "<id0 hex> <id1 hex> <three-point 0|1> <srgb 0|1>" per line
#include "shader_provider.h"
#include "graphics/shader_lower.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

std::string Shipyard_ShaderPackTemplate;

static uint64_t Fnv64(const std::string& text) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < text.size(); ++i)
        hash = (hash ^ static_cast<unsigned char>(text[i])) * 1099511628211ULL;
    return hash;
}

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::runtime_error("usage: shader_pack_sources <template> <keys> <out-dir> <abi>");
        std::ifstream templ(argv[1], std::ios::binary);
        std::ostringstream bytes;
        bytes << templ.rdbuf();
        Shipyard_ShaderPackTemplate = bytes.str();
        if (Shipyard_ShaderPackTemplate.empty())
            throw std::runtime_error("empty shader template");
        const unsigned abi = static_cast<unsigned>(std::strtoul(argv[4], NULL, 10));
        if (abi != 2 && abi != 3)
            throw std::runtime_error("unsupported shader ABI");
        std::ifstream keys(argv[2]);
        const std::string out(argv[3]);
        unsigned long long id0 = 0, id1 = 0;
        int threePoint = 0, srgb = 0;
        while (keys >> std::hex >> id0 >> id1 >> std::dec >> threePoint >> srgb) {
            Fast::ShaderProgramKey key;
            key.shaderId0 = id0;
            key.shaderId1 = id1;
            key.filteringMode = threePoint ? Fast::FILTER_THREE_POINT : Fast::FILTER_LINEAR;
            key.srgbMode = srgb != 0;
            CCFeatures features;
            size_t floats = 0;
            std::string hlsl, lowered, error;
            if (!Shipyard_BuildFast3DShader(key, features, floats, hlsl, error) ||
                !Shipyard_LowerShader(hlsl, lowered, error, abi)) {
                std::printf("F %016llx %016llx %d %d %s\n", id0, id1, threePoint, srgb, error.c_str());
                continue;
            }
            char name[17];
            std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(Fnv64(lowered)));
            const std::string path = out + "/" + name + ".hlsl";
            std::ifstream existing(path.c_str(), std::ios::binary);
            std::ostringstream current;
            if (existing.good())
                current << existing.rdbuf();
            existing.close();
            // A name collision must never let one file stand for two sources.
            if (!current.str().empty() && current.str() != lowered)
                throw std::runtime_error("lowered source name collision: " + path);
            if (current.str().empty()) {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << lowered;
                if (!file.good())
                    throw std::runtime_error("writing " + path);
            }
            std::printf("K %016llx %016llx %d %d %u %s\n", id0, id1, threePoint, srgb,
                        static_cast<unsigned>(floats), name);
        }
        return 0;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "%s\n", failure.what());
        return 1;
    }
}
