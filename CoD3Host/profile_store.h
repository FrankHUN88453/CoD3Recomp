#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include <utility>

// Host file format only. Guest profile pointers are never stored on disk.
namespace ProfileStore {
constexpr uint32_t MaxEntries = 128, MaxPayload = 65536;
struct Value { uint32_t type = 0; std::vector<uint8_t> bytes; };
using Values = std::map<uint32_t, Value>;
inline void Put(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(v >> shift));
}
inline bool Valid(const Value& v) {
    if (v.type == 1 || v.type == 5) return v.bytes.size() == 4;
    if (v.type == 2 || v.type == 3) return v.bytes.size() == 8;
    return (v.type == 4 || v.type == 6) && v.bytes.size() <= MaxPayload;
}
inline std::vector<uint8_t> Encode(const Values& values) {
    std::vector<uint8_t> out{'C','O','D','3','P','R','F','1'};
    if (values.size() > MaxEntries) return {};
    Put(out, uint32_t(values.size()));
    for (const auto& [id, value] : values) {
        if (!Valid(value)) return {};
        Put(out, id); Put(out, value.type); Put(out, uint32_t(value.bytes.size()));
        out.insert(out.end(), value.bytes.begin(), value.bytes.end());
    }
    return out;
}
inline bool Decode(const std::vector<uint8_t>& data, Values& result) {
    if (data.size() < 12 || std::memcmp(data.data(), "COD3PRF1", 8)) return false;
    size_t at = 8;
    auto word = [&]() { uint32_t v = 0; for (int i=0;i<4;++i) v=(v<<8)|data[at++]; return v; };
    const uint32_t count = word();
    if (count > MaxEntries) return false;
    Values parsed;
    for (uint32_t i=0;i<count;++i) {
        if (data.size()-at < 12) return false;
        const uint32_t id=word(), type=word(), size=word();
        if (size > MaxPayload || size > data.size()-at || parsed.count(id)) return false;
        Value value{type, {data.begin()+at, data.begin()+at+size}};
        if (!Valid(value)) return false;
        parsed.emplace(id, std::move(value)); at += size;
    }
    if (at != data.size()) return false;
    result = std::move(parsed);
    return true;
}
}
