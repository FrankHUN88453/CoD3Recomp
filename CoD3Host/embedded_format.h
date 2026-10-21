#pragma once

// A library carried inside the executable as a resource (embedded_libraries.cpp,
// written by tools/embed_pack.cpp at build time): this header, then the file
// compressed in Windows' own XPRESS Huffman format.

#include <cstddef>
#include <cstdint>

namespace Embedded
{
    constexpr char Magic[4] = { 'C', '3', 'D', 'L' };
    constexpr uint32_t Version = 1;
    constexpr unsigned short CompressionFormat = 4;   // COMPRESSION_FORMAT_XPRESS_HUFF
    // The resources' identifiers, one after another from this one.
    constexpr int FirstResource = 3001;

    struct Header
    {
        char magic[4];
        uint32_t version;
        uint64_t originalSize;
        uint64_t packedSize;
        uint64_t hash;           // FNV-1a of the original file
        wchar_t name[64];        // its file name, avcodec-62.dll say
    };

    inline uint64_t Hash(const uint8_t* data, size_t size)
    {
        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 0; i < size; i++) { hash ^= data[i]; hash *= 1099511628211ull; }
        return hash;
    }
}
