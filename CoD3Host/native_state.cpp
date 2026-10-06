// The native path's draw state, recorded at the title's draw calls and held
// against the register file (native_state.h, docs/native-d3d11-plan.md).
//
// Where the device keeps what, from the decompiled library: the dirty masks
// at +0 .. +32 say which of these go out before a draw, and the copies they
// go out from are
//   +1024   the fetch constants, 0x4800.., 192 words
//   +1792   the vertex float constants, 0x4000.., 1024 words
//   +5888   the pixel float constants, 0x4400.., 1024 words
//   +9984   the boolean and loop constants, 0x4900.., 40 words
//   +10240  0x2000..0x200F (the surfaces)
//   +10304  0x2080..0x2082 (the window offset and scissor)
//   +10316  0x2100..0x2114
//   +10400  0x2180..0x2184
//   +10420  0x2200..0x220B
//   +10468  0x2280..0x2294
//   +10552  0x2300..0x2325
//   +10704  0x2380..0x2387
// and the shader objects at +12416 (pixel) and +12420 (vertex).
//
// The shadows are not the whole of it. The title's own renderer writes
// packets straight into the device's command buffer: a material's state
// is a block of register writes and SET_CONSTANTs prepared beforehand
// (sub_821563A8 and its kin copy it in after room from sub_82156B38), and
// it clears the dirty bits of what it wrote, so the shadows keep older
// values. Float constants come from memory too (LOAD_ALU_CONSTANT) and
// from room in the command buffer the title fills (sub_822F8D38). And when
// the device draws in two passes (a Z pass and the colour pass, +10808 bit
// 6), the shader load writes the Z pass its own mode, program control and
// position-only vertex program, predicated on its bins.
//
// So each device has a model of what its own command stream has set,
// read from the stream in the order it was written: at each draw call up
// to the draw, and at each kick (sub_822F2818) to the end of the segment.
// It knows each register's last value, per float4 constant whether it is
// to come from memory, and per pass the programs and what predicated
// packets set. A draw's record is the model at its packet, the shadows
// standing in for what the stream has not said yet.

#include "native_state.h"
#include "kernel.h"
#include "gpu.h"
#include "render.h"
#include "render_shaders.h"
#include "render_state.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    struct Block { uint32_t first, count, offset; };
    constexpr Block Blocks[] = {
        { 0x2000, 16, 10240 }, { 0x2080, 3, 10304 }, { 0x2100, 21, 10316 }, { 0x2180, 5, 10400 },
        { 0x2200, 12, 10420 }, { 0x2280, 21, 10468 }, { 0x2300, 38, 10552 }, { 0x2380, 8, 10704 },
        { 0x4800, 192, 1024 }, { 0x4000, 1024, 1792 }, { 0x4400, 1024, 5888 }, { 0x4900, 40, 9984 },
    };
    constexpr uint32_t ShadowWords = 16 + 3 + 21 + 5 + 12 + 21 + 38 + 8 + 192 + 1024 + 1024 + 40;

    // Where a register is in a record's copy; ShadowWords for none. A table
    // by register: the stream writes thousands of registers a frame.
    struct CopyIndexTable
    {
        uint16_t at[0x5000];
        CopyIndexTable()
        {
            for (uint32_t reg = 0; reg < 0x5000; reg++) at[reg] = uint16_t(ShadowWords);
            uint32_t next = 0;
            for (const Block& block : Blocks)
                for (uint32_t i = 0; i < block.count; i++) at[block.first + i] = uint16_t(next++);
        }
    };
    const CopyIndexTable g_copyIndex;
    inline uint32_t CopyIndex(uint32_t reg) { return reg < 0x5000 ? g_copyIndex.at[reg] : ShadowWords; }

    constexpr uint32_t DeviceWrite = 40, DeviceSegmentEnd = 44;

    constexpr uint32_t OpDrawIndx = 0x22, OpDrawIndx2 = 0x36, OpImLoad = 0x27, OpImLoadImmediate = 0x2B;
    constexpr uint32_t OpSetConstant = 0x2D, OpLoadAluConstant = 0x2F, OpSetBinMaskLow = 0x60;

    // The two passes of a device drawing in bins: the replay selects bins
    // 0 and 31 for the Z pass, bin 1 for the colour pass (Gpu::BinSelect).
    // The colour pass's state is also that of everything not drawn in bins.
    constexpr uint32_t ColourPass = 0, ZPass = 1;
    constexpr uint32_t ZBins = 0x80000001u, ColourBins = 0x00000002u;

    bool Wanted(const char* name)
    {
        const char* text = getenv(name);
        return text != nullptr && text[0] != 0 && text[0] != '0';
    }
    bool CheckingNow()
    {
        static const bool on = Wanted("COD3_NATIVECHECK");
        return on;
    }
    bool DrawingNow()
    {
        static const bool on = Wanted("COD3_NATIVE");
        return on;
    }
    // Whether draws are recorded at all.
    bool RecordingNow()
    {
        static const bool on = CheckingNow() || DrawingNow();
        return on;
    }

    // The library's own physical address (the window at 0xE0000000 is 4 KB on).
    uint32_t Physical(uint32_t address)
    {
        return ((((address >> 20) & 0xFFF) + 512) & 0x1000) + (address & 0x1FFFFFFF);
    }

    // A register, by the index a SET_CONSTANT or LOAD_ALU_CONSTANT gives.
    bool ConstantIndex(uint32_t offsetAndType, uint32_t& index)
    {
        index = offsetAndType & 0x7FF;
        switch ((offsetAndType >> 16) & 0xFF)
        {
        case 0: index += 0x4000; return true;
        case 1: index += 0x4800; return true;
        case 2: index += 0x4900; return true;
        case 3: index += 0x4908; return true;
        case 4: index += 0x2000; return true;
        default: return false;
        }
    }

    struct Program { uint32_t address = 0, dwords = 0; uint64_t hash = 0; };

    Program LoadedProgram(uint8_t* base, uint32_t physical, uint32_t dwords)
    {
        Program program{ physical, dwords, 0 };
        if (dwords != 0 && dwords <= 0x4000) program.hash = RenderShaders::HashMicrocode(base + Guest::PhysicalAlias(physical), dwords);
        return program;
    }

    // What a device's command stream has set, in the order it was written.
    struct DeviceModel
    {
        bool started = false;
        uint32_t scanned = 0;                // the last word read
        uint32_t segmentEnd = 0;             // the end of the segment it is in (device+44)
        uint32_t binMask = ~0u;              // SET_BIN_MASK_LO's last
        uint32_t registers[ShadowWords];
        bool known[ShadowWords] = {};
        uint32_t unknown = ShadowWords;      // how many are not known yet
        uint32_t constantMemory[512] = {};   // per float4 (vertex, then pixel): the physical address it is loaded from, 0 none
        uint32_t fromMemory = 0;             // how many of those are not nought
        Program vertex[2], pixel[2];         // by pass
        // Registers a predicated packet set for one pass: by copy index, the
        // passes (bit 0 colour, 1 Z) and the values; and the indices that
        // have any, perhaps more than once and some cleared since.
        uint8_t passMask[ShadowWords] = {};
        uint32_t passValue[2][ShadowWords];
        std::vector<uint16_t> passList;
    };
    std::mutex g_modelMutex;
    std::unordered_map<uint32_t, std::unique_ptr<DeviceModel>> g_devices;

    DeviceModel& ModelOf(uint32_t device)
    {
        std::unique_ptr<DeviceModel>& model = g_devices[device];
        if (!model) model.reset(new DeviceModel);
        return *model;
    }

    // A draw packet's state.
    struct Record
    {
        uint32_t lastWordValue = 0;
        uint32_t initiator = 0;
        uint32_t device = 0;
        uint32_t registers[ShadowWords];
        std::vector<std::pair<uint16_t, uint32_t>> constantMemory;   // float4 index: physical address
        Program vertex[2], pixel[2];
        std::vector<std::pair<uint32_t, uint32_t>> passRegisters[2];
    };

    // The records, a ring of them, found by their packet's last word.
    constexpr uint32_t RecordCount = 8192;
    std::mutex g_mutex;
    std::unique_ptr<Record[]> g_records;
    std::vector<uint32_t> g_keys;
    std::unordered_map<uint32_t, uint32_t> g_byKey;
    uint32_t g_next = 0;

    // What the check found, since the last report.
    struct Mismatch { uint64_t count = 0; uint32_t native = 0, file = 0, writer = 0, pass = 0, draw = 0; uint32_t around[48] = {}; };
    struct Tally
    {
        uint64_t recorded = 0, unparsed = 0, resyncs = 0, unknown = 0;
        uint64_t checked = 0, unmatched = 0, stale = 0, equal = 0;
        uint64_t passes[2] = {};
        uint64_t initiatorDiffers = 0;
        uint64_t vertexDiffers = 0, pixelDiffers = 0, colourDraws = 0;
        uint64_t memoryConstants = 0;
        std::map<uint32_t, Mismatch> registers;
    };
    Tally g_tally;
    std::chrono::steady_clock::time_point g_lastReport = std::chrono::steady_clock::now();

    const char* RegisterName(uint32_t reg)
    {
        switch (reg)
        {
        case 0x2000: return "SURFACE_INFO"; case 0x2001: return "COLOR_INFO0"; case 0x2002: return "DEPTH_INFO";
        case 0x2003: return "COLOR_INFO1"; case 0x2004: return "COLOR_INFO2"; case 0x2005: return "COLOR_INFO3";
        case 0x2080: return "WINDOW_OFFSET"; case 0x2081: return "WINDOW_SCISSOR_TL"; case 0x2082: return "WINDOW_SCISSOR_BR";
        case 0x2100: return "MAX_VTX_INDX"; case 0x2101: return "MIN_VTX_INDX"; case 0x2102: return "INDX_OFFSET";
        case 0x2103: return "MULTI_PRIM_RESET"; case 0x2104: return "COLOR_MASK"; case 0x2105: return "BLEND_RED";
        case 0x2106: return "BLEND_GREEN"; case 0x2107: return "BLEND_BLUE"; case 0x2108: return "BLEND_ALPHA";
        case 0x210D: return "STENCILREFMASK"; case 0x210E: return "ALPHA_REF";
        case 0x210F: return "VPORT_XSCALE"; case 0x2110: return "VPORT_XOFFSET"; case 0x2111: return "VPORT_YSCALE";
        case 0x2112: return "VPORT_YOFFSET"; case 0x2113: return "VPORT_ZSCALE"; case 0x2114: return "VPORT_ZOFFSET";
        case 0x2180: return "SQ_PROGRAM_CNTL"; case 0x2181: return "SQ_CONTEXT_MISC";
        case 0x2200: return "DEPTHCONTROL"; case 0x2201: return "BLENDCONTROL0"; case 0x2202: return "COLORCONTROL";
        case 0x2203: return "HIZCONTROL"; case 0x2204: return "CLIP_CNTL"; case 0x2205: return "SU_SC_MODE_CNTL";
        case 0x2206: return "VTE_CNTL"; case 0x2208: return "MODECONTROL"; case 0x2280: return "POINT_SIZE";
        case 0x2318: return "COPY_CONTROL";
        default: return nullptr;
        }
    }

    void Report()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - g_lastReport < std::chrono::seconds(5)) return;
        g_lastReport = now;
        Tally& t = g_tally;
        printf("native state, the last five seconds: %llu draw packets recorded (%llu scans not parsed, %llu resyncs, %llu registers not yet known)\n",
            (unsigned long long)t.recorded, (unsigned long long)t.unparsed, (unsigned long long)t.resyncs, (unsigned long long)t.unknown);
        printf("  %llu draws checked (%llu colour pass, %llu Z pass): %llu without a record, %llu with a stale one, %llu the same in every register\n",
            (unsigned long long)t.checked, (unsigned long long)t.passes[0], (unsigned long long)t.passes[1],
            (unsigned long long)t.unmatched, (unsigned long long)t.stale, (unsigned long long)t.equal);
        printf("  initiator differs %llu; vertex program differs %llu, pixel program differs %llu of %llu colour draws; %llu constants from memory\n",
            (unsigned long long)t.initiatorDiffers, (unsigned long long)t.vertexDiffers, (unsigned long long)t.pixelDiffers,
            (unsigned long long)t.colourDraws, (unsigned long long)t.memoryConstants);
        std::vector<std::pair<uint32_t, Mismatch>> list(t.registers.begin(), t.registers.end());
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
        uint32_t constants[3] = {}, fetches = 0, bools = 0;
        for (const auto& [reg, m] : list)
        {
            if (reg >= 0x4000 && reg < 0x4400) constants[0]++;
            else if (reg >= 0x4400 && reg < 0x4800) constants[1]++;
            else if (reg >= 0x4800 && reg < 0x48C0) fetches++;
            else if (reg >= 0x4900) bools++;
        }
        printf("  registers that differed: %zu (vertex constants %u, pixel constants %u, fetch constants %u, boolean and loop %u)\n",
            list.size(), constants[0], constants[1], fetches, bools);
        int shown = 0;
        for (const auto& [reg, m] : list)
        {
            if (shown++ >= 16) break;
            const char* name = RegisterName(reg);
            printf("    %04X %-18s x%-8llu e.g. %s pass: native %08X, register file %08X, written by the packet at %08X:",
                reg, name != nullptr ? name : "", (unsigned long long)m.count, m.pass == ZPass ? "Z" : "colour", m.native, m.file, m.writer);
            // The words around the writer: a few for a constant, more for a register.
            const uint32_t from = reg >= 0x4000 ? 28 : 0, to = reg >= 0x4000 ? 40 : 48;
            for (uint32_t i = from; i < to; i++) printf("%s%s%08X", reg < 0x4000 && i % 16 == 0 ? "\n        " : "", i == 32 ? " [" : " ", m.around[i]);
            if (reg < 0x4000) printf("\n        the draw's last word at %08X", m.draw);
            printf("\n");
        }
        fflush(stdout);
        t = Tally{};
    }

    // A record for one draw packet, from the device's model as it is at the
    // packet, the shadows standing in for what it does not know.
    void Keep(uint8_t* base, uint32_t device, uint32_t lastWord, uint32_t initiator, DeviceModel& model)
    {
        const uint32_t key = Physical(lastWord);
        // The pass overrides still in force, the list kept short.
        if (!model.passList.empty())
        {
            size_t kept = 0;
            for (uint16_t at : model.passList)
                if ((model.passMask[at] & 3) != 0 && (model.passMask[at] & 0x80) == 0)
                {
                    model.passMask[at] |= 0x80;   // listed once
                    model.passList[kept++] = at;
                }
            model.passList.resize(kept);
            for (uint16_t at : model.passList) model.passMask[at] &= 0x7F;
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_records)
        {
            g_records.reset(new Record[RecordCount]);
            g_keys.assign(RecordCount, 0);
        }
        const uint32_t slot = g_next++ % RecordCount;
        if (g_keys[slot] != 0)
        {
            auto it = g_byKey.find(g_keys[slot]);
            if (it != g_byKey.end() && it->second == slot) g_byKey.erase(it);
        }
        Record& record = g_records[slot];
        record.lastWordValue = Guest::Read32(base, lastWord);
        record.initiator = initiator;
        record.device = device;
        uint32_t unknown = 0;
        if (model.unknown == 0) memcpy(record.registers, model.registers, sizeof(record.registers));
        else
        {
            uint32_t at = 0;
            for (const Block& block : Blocks)
                for (uint32_t i = 0; i < block.count; i++, at++)
                {
                    if (model.known[at]) record.registers[at] = model.registers[at];
                    else { record.registers[at] = Guest::Read32(base, device + block.offset + i * 4); unknown++; }
                }
        }
        record.constantMemory.clear();
        if (model.fromMemory != 0)
            for (uint32_t i = 0; i < 512; i++)
                if (model.constantMemory[i] != 0) record.constantMemory.emplace_back(uint16_t(i), model.constantMemory[i]);
        for (uint32_t pass = 0; pass < 2; pass++)
        {
            record.vertex[pass] = model.vertex[pass];
            record.pixel[pass] = model.pixel[pass];
            record.passRegisters[pass].clear();
        }
        static const struct RegisterOf
        {
            uint16_t reg[ShadowWords];
            RegisterOf()
            {
                uint32_t next = 0;
                for (const Block& block : Blocks)
                    for (uint32_t i = 0; i < block.count; i++) reg[next++] = uint16_t(block.first + i);
            }
        } registerOf;
        for (uint16_t at : model.passList)
            for (uint32_t pass = 0; pass < 2; pass++)
                if (model.passMask[at] & (1u << pass)) record.passRegisters[pass].emplace_back(registerOf.reg[at], model.passValue[pass][at]);
        g_keys[slot] = key;
        g_byKey[key] = slot;
        g_tally.recorded++;
        g_tally.unknown += unknown;
    }

    // The passes a predicated packet runs in, under that bin mask.
    uint32_t PassesOf(uint32_t header, uint32_t binMask)
    {
        if ((header & 1) == 0) return 3;
        return ((binMask & ColourBins) != 0 ? 1u : 0u) | ((binMask & ZBins) != 0 ? 2u : 0u);
    }

    // A register written, in the passes given.
    inline void Written(DeviceModel& model, uint32_t reg, uint32_t value, uint32_t passes)
    {
        const uint32_t at = CopyIndex(reg);
        if (at >= ShadowWords) return;
        if (passes == 3)
        {
            model.registers[at] = value;
            if (!model.known[at]) { model.known[at] = true; model.unknown--; }
            model.passMask[at] = 0;
            if (reg >= 0x4000 && reg < 0x4800)
            {
                uint32_t& memory = model.constantMemory[(reg - 0x4000) >> 2];
                if (memory != 0) { memory = 0; model.fromMemory--; }
            }
            return;
        }
        for (uint32_t pass = 0; pass < 2; pass++)
            if (passes & (1u << pass)) model.passValue[pass][at] = value;
        if (model.passMask[at] == 0) model.passList.push_back(uint16_t(at));
        model.passMask[at] |= uint8_t(passes & 3);
    }

    // The device's packets after model.scanned up to `last` (the last word
    // to read), into the model; a record at each draw.
    void Scan(uint8_t* base, uint32_t device, DeviceModel& model, uint32_t last)
    {
        uint32_t at = model.scanned + 4;
        while (at <= last)
        {
            const uint32_t header = Guest::Read32(base, at);
            const uint32_t type = header >> 30;
            const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t length = 1;
            if (type == 0)
            {
                const bool one = (header & 0x8000) != 0;
                for (uint32_t i = 0; i < count && at + 4 + i * 4 <= last; i++)
                    Written(model, (header & 0x7FFF) + (one ? 0 : i), Guest::Read32(base, at + 4 + i * 4), 3);
                length = 1 + count;
            }
            else if (type == 1) length = 3;
            else if (type == 3)
            {
                const uint32_t opcode = (header >> 8) & 0x7F;
                const uint32_t passes = PassesOf(header, model.binMask);
                length = 1 + count;
                auto word = [&](uint32_t i) { return Guest::Read32(base, at + 4 + i * 4); };
                if (opcode == OpSetBinMaskLow) model.binMask = word(0);
                else if (opcode == OpSetConstant)
                {
                    uint32_t index;
                    if (ConstantIndex(word(0), index))
                        for (uint32_t i = 0; i + 1 < count; i++) Written(model, index + i, word(1 + i), passes);
                }
                else if (opcode == OpLoadAluConstant)
                {
                    uint32_t index;
                    if (ConstantIndex(word(1), index) && index >= 0x4000 && index < 0x4800)
                    {
                        const uint32_t address = word(0) & ~3u, dwords = word(2) & 0xFFF;
                        for (uint32_t i = 0; i < dwords; i += 4)
                            if (index + i < 0x4800)
                            {
                                uint32_t& memory = model.constantMemory[(index + i - 0x4000) >> 2];
                                if (memory == 0) model.fromMemory++;
                                memory = address + i * 4;
                            }
                    }
                }
                else if (opcode == OpImLoad || opcode == OpImLoadImmediate)
                {
                    const bool pixel = (word(0) & 1) != 0;
                    const Program program = opcode == OpImLoad
                        ? LoadedProgram(base, word(0) & ~3u, word(1) & 0xFFFF)
                        : LoadedProgram(base, Physical(at + 12), word(1) & 0xFFFF);
                    for (uint32_t pass = 0; pass < 2; pass++)
                        if (passes & (1u << pass)) (pixel ? model.pixel : model.vertex)[pass] = program;
                }
                else if (opcode == OpDrawIndx || opcode == OpDrawIndx2)
                    Keep(base, device, at + count * 4, word(opcode == OpDrawIndx ? 1 : 0), model);
            }
            at += length * 4;
        }
        if (at != last + 4)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_tally.unparsed++;
        }
        model.scanned = last;
    }

    // Whether the model can read on from where it is to `last`: the same
    // segment of the command buffer, forward.
    bool Continues(uint8_t* base, uint32_t device, const DeviceModel& model, uint32_t last)
    {
        return model.started && Guest::Read32(base, device + DeviceSegmentEnd) == model.segmentEnd &&
               last >= model.scanned && last - model.scanned < 0x400000;
    }

    // The model goes on from the device's write pointer, in its segment.
    void Restart(uint8_t* base, uint32_t device, DeviceModel& model, uint32_t at)
    {
        model.started = true;
        model.scanned = at;
        model.segmentEnd = Guest::Read32(base, device + DeviceSegmentEnd);
    }
}

bool NativeState::Checking() { return CheckingNow(); }

bool NativeState::Drawing() { return DrawingNow(); }

namespace
{
    // The registers of the draw being made, by register number: only the
    // draw's own (the blocks above) are filled, each draw.
    uint32_t g_image[0x5000];
}

bool NativeState::Begin(uint32_t lastWordPhysical)
{
    if (!DrawingNow()) return false;
    const uint32_t key = lastWordPhysical & 0x1FFFFFFF;
    Program vertex, pixel;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_byKey.find(key);
        if (it == g_byKey.end()) return false;
        const Record& record = g_records[it->second];
        if (Guest::Read32(Guest::Base, Guest::PhysicalAlias(lastWordPhysical)) != record.lastWordValue) return false;
        const uint32_t select = uint32_t(Gpu::BinSelect());
        const uint32_t pass = (select & ZBins) != 0 && (select & ColourBins) == 0 ? ZPass : ColourPass;
        uint32_t at = 0;
        for (const Block& block : Blocks)
        {
            memcpy(g_image + block.first, record.registers + at, block.count * 4);
            at += block.count;
        }
        for (const auto& [index, address] : record.constantMemory)
        {
            const uint32_t reg = 0x4000 + index * 4u;
            for (uint32_t i = 0; i < 4; i++) g_image[reg + i] = Guest::Read32(Guest::Base, Guest::PhysicalAlias(address + i * 4));
        }
        for (const auto& [reg, value] : record.passRegisters[pass])
            if (reg < 0x5000) g_image[reg] = value;
        vertex = record.vertex[pass];
        pixel = record.pixel[pass];
    }
    if (vertex.dwords != 0) Render::UseProgram(false, Guest::PhysicalAlias(vertex.address), vertex.dwords, vertex.hash);
    if (pixel.dwords != 0) Render::UseProgram(true, Guest::PhysicalAlias(pixel.address), pixel.dwords, pixel.hash);
    RenderState::UseRegisters(g_image);
    return true;
}

void NativeState::End() { RenderState::UseRegisters(nullptr); }

void NativeState::Recorded(uint8_t* base, uint32_t device, uint32_t before, uint32_t after)
{
    if (!RecordingNow()) return;
    std::lock_guard<std::mutex> modelLock(g_modelMutex);
    DeviceModel& model = ModelOf(device);
    if (!Continues(base, device, model, after))
    {
        // The model lost the stream (or never had it): on from this call.
        if (model.started)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_tally.resyncs++;
        }
        Restart(base, device, model, before);
        if (after < before || after - before > 0x10000) { model.scanned = after; return; }
    }
    Scan(base, device, model, after);
}

void NativeState::Kicking(uint8_t* base, uint32_t device)
{
    if (!RecordingNow()) return;
    std::lock_guard<std::mutex> modelLock(g_modelMutex);
    DeviceModel& model = ModelOf(device);
    const uint32_t last = Guest::Read32(base, device + DeviceWrite);
    if (Continues(base, device, model, last)) Scan(base, device, model, last);
}

void NativeState::Kicked(uint8_t* base, uint32_t device)
{
    if (!RecordingNow()) return;
    std::lock_guard<std::mutex> modelLock(g_modelMutex);
    DeviceModel& model = ModelOf(device);
    if (!model.started) return;
    Restart(base, device, model, Guest::Read32(base, device + DeviceWrite));
}

void NativeState::Check(uint32_t lastWordPhysical, uint32_t initiator, uint32_t indexBase)
{
    (void)indexBase;
    if (!CheckingNow()) return;
    const uint32_t key = lastWordPhysical & 0x1FFFFFFF;
    const std::atomic<uint32_t>* file = Gpu::RegisterFile();
    std::lock_guard<std::mutex> lock(g_mutex);
    Tally& t = g_tally;
    t.checked++;
    auto it = g_byKey.find(key);
    if (it == g_byKey.end()) { t.unmatched++; Report(); return; }
    const Record& record = g_records[it->second];
    if (Guest::Read32(Guest::Base, Guest::PhysicalAlias(lastWordPhysical)) != record.lastWordValue) { t.stale++; Report(); return; }

    // The pass the command processor is in: the Z pass when it runs the
    // Z bins only.
    const uint32_t select = uint32_t(Gpu::BinSelect());
    const uint32_t pass = (select & ZBins) != 0 && (select & ColourBins) == 0 ? ZPass : ColourPass;
    t.passes[pass]++;

    // What the record says the registers are, for this pass.
    uint32_t expected[ShadowWords];
    memcpy(expected, record.registers, sizeof(expected));
    for (const auto& [index, address] : record.constantMemory)
    {
        const uint32_t reg = 0x4000 + index * 4u;
        for (uint32_t i = 0; i < 4; i++) expected[CopyIndex(reg + i)] = Guest::Read32(Guest::Base, Guest::PhysicalAlias(address + i * 4));
        t.memoryConstants++;
    }
    for (const auto& [reg, value] : record.passRegisters[pass])
    {
        const uint32_t at = CopyIndex(reg);
        if (at < ShadowWords) expected[at] = value;
    }

    if (record.initiator != initiator) t.initiatorDiffers++;
    if (record.vertex[pass].hash != Render::CurrentProgramHash(false)) t.vertexDiffers++;
    if ((file[0x2208].load(std::memory_order_relaxed) & 7) == 4)
    {
        t.colourDraws++;
        if (record.pixel[pass].hash != Render::CurrentProgramHash(true)) t.pixelDiffers++;
    }

    bool equal = true;
    uint32_t at = 0;
    for (const Block& block : Blocks)
        for (uint32_t i = 0; i < block.count; i++, at++)
        {
            const uint32_t reg = block.first + i;
            const uint32_t value = file[reg].load(std::memory_order_relaxed);
            if (value == expected[at]) continue;
            equal = false;
            Mismatch& m = t.registers[reg];
            if (m.count++ == 0)
            {
                m.native = expected[at];
                m.file = value;
                m.pass = pass;
                m.writer = Gpu::RegisterWriter(reg);
                if (m.writer != 0)
                    for (uint32_t w = 0; w < 48; w++) m.around[w] = Guest::Read32(Guest::Base, Guest::PhysicalAlias(m.writer - 128 + w * 4));
                m.draw = key;
            }
        }
    if (equal) t.equal++;
    Report();
}
