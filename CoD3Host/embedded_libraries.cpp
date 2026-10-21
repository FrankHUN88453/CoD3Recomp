// The libraries the executable carries (embedded_libraries.h): resources from
// Embedded::FirstResource on, each a header and an XPRESS Huffman packed file
// (embedded_format.h), unpacked with the routine Windows has for it.
//
// They go into %LOCALAPPDATA%\CoD3Recomp\libraries\<hash of them all>: one
// folder a set, so a newer build with other libraries never meets an older
// set, and a file is written under another name and renamed when whole, so
// one with the right size is the right one.

#include "embedded_libraries.h"
#include "embedded_format.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    typedef LONG (NTAPI* GetWorkSpaceSize)(USHORT, PULONG, PULONG);
    typedef LONG (NTAPI* Decompress)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

    struct Packed { const Embedded::Header* header; const uint8_t* data; };

    std::vector<Packed> Resources()
    {
        std::vector<Packed> found;
        for (int id = Embedded::FirstResource;; id++)
        {
            HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
            if (resource == nullptr) break;
            HGLOBAL loaded = LoadResource(nullptr, resource);
            const auto* bytes = loaded != nullptr ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
            const DWORD size = SizeofResource(nullptr, resource);
            if (bytes == nullptr || size < sizeof(Embedded::Header)) break;
            const auto* header = reinterpret_cast<const Embedded::Header*>(bytes);
            if (memcmp(header->magic, Embedded::Magic, 4) != 0 || header->version != Embedded::Version ||
                sizeof(Embedded::Header) + header->packedSize > size)
                break;
            found.push_back({ header, bytes + sizeof(Embedded::Header) });
        }
        return found;
    }

    bool SizeIs(const std::wstring& path, uint64_t size)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
        return ((uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow) == size;
    }

    bool Unpack(const Packed& packed, const std::wstring& path)
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto workSpaceSize = reinterpret_cast<GetWorkSpaceSize>(GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));
        const auto decompress = reinterpret_cast<Decompress>(GetProcAddress(ntdll, "RtlDecompressBufferEx"));
        if (workSpaceSize == nullptr || decompress == nullptr) return false;
        ULONG workSize = 0, fragmentSize = 0;
        if (workSpaceSize(Embedded::CompressionFormat, &workSize, &fragmentSize) != 0) return false;
        std::vector<uint8_t> work(fragmentSize != 0 ? fragmentSize : workSize);
        std::vector<uint8_t> out(size_t(packed.header->originalSize));
        ULONG final = 0;
        const LONG status = decompress(Embedded::CompressionFormat, out.data(), ULONG(out.size()), const_cast<PUCHAR>(packed.data),
                                       ULONG(packed.header->packedSize), &final, work.data());
        if (status != 0 || final != out.size()) return false;
        if (Embedded::Hash(out.data(), out.size()) != packed.header->hash) return false;

        const std::wstring partial = path + L".part";
        FILE* f = nullptr;
        if (_wfopen_s(&f, partial.c_str(), L"wb") != 0 || f == nullptr) return false;
        const bool written = fwrite(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        if (!written || !MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            DeleteFileW(partial.c_str());
            return false;
        }
        return true;
    }

    std::wstring Make()
    {
        const std::vector<Packed> packed = Resources();
        if (packed.empty()) return {};
        // The set's folder, by the hashes of what is in it.
        uint64_t key = 14695981039346656037ull;
        for (const Packed& p : packed) key = (key ^ p.header->hash) * 1099511628211ull;
        wchar_t root[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) length = GetTempPathW(MAX_PATH, root);
        if (length == 0) return {};
        std::wstring folder(root);
        if (folder.back() != L'\\') folder += L'\\';
        folder += L"CoD3Recomp";
        CreateDirectoryW(folder.c_str(), nullptr);
        folder += L"\\libraries";
        CreateDirectoryW(folder.c_str(), nullptr);
        wchar_t name[32];
        swprintf(name, 32, L"\\%016llx", (unsigned long long)key);
        folder += name;
        CreateDirectoryW(folder.c_str(), nullptr);
        for (const Packed& p : packed)
        {
            const std::wstring path = folder + L"\\" + p.header->name;
            if (SizeIs(path, p.header->originalSize)) continue;
            printf("Unpacking %ls (first start of this version)...\n", p.header->name);
            fflush(stdout);
            if (!Unpack(p, path))
            {
                printf("Could not unpack %ls into %ls; the sound may be silent.\n", p.header->name, folder.c_str());
                fflush(stdout);
                return {};
            }
        }
        return folder;
    }
}

const std::wstring& EmbeddedLibraries::Folder()
{
    static std::wstring folder;
    static std::once_flag once;
    std::call_once(once, []() { folder = Make(); });
    return folder;
}
