// Native title Options, independently of the F11 mixer and mouse settings.
#include "kernel.h"
#include "settings.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <string>

namespace {
void Restore(uint8_t* base,uint32_t preferences) {
    const auto settings=Settings::Get();
    Guest::Write32(base,preferences+68,settings.controllerHorizontal);
    Guest::Write32(base,preferences+72,settings.controllerVertical);
    for(uint32_t offset:{92u,96u,100u}) Guest::Write32(base,preferences+offset,settings.nativeVolume/2);
}
}
#ifndef COD3_MP
// --- single player: the Options kept between plays (0.5.233) ---
// The campaign's options live in its state block at 0x82A8E610 (the block
// the title also hands to the next mission through the launch data):
// +60..+107 hold the sensitivities, volumes and the switches, +400 the
// brightness (a float, 1.0 by default; the Gamma screen's).
// A new game process built them from the defaults each time ("brightness
// and sound reset after every play"). Now they are written to
// saves\options-sp.bin whenever they change (checked once a second) and put
// back when the block is built in the first 30 s of a process that was not
// relaunched for a next mission (that one has them in its launch data).
// COD3_NOSAVEOPTIONS=1 turns it off.
namespace {
constexpr uint32_t SpState=0x82A8E610, SpFrom=60, SpSize=48, SpBright=400;
// what is kept: the 48 bytes, then the brightness
void SpRead(const uint8_t* base,uint8_t* out) { memcpy(out,base+SpState+SpFrom,SpSize); memcpy(out+SpSize,base+SpState+SpBright,4); }
std::filesystem::path SpOptionsFile() {
    std::filesystem::path root=Kernel::SavesRoot();
    if(root.empty()) return {};
    return root/"options-sp.bin";
}
double Uptime() {
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}
bool SpOptionsOff() { return getenv("COD3_NOSAVEOPTIONS")!=nullptr; }
void SpOptionsRestore(uint8_t* base,uint32_t preferences) {
    if(preferences!=SpState || SpOptionsOff() || getenv("COD3_LAUNCHDATA") || Uptime()>30) return;
    const std::filesystem::path file=SpOptionsFile();
    if(file.empty()) return;
    FILE* in=_wfopen(file.c_str(),L"rb");
    if(!in) return;
    uint8_t saved[SpSize+4]; const size_t got=fread(saved,1,SpSize+4,in); fclose(in);
    if(got!=SpSize && got!=SpSize+4) return;
    memcpy(base+SpState+SpFrom,saved,SpSize);
    if(got==SpSize+4) {
        uint32_t raw; memcpy(&raw,saved+SpSize,4); raw=_byteswap_ulong(raw);   // (kept as the guest holds it)
        float f; memcpy(&f,&raw,4);
        if(f>=0.2f && f<=3.0f) memcpy(base+SpState+SpBright,saved+SpSize,4);
    }
    uint32_t b=Guest::Read32(base,SpState+SpBright); float bright; memcpy(&bright,&b,4);
    printf("native options: restored the saved options (volume %u/50, brightness %.2f)\n",
        Guest::Read32(base,SpState+92),bright); fflush(stdout);
}
}
// called once a frame (coop.cpp's Com_EventLoop hook)
void NativeOptionsPoll(uint8_t* base) {
    if(SpOptionsOff()) return;
    static double last=0; static bool have=false; static uint8_t seen[SpSize+4];
    const double now=Uptime();
    if(now<30 || now-last<1.0) return;   // (after the boot's own building of the block)
    last=now;
    if(!have) {
        auto text=[&](uint32_t at){ std::string t; for(int i=0;i<40;i++){ const char c=char(base[at+i]); if(c<32||c>126) break; t+=c; } return t; };
        printf("native options: state +60..+107:");
        for(uint32_t o=SpFrom;o<SpFrom+SpSize;o+=4) printf(" %08X",Guest::Read32(base,SpState+o));
        printf(" (+76 is from \"%s\")\n",text(0x8206EBCCu).c_str()); fflush(stdout);
        printf("native options: state +352..+411:");
        for(uint32_t o=352;o<412;o+=4) printf(" %08X",Guest::Read32(base,SpState+o));
        printf("\n"); fflush(stdout);
    }
    uint8_t current[SpSize+4]; SpRead(base,current);
    if(have && memcmp(current,seen,SpSize+4)==0) return;
    const bool first=!have;
    memcpy(seen,current,SpSize+4); have=true;
    const std::filesystem::path file=SpOptionsFile();
    if(file.empty()) return;
    if(first && std::filesystem::exists(file)) return;   // (the saved ones were put back: nothing new yet)
    FILE* out=_wfopen(file.c_str(),L"wb");
    if(!out) return;
    fwrite(current,1,SpSize+4,out); fclose(out);
    uint32_t b=Guest::Read32(base,SpState+SpBright); float bright; memcpy(&bright,&b,4);
    printf("native options: saved the options (volume %u/50, brightness %.2f)\n",
        Guest::Read32(base,SpState+92),bright); fflush(stdout);
}
#endif
#ifdef COD3_MP
extern "C" PPC_FUNC(__imp__sub_82506E78);
PPC_FUNC(sub_82506E78)
#else
extern "C" PPC_FUNC(__imp__sub_824D9C50);
PPC_FUNC(sub_824D9C50)
#endif
{
    const uint32_t preferences=ctx.r3.u32;
#ifdef COD3_MP
    __imp__sub_82506E78(ctx,base);
    // Only native CL_GamepadMove references this constant. Mounted yaw uses
    // its explicit cap before this fallback. 290 -> 580 degrees/second at the new level20 ceiling.
    Guest::Write32(base,0x82085A6C,std::bit_cast<uint32_t>(0.580f));
#else
    __imp__sub_824D9C50(ctx,base);
#endif
    // A native Restore Defaults action must use defaults, not the last save.
    Guest::Write32(base,preferences+68,100);
    Guest::Write32(base,preferences+72,100);
    for(uint32_t offset:{92u,96u,100u}) Guest::Write32(base,preferences+offset,25);
#ifndef COD3_MP
    SpOptionsRestore(base,preferences);
#endif
    static std::atomic<unsigned> reports{0};
    if(reports.fetch_add(1)<12) {
        printf("native options: defaults at %08X: controller %u/%u, volume%u/50\n",preferences,
            Guest::Read32(base,preferences+68),Guest::Read32(base,preferences+72),Guest::Read32(base,preferences+92));
        fflush(stdout);
    }
}
#ifdef COD3_MP
namespace {
constexpr uint32_t Preferences=0x82C6B730;
bool restoredMain=false;
void Ready(uint8_t* base) {
    // Console save deserialization follows construction. Restore PC Options
    // once after that; subsequent changes made in the menu remain untouched.
    if(!restoredMain) { Restore(base,Preferences); restoredMain=true; }
}
void Capture(uint8_t* base,bool controller) {
    auto settings=Settings::Get();
    const int horizontal=int(Guest::Read32(base,Preferences+68));
    const int vertical=int(Guest::Read32(base,Preferences+72));
    const int volume=int(Guest::Read32(base,Preferences+92))*2;
    bool changed=false;
    if(controller && horizontal>=10 && horizontal<=200 && vertical>=10 && vertical<=200) {
        changed=settings.controllerHorizontal!=horizontal || settings.controllerVertical!=vertical;
        settings.controllerHorizontal=horizontal; settings.controllerVertical=vertical;
    }
    if(!controller && volume>=0 && volume<=100) { changed=settings.nativeVolume!=volume; settings.nativeVolume=volume; }
    if(changed) {
        Settings::Set(settings);
        printf("native options: saved %s horizontal=%d vertical=%d volume=%d%%\n",controller?"controller":"audio",
            settings.controllerHorizontal,settings.controllerVertical,settings.nativeVolume); fflush(stdout);
    }
}
void Call(PPCContext& ctx,uint8_t* base,uint32_t function,uint32_t r3,uint32_t r4,uint32_t r5=0) {
    auto* target=Guest::Lookup(function); if(!target) return;
    PPCContext call=ctx; call.r1.u32=(ctx.r1.u32-0x2000)&~15u;
    call.r3.u64=r3; call.r4.u64=r4; call.r5.u64=r5; call.lr=0;
    target(call,base);
}
void Labels(PPCContext& ctx,uint8_t* base,uint32_t menu) {
    static uint32_t text=0;
    if(!text) text=Guest::AllocatePhysical(0x100);
    if(!text) return;
    for(unsigned axis=0;axis<2;++axis) {
        const uint32_t label=Guest::Read32(base,menu+112+axis*4);
        if(!label) continue;
        const uint32_t value=Guest::Read32(base,menu+216+axis*4),at=text+axis*128;
        snprintf(reinterpret_cast<char*>(base+at),128,"%s SENSITIVITY %u (%u%%)",axis?"VERTICAL":"HORIZONTAL",value/10,value);
        Call(ctx,base,Guest::Read32(base,Guest::Read32(base,label)+132),label,at);
    }
}
}
extern "C" PPC_FUNC(__imp__sub_825095D8);
PPC_FUNC(sub_825095D8) {
    const uint32_t preferences=ctx.r3.u32,caller=uint32_t(ctx.lr);
    if(preferences==Preferences) Ready(base);
    static unsigned reports=0;
    if(reports++<40) {
        printf("native options: apply prefs=%08X caller=%08X H=%u V=%u audio=%u/%u/%u pause=%u\n",
            preferences,caller,Guest::Read32(base,preferences+68),Guest::Read32(base,preferences+72),
            Guest::Read32(base,preferences+92),Guest::Read32(base,preferences+96),Guest::Read32(base,preferences+100),
            Guest::Read8(base,0x82C73024)); fflush(stdout);
    }
    __imp__sub_825095D8(ctx,base);
}
// The native controller screen stores menu+216/+220, clamps to50 and draws
// value*.02. Use10..200, steps10; a full bar now represents level20.
extern "C" PPC_FUNC(__imp__sub_824E72C0);
PPC_FUNC(sub_824E72C0) {
    ctx.r5.u32=(std::clamp(ctx.r5.u32,10u,200u)+2)/4;
    __imp__sub_824E72C0(ctx,base);
}
extern "C" PPC_FUNC(__imp__sub_824E7560);
PPC_FUNC(sub_824E7560) {
    const uint32_t menu=ctx.r3.u32; Ready(base);
    __imp__sub_824E7560(ctx,base); Labels(ctx,base,menu);
    printf("native options: controller menu horizontal=%u vertical=%u, maximum200\n",
        Guest::Read32(base,menu+216),Guest::Read32(base,menu+220)); fflush(stdout);
}
extern "C" PPC_FUNC(__imp__sub_824E7358);
PPC_FUNC(sub_824E7358) {
    const uint32_t menu=ctx.r3.u32,row=Guest::Read16(base,menu+34);
    if(row!=2 && row!=3) { __imp__sub_824E7358(ctx,base); return; }
    const uint32_t offset=row==2?216:220,bar=row==2?200:204;
    const int before=int(Guest::Read32(base,menu+offset));
    const int after=std::clamp(before+((ctx.r4.u32&255)==1?10:-10),10,200);
    __imp__sub_824E7358(ctx,base); // Retain native highlight/arrow animation.
    Guest::Write32(base,menu+offset,uint32_t(after));
    Call(ctx,base,0x824E72C0,menu,Guest::Read32(base,menu+bar),uint32_t(after));
    Labels(ctx,base,menu);
    printf("native options: %s level%d (%d%%)\n",row==2?"horizontal":"vertical",after/10,after); fflush(stdout);
}
extern "C" PPC_FUNC(__imp__sub_824DB360);
PPC_FUNC(sub_824DB360) { __imp__sub_824DB360(ctx,base); Capture(base,true); }
extern "C" PPC_FUNC(__imp__sub_824F56A0);
PPC_FUNC(sub_824F56A0) { Ready(base); __imp__sub_824F56A0(ctx,base); }
extern "C" PPC_FUNC(__imp__sub_824DB070);
PPC_FUNC(sub_824DB070) { __imp__sub_824DB070(ctx,base); Capture(base,false); }
extern "C" PPC_FUNC(__imp__sub_823F6DC0);
PPC_FUNC(sub_823F6DC0) { __imp__sub_823F6DC0(ctx,base); Capture(base,false); }
#endif

#ifdef COD3_MP
void NativeOptionsTracePad(PPCContext& ctx,uint8_t* base) {
    static uint32_t previousH=~0u,previousV=~0u,previousA=~0u;
    static unsigned reports=0;
    const uint32_t h=Guest::Read32(base,Preferences+68),v=Guest::Read32(base,Preferences+72),a=Guest::Read32(base,Preferences+92);
    if(reports<40 && (h!=previousH || v!=previousV || a!=previousA)) {
        ++reports;previousH=h;previousV=v;previousA=a;
        printf("native options: pad look H=%u V=%u audio=%u/50 pause=%u caller=%08X\n",
            h,v,a,Guest::Read8(base,0x82C73024),uint32_t(ctx.lr));fflush(stdout);
    }
}
#endif

#ifdef COD3_MP
namespace {
// Verified against live123 objects: controller panel vtable820716DC;
// child pointers+8/+12, slider value+24/min+28/max+32, setter196/getter200.
int ControllerAxis(uint8_t* base,uint32_t slider) {
    if(!slider || Guest::Read32(base,slider)!=0x82070AC4) return -1;
    const uint32_t panel=Guest::Read32(base,slider+4);
    if(!panel || Guest::Read32(base,panel)!=0x820716DC) return -1;
    const uint32_t children=Guest::Read32(base,panel+4);
    if(!children) return -1;
    for(unsigned axis=0;axis<2;++axis)
        if(Guest::Read32(base,children+8+axis*4)==slider) return int(axis);
    return -1;
}
void ControllerRange(uint8_t* base,uint32_t slider) {
    Guest::Write32(base,slider+28,10);Guest::Write32(base,slider+32,200);
}
void ControllerInitialize(uint8_t* base,uint32_t slider,int axis) {
    if(Guest::Read32(base,slider+32)!=200) {
        const uint32_t panel=Guest::Read32(base,slider+4),local=Guest::Read32(base,panel+80);
        const uint32_t profile=local<=3?Guest::Read32(base,0x82C8A270+local*3208+180):0;
        const uint32_t preferences=Preferences+(profile<4?profile:0)*6984;
        Guest::Write32(base,slider+24,std::clamp(Guest::Read32(base,preferences+68+axis*4),10u,200u));
    }
    ControllerRange(base,slider);
}
}
extern "C" PPC_FUNC(__imp__sub_820BDA20);
PPC_FUNC(sub_820BDA20) {
    if(ControllerAxis(base,ctx.r3.u32)>=0) {
        ControllerRange(base,ctx.r3.u32);
        ctx.r4.u32=std::clamp(ctx.r4.u32,10u,200u);
    }
    __imp__sub_820BDA20(ctx,base);
}
extern "C" PPC_FUNC(__imp__sub_82509E00);
extern "C" PPC_FUNC(__imp__sub_82509E58);
namespace {
void ControllerStep(PPCContext& ctx,uint8_t* base,bool up) {
    const uint32_t slider=ctx.r3.u32;
    const int axis=ControllerAxis(base,slider);
    if(axis<0) {
        if(up)__imp__sub_82509E00(ctx,base);else __imp__sub_82509E58(ctx,base);
        return;
    }
    ControllerInitialize(base,slider,axis);
    const int before=int(Guest::Read32(base,slider+24));
    const uint32_t after=uint32_t(std::clamp(before+(up?10:-10),10,200));
    // Keep the native UI sound/animation and return value. It steps by1;
    // publish our ten-percent value and redraw using the real max200.
    if(up)__imp__sub_82509E00(ctx,base);else __imp__sub_82509E58(ctx,base);
    Guest::Write32(base,slider+24,after);
    Call(ctx,base,0x824E8C40,slider,0);
    printf("native options: live %s %u%%, range10..200\n",axis?"vertical":"horizontal",after);fflush(stdout);
}
}
PPC_FUNC(sub_82509E00) {ControllerStep(ctx,base,true);}
PPC_FUNC(sub_82509E58) {ControllerStep(ctx,base,false);}
extern "C" PPC_FUNC(__imp__sub_8250A4D0);
PPC_FUNC(sub_8250A4D0) {
    const uint32_t panel=ctx.r3.u32;
    const bool controller=Guest::Read32(base,panel)==0x820716DC;
    __imp__sub_8250A4D0(ctx,base);
    if(controller) Capture(base,true);
}
#endif

#ifdef COD3_MP
// Construction can set the value before installing the child pointer. Repair
// old resource limits before the first draw as well as before the first step.
extern "C" PPC_FUNC(__imp__sub_824F6758);
PPC_FUNC(sub_824F6758) {
    const uint32_t panel=ctx.r3.u32;
    if(Guest::Read32(base,panel)==0x820716DC) {
        const uint32_t children=Guest::Read32(base,panel+4);
        for(unsigned axis=0;children && axis<2;++axis) {
            const uint32_t slider=Guest::Read32(base,children+8+axis*4);
            if(ControllerAxis(base,slider)==int(axis)) ControllerInitialize(base,slider,int(axis));
        }
    }
    __imp__sub_824F6758(ctx,base);
}
#endif
