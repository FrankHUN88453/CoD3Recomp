#include "kernel.h"
#include "shaders.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    std::mutex g_mutex;
    std::map<uint64_t, bool> g_seen;      // content hash -> written
    uint64_t g_lastVertexHash = 0;
    uint64_t g_lastPixelHash = 0;
    std::vector<uint32_t> g_lastVertexWords;
    std::vector<uint32_t> g_lastPixelWords;
    uint64_t g_vertexUploads = 0;
    uint64_t g_pixelUploads = 0;
    uint64_t g_distinct = 0;
    uint64_t g_dwords = 0;
    bool g_directoryFailed = false;

    // Enough to hold everything this title uses without filling a disk if a
    // decode ever goes wrong and the sizes stop making sense.
    constexpr uint64_t MaximumFiles = 256;
    constexpr uint32_t MaximumDwords = 64 * 1024;

    std::filesystem::path Directory()
    {
        return std::filesystem::current_path() / "shaders";
    }

    uint64_t Hash(const std::vector<uint8_t>& bytes)
    {
        uint64_t hash = 1469598103934665603ull;
        for (uint8_t byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

void Shaders::Capture(bool pixel, uint32_t guestAddress, uint32_t sizeDwords)
{
    if (sizeDwords == 0 || sizeDwords > MaximumDwords) return;
    if (guestAddress == 0) return;

    // The microcode is kept exactly as the console stores it, big endian, so a
    // translator reads the same bytes the hardware would.
    std::vector<uint8_t> bytes(size_t(sizeDwords) * 4);
    std::vector<uint32_t> words(sizeDwords);
    for (uint32_t i = 0; i < sizeDwords; i++)
    {
        const uint32_t word = Guest::Read32(Guest::Base, guestAddress + i * 4);
        words[i] = word;
        bytes[i * 4 + 0] = uint8_t(word >> 24);
        bytes[i * 4 + 1] = uint8_t(word >> 16);
        bytes[i * 4 + 2] = uint8_t(word >> 8);
        bytes[i * 4 + 3] = uint8_t(word);
    }

    const uint64_t hash = Hash(bytes);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (pixel) { g_pixelUploads++; g_lastPixelHash = hash; g_lastPixelWords = words; }
    else       { g_vertexUploads++; g_lastVertexHash = hash; g_lastVertexWords = words; }
    g_dwords += sizeDwords;

    if (g_seen.find(hash) != g_seen.end()) return;
    g_seen[hash] = true;
    g_distinct++;
    if (g_distinct > MaximumFiles || g_directoryFailed) return;

    std::error_code error;
    std::filesystem::create_directories(Directory(), error);
    if (error)
    {
        g_directoryFailed = true;
        printf("shaders: could not create %s, not capturing microcode\n",
            Directory().string().c_str());
        fflush(stdout);
        return;
    }

    char name[64];
    snprintf(name, sizeof(name), "%s_%016llx.bin",
        pixel ? "ps" : "vs", (unsigned long long)hash);

    const std::filesystem::path path = Directory() / name;
    FILE* file = fopen(path.string().c_str(), "wb");
    if (file == nullptr) return;
    fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);
}

void Shaders::Report()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_vertexUploads + g_pixelUploads == 0) return;

    printf("microcode: %llu vertex and %llu pixel uploads, %llu dwords, "
           "%llu distinct programs written to %s\n",
        (unsigned long long)g_vertexUploads,
        (unsigned long long)g_pixelUploads,
        (unsigned long long)g_dwords,
        (unsigned long long)g_distinct,
        Directory().string().c_str());
    fflush(stdout);
}

uint64_t Shaders::LastHash(bool pixel)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return pixel ? g_lastPixelHash : g_lastVertexHash;
}

std::vector<uint32_t> Shaders::Microcode(bool pixel)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return pixel ? g_lastPixelWords : g_lastVertexWords;
}
