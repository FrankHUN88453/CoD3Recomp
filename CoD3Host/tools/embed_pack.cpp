// embed_pack <input> <output> <name>: a library made ready to go into the
// executable as a resource (embedded_libraries.cpp): a short header (its
// name, size and hash) and the file compressed with the XPRESS Huffman
// format Windows has built in, which the executable undoes with the same
// system routine. Built and run as part of the build.

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#include "../embedded_format.h"

namespace
{
    typedef LONG (NTAPI* GetWorkSpaceSize)(USHORT, PULONG, PULONG);
    typedef LONG (NTAPI* Compress)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);

    bool ReadAll(const wchar_t* path, std::vector<uint8_t>& data)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"rb") != 0 || f == nullptr) return false;
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize(size_t(size));
        const bool ok = size == 0 || fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        return ok;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4)
    {
        fwprintf(stderr, L"usage: embed_pack <input> <output> <name>\n");
        return 2;
    }
    std::vector<uint8_t> input;
    if (!ReadAll(argv[1], input) || input.empty())
    {
        fwprintf(stderr, L"embed_pack: cannot read %ls\n", argv[1]);
        return 1;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto workSpaceSize = reinterpret_cast<GetWorkSpaceSize>(GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));
    const auto compress = reinterpret_cast<Compress>(GetProcAddress(ntdll, "RtlCompressBuffer"));
    if (workSpaceSize == nullptr || compress == nullptr) { fwprintf(stderr, L"embed_pack: no compression routines\n"); return 1; }

    const USHORT format = Embedded::CompressionFormat | 0x0100;   // COMPRESSION_ENGINE_MAXIMUM
    ULONG workSize = 0, fragmentSize = 0;
    if (workSpaceSize(format, &workSize, &fragmentSize) != 0) { fwprintf(stderr, L"embed_pack: no work space size\n"); return 1; }
    std::vector<uint8_t> work(workSize);
    std::vector<uint8_t> packed(input.size() + input.size() / 8 + 65536);
    ULONG packedSize = 0;
    const LONG status = compress(format, input.data(), ULONG(input.size()), packed.data(), ULONG(packed.size()), 4096, &packedSize, work.data());
    if (status != 0) { fwprintf(stderr, L"embed_pack: compression failed (0x%08lX)\n", status); return 1; }

    Embedded::Header header{};
    memcpy(header.magic, Embedded::Magic, sizeof(header.magic));
    header.version = Embedded::Version;
    header.originalSize = input.size();
    header.packedSize = packedSize;
    header.hash = Embedded::Hash(input.data(), input.size());
    wcsncpy_s(header.name, argv[3], _TRUNCATE);

    FILE* out = nullptr;
    if (_wfopen_s(&out, argv[2], L"wb") != 0 || out == nullptr) { fwprintf(stderr, L"embed_pack: cannot write %ls\n", argv[2]); return 1; }
    const bool written = fwrite(&header, sizeof(header), 1, out) == 1 && fwrite(packed.data(), 1, packedSize, out) == packedSize;
    fclose(out);
    if (!written) { fwprintf(stderr, L"embed_pack: cannot write %ls\n", argv[2]); return 1; }
    wprintf(L"embed_pack: %ls, %llu bytes to %lu\n", argv[3], (unsigned long long)input.size(), packedSize);
    return 0;
}
