#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xmmintrin.h>
extern "C" int _fltused=0;

// Experimental NFSMW PC 1.3 x86 plugin. All addresses verified against the
// user's executable and a live memory dump. No AI vehicle state is written.
// The private parameter block below uses exactly the native SIREN module that
// CARSFX_Siren uses at 004F03A0 (register/update/release are native sound calls).
struct SirenParams {
    DWORD handle;
    int gain, pan, doppler, speed, mode, cutoff, auxiliary, mix, reverb;
};
static SirenParams params;
static DWORD ownerSim, mixerGeneration, moduleGeneration;
static DWORD retryAt;
static int enabled=1, volume=70;
static DWORD nativeStart, nativeTimer;
static float nativeWail=11.0f, nativeYelp=6.0f;
static int nativeTone, escalationLevel=2;
extern "C" __declspec(dllexport) DWORD PolicySim=0;
static bool damageActive;
static DWORD damageSince;
static float escalationAfter=150.0f;
static bool nativeIntro;
static bool controlsActive;
extern "C" __declspec(dllexport) int CurrentMode=0;
static HANDLE logFile=INVALID_HANDLE_VALUE;
static DWORD nativeFrame=0x00480E40;
extern "C" __declspec(dllexport) volatile LONG Starts=0;
extern "C" __declspec(dllexport) volatile LONG Stops=0;
extern "C" __declspec(dllexport) volatile LONG StartFailures=0;
extern "C" __declspec(dllexport) DWORD ActiveSim=0;

static DWORD Read(DWORD a) { return *(const DWORD*)a; }
static void Log(const char* text) {
    if(logFile!=INVALID_HANDLE_VALUE) {
        DWORD written; WriteFile(logFile,text,lstrlenA(text),&written,0);
        FlushFileBuffers(logFile);
    }
}

static bool Readable(DWORD p,DWORD bytes) {
    MEMORY_BASIC_INFORMATION info;
    if(!p || !VirtualQuery((void*)p,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    DWORD protection=info.Protect&0xFF;
    if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
       protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return false;
    return p>=(DWORD)info.BaseAddress && p+bytes>=p &&
        p+bytes<=(DWORD)info.BaseAddress+info.RegionSize;
}
static float ReadSeconds(DWORD address,float fallback) {
    float seconds=*(const float*)address;
    if(!(seconds>=0.1f && seconds<=3600.0f))return fallback;
    return seconds;
}
static double NativeSeconds(DWORD ticks) {
    // EXACT native clock factor at 00890984, NOT milliseconds. Use double
    // to retain the native extended product's sub-tick comparison behavior.
    return (double)(LONG)ticks * (double)0.00025f;
}
// Read-only COM lookup, equivalent to the game's interface lookup, without
// calling constructors or attaching anything to the vehicle.
static DWORD VehicleInterface(DWORD sim,DWORD handle) {
    if(!Readable(sim,8))return 0;
    DWORD object=Read(sim+4);
    if(!Readable(object,12))return 0;
    DWORD first=Read(object+4), last=Read(object+8);
    if(last<first || last-first>1024 || (last-first)%8 || !Readable(first,last-first))return 0;
    for(DWORD p=first;p<last;p+=8)if(Read(p)==handle)return Read(p+4);
    return 0;
}
extern "C" __declspec(dllexport) bool __cdecl PlayerLightsDamaged(DWORD sim) {
    DWORD damage=VehicleInterface(sim,0x00620FD0);
    if(!Readable(damage,4))return false;
    DWORD table=Read(damage);
    if(!Readable(table,8) || Read(table+4)!=0x0068C1D0)return false;
    typedef bool (__thiscall *IsLightDamaged)(DWORD,DWORD);
    return ((IsLightDamaged)0x0068C1D0)(damage,0x7000);
}
// Only a real pursuit whose AITarget is THIS player's sim can escalate.
// IPursuit registration: 0042E1E0 -> 00425220, vector 0092FD28.
// Concrete vtable 00892770, GetTarget 0040AE70 reads interface+74.
// Never invoke a virtual method while inspecting this list. In particular,
// 0092FD5C/64 is IRoadBlock, NOT IPursuit.
static bool RealPursuit(float* age) {
    DWORD count=Read(0x0092FD34), list=Read(0x0092FD2C);
    if(!PolicySim || count>64 || !count || !Readable(list,count*4))return false;
    for(DWORD i=0;i<count;++i) {
        DWORD ip=Read(list+4*i);
        if(!Readable(ip,0x78) || Read(ip)!=0x00892770)continue;
        DWORD target=Read(ip+0x74);
        if(!Readable(target,0x20) || Read(target+0x1C)!=PolicySim)continue;
        DWORD pursuit=Read(0x00993CC8);
        if(!Readable(pursuit,0x19C) || Read(pursuit)!=0x008B24E4)return false;
        float seconds=*(const float*)(pursuit+0x140);
        if(!(seconds>=0.0f && seconds<=86400.0f))return false;
        *age=seconds;
        return (int)Read(pursuit+0x104)>=escalationLevel || seconds>=escalationAfter;
    }
    return false;
}
static void ResetNativePolicy(DWORD now) {
    nativeStart=nativeTimer=now; nativeTone=0; nativeIntro=true;
    nativeWail=11.0f; nativeYelp=6.0f; escalationLevel=2; escalationAfter=150.0f;
    // Copy tuning read-only; all cycle timers belong to this plugin.
    DWORD pursuit=Read(0x00993CC8);
    if(Readable(pursuit,0x19C)) {
        DWORD support=Read(pursuit+0x198), levels=Read(pursuit+0x184);
        if(Readable(support,0xC0)) {
            nativeWail=ReadSeconds(support+0x7C,nativeWail);
            nativeYelp=ReadSeconds(support+0x58,nativeYelp);
        }
        if(Readable(levels,0xB1)) {
            int threshold=*(const signed char*)(levels+0xB0);
            if(threshold>=1 && threshold<=10)escalationLevel=threshold;
            escalationAfter=ReadSeconds(levels+0x94,escalationAfter);
        }
    }
}
// Translated ordinary engaged-pursuit branch of 00416B90..004170CA.
// No synthetic pursuit level, duration, or tactical events are created.
extern "C" __declspec(dllexport) int __cdecl NativeAutoTone(DWORD now) {
    float age=6.0f; // no real pursuit: ordinary Wail/Yelp, no acquisition intro
    bool escalated=RealPursuit(&age);
    // Native initial acquisition window: 6 s, including the 3 s scream
    // introduction only when the native escalation gate is satisfied.
    if(age<6.0f) {
        nativeTone=escalated && age<3.0f ? 2 : 1;
        return nativeTone;
    }
    // An ended/replaced pursuit must not leave its acquisition scream latched.
    if(nativeTone==2)nativeTone=0;
    if(nativeIntro) {
        nativeIntro=false; nativeTimer=now; nativeTone=escalated?1:0;
    }
    double elapsed=NativeSeconds(now-nativeTimer);
    // Preserve native strict '>' boundaries and timer-reset semantics.
    if(elapsed>(double)nativeWail+(double)nativeYelp) { nativeTone=0; nativeTimer=now; }
    else if(elapsed>nativeWail)nativeTone=1;
    return nativeTone;
}

// Pure read-only ownership/type gate, also exercised as compiled x86 code by
// the offline test harness. Sound is provided by the game's live task list.
// Local ownership AND Human control AND CarTypeInfo::UsageType==Cop are needed.
// DamageCopCar/SoundCop/AIVehicleCopCar are deliberately not prerequisites.
extern "C" __declspec(dllexport) DWORD __cdecl EligibleVehicle(DWORD sound) {
    if(!sound)return 0;
    DWORD sim=Read(sound+0x34), iv=Read(sound+0x48);
    if(!sim || !iv || Read(sim)!=0x008AA940 ||
       iv!=sim+0x80 || Read(iv)!=0x008AA828)return 0;
    DWORD player=Read(sim+0x58);
    if(!player || Read(player)!=0x008B0B10 || Read(player-8)!=sim)return 0;
    if(Read(iv+0x94)!=0)return 0; // DriverClass::Human
    DWORD model=Read(iv+0xE0), table=Read(0x009B09D8);
    // This release deliberately supports the stock 84-entry model table only.
    // Unknown/added IDs fail closed, rather than guessing their vehicle type.
    if(!table || model>=84)return 0;
    DWORD car=table+model*0xD0;
    if(Read(car+0x94)!=1)return 0; // CarUsageType::Cop, including COPMIDSIZEINT
    // The game's helicopter is also UsageType::Cop; it is not a police car.
    if(Read(car)==0x48504F43 && Read(car+4)==0x00494C45)return 0; // "COPHELI"
    return iv;
}

static bool Match(DWORD a,const BYTE* p,DWORD n);
#include "PlayerCopMap.h"

static void StopSiren() {
    controlsActive=false;
    damageActive=false; PolicySim=0;
    if(params.handle) {
        // Never hand a handle from a previous sound-engine generation back
        // to a newly initialized engine. Native teardown owns old resources.
        if(Read(0x00911FA8)==mixerGeneration && Read(0x00914934)==moduleGeneration) {
            typedef void (__thiscall *Release)(DWORD);
            ((Release)0x00831782)(params.handle);
        }
        params.handle=0;
        ++Stops; Log("STOP: released this plugin's private SIREN only.\r\n");
    }
    ownerSim=0; ActiveSim=0;
}

// Player-only normalized native policy, NOT an invocation of AIVehicleCopCar AI.
// Uses the same 4000-tick/second simulation clock as native AI siren timers.
extern "C" __declspec(dllexport) int __cdecl ProcessAutoTone(DWORD now) {
    if(!controlsActive) {
        controlsActive=true;
        ResetNativePolicy(now);
    }
    int mode=NativeAutoTone(now);
    if(CurrentMode!=mode) {
        CurrentMode=mode;
        Log(mode==0?"TONE: Wail.\r\n":mode==1?"TONE: Yelp.\r\n":"TONE: Scream.\r\n");
    }
    return mode;
}

extern "C" __declspec(dllexport) void __cdecl UpdatePlayerSiren() {
    DWORD mixer=Read(0x00911FA8), module=Read(0x00914934);
    typedef bool (__cdecl *IsPaused)();
    if(!enabled || !mixer || !module || Read(0x00925E90)!=6 || Read(0x009885C8) ||
       ((IsPaused)0x00468390)()) {
        StopSiren(); return;
    }
    DWORD foreground=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()) { StopSiren(); return; }
    DWORD count=Read(0x00913E7C), tasks=Read(0x00913E74), found=0;
    if(count>256 || (count && !tasks)) { StopSiren(); return; }
    for(DWORD i=0;i<count;++i) {
        DWORD task=Read(tasks+4*i);
        if(!task || Read(task)!=0x00897740)continue;
        DWORD sound=Read(task+8), iv=EligibleVehicle(sound);
        if(iv) {
            // Read actual lights; do not compete with Extra Options for O.
            if((Read(iv+0x9C)&0x7000)!=0)found=Read(sound+0x34);
            break;
        }
    }
    if(!found) { StopSiren(); return; }
    if(ownerSim && (found!=ownerSim || mixer!=mixerGeneration || module!=moduleGeneration))StopSiren();
    PolicySim=found;
    DWORD clock=Read(0x00925AE8);
    int selected=ProcessAutoTone(clock);
    // SoundCop's native damaged-police-light override applies even when the
    // car is not destroyed. Native Die is bounded to 10 seconds per damage edge.
    bool damaged=PlayerLightsDamaged(found);
    if(damaged && !damageActive) {
        damageSince=clock; damageActive=true;
        Log("DAMAGE: native police-light damage; Die override (up to 10 seconds).\r\n");
    }
    if(!damaged)damageActive=false;
    if(damageActive) {
        selected=3;
        if(NativeSeconds(clock-damageSince)>10.0) {
            // Silence this voice without resetting the damage latch or controls.
            if(params.handle) {
                typedef void (__thiscall *Release)(DWORD);
                ((Release)0x00831782)(params.handle);
                params.handle=0; ++Stops;
            }
            return;
        }
    }
    if(!params.handle) {
        DWORD now=GetTickCount();
        if(retryAt && (LONG)(now-retryAt)<0)return;
        params.gain=(32767*volume)/100;
        params.pan=0; params.doppler=0; params.speed=0;
        params.mode=selected; params.cutoff=25000; params.auxiliary=0;
        params.mix=32767; params.reverb=0;
        mixerGeneration=mixer; moduleGeneration=module;
        // Same register call/descriptor/parameter layout as 004F0403..004F0454.
        // The descriptor is already registered by native audio initialization;
        // do not reload it or modify the settings of any existing source.
        typedef int (__cdecl *Register)(DWORD,const void*,DWORD*);
        int result=((Register)0x00831A8A)(0x00914934,&params.gain,&params.handle);
        if(result<0 || !params.handle) {
            params.handle=0; retryAt=now+2000;
            if(++StartFailures==1)Log("FAIL: native SIREN register failed; retry limited to once per 2 seconds.\r\n");
            return;
        }
        ownerSim=found; ActiveSim=found; retryAt=0;
        ++Starts; Log("START: private native SIREN; reciprocal local Human owner; UsageType=Cop; police lights on.\r\n");
    }
    params.mode=selected; // Same private native voice; update tone in place.
    typedef int (__thiscall *Submit)(DWORD,const void*);
    ((Submit)0x0083179C)(params.handle,&params.gain);
}

// Preserve the original call's stack, registers, flags and complete FP/SIMD
// state. The original per-frame function still executes once, with its real dt.
extern "C" __declspec(dllexport,naked) void FrameHook() {
    __asm {
        push dword ptr [esp+4]
        call dword ptr [nativeFrame]
        lea esp,[esp+4]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        call UpdateWorldMapIcon
        call UpdatePlayerSiren
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}

static bool Match(DWORD a,const BYTE* p,DWORD n) {
    for(DWORD i=0;i<n;++i)if(*(BYTE*)(a+i)!=p[i])return false;
    return true;
}
static bool Install() {
    if((DWORD)GetModuleHandleW(0)!=0x00400000)return false;
    const BYTE site[]={0xE8,0xE7,0x66,0xE3,0xFF};
    const BYTE frame[]={0x6A,0xFF,0x68,0xD2,0xA5,0x86,0,0x64,0xA1,0,0,0,0};
    const BYTE submit[]={0x56,0x8B,0xF1,0xE8,0x85,0xF8,0xFF,0xFF};
    const BYTE release[]={0x56,0x8B,0xF1,0xE8,0x9F,0xF8,0xFF,0xFF};
    const BYTE reg[]={0x56,0xE8,0x99,0xF5,0xFF,0xFF};
    const BYTE player[]={0x8B,0x41,0x58,0xC3};
    const BYTE model[]={0x8B,0x81,0xE0,0,0,0,0xC3};
    const BYTE driver[]={0x8B,0x81,0x94,0,0,0,0xC3};
    const BYTE glare[]={0x8B,0x81,0x9C,0,0,0};
    const BYTE carusage[]={0x8B,0x91,0x94,0,0,0,0xB8,1,0,0,0};
    const BYTE damagedLight[]={0x56,0x8B,0xF1,0x8B,0x46,0xF4};
    const BYTE pause[]={0xB9,0x70,0x5E,0x92,0,0xE9,0xE6,0x32,0x1E,0};
    if(!Match(0x64A754,site,sizeof(site)) || !Match(0x480E40,frame,sizeof(frame)) ||
       !Match(0x83179C,submit,sizeof(submit)) || !Match(0x831782,release,sizeof(release)) ||
       !Match(0x831A8A,reg,sizeof(reg)) || !Match(0x686A20,player,sizeof(player)) ||
       !Match(0x6880F0,model,sizeof(model)) || !Match(0x6880B0,driver,sizeof(driver)) ||
       !Match(0x6881C0,glare,sizeof(glare)) || !Match(0x739970,carusage,sizeof(carusage)) ||
       !Match(0x68C1D0,damagedLight,sizeof(damagedLight)) ||
       !Match(0x468390,pause,sizeof(pause)) ||
       Read(0x8B0B14)!=0x6F8FB0 || Read(0x8B0B18)!=0x6F8F40)return false;
    DWORD old;
    if(!VirtualProtect((void*)0x64A754,5,PAGE_EXECUTE_READWRITE,&old))return false;
    *(DWORD*)0x64A755=(DWORD)&FrameHook-0x64A759; // CALL opcode remains E8.
    DWORD unused;VirtualProtect((void*)0x64A754,5,old,&unused);
    FlushInstructionCache(GetCurrentProcess(),(void*)0x64A754,5);
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wchar_t path[MAX_PATH]; DWORD n=GetModuleFileNameW(instance,path,MAX_PATH);
        if(n && n<MAX_PATH-5) {
            // Replace the module extension instead of appending to .asi.
            DWORD stem=n;
            while(stem && path[stem-1]!=L'.' && path[stem-1]!=L'\\' && path[stem-1]!=L'/')--stem;
            if(stem && path[stem-1]==L'.')n=stem-1;
            lstrcpyW(path+n,L".ini");
            enabled=GetPrivateProfileIntW(L"Siren",L"Enabled",1,path)!=0;
            volume=(int)GetPrivateProfileIntW(L"Siren",L"Volume",70,path);
            if(volume<0)volume=0;if(volume>100)volume=100;
            bool logging=GetPrivateProfileIntW(L"Siren",L"Logging",0,path)!=0;
            if(logging) {
                lstrcpyW(path+n,L".log");
                logFile=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
            }
        }
        if(Install()) {
            Log("ENABLED v0.6.3: player siren, police/heli map icons; strict local Human gate.\r\n");
            Log(InstallMapHook()?"MAP: local player helicopter icons enabled; police-car icons require Extra Options.\r\n":
                "MAP: minimap signature conflict; minimap hook not installed.\r\n");
        } else Log("DISABLED: executable signature mismatch or hook conflict; no patch installed.\r\n");
    }
    // At process exit Windows tears down audio. Never call game code from
    // loader-lock detach or inject into an already running game.
    return TRUE;
}

