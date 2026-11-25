// NFSMW PC 1.3: modify only the existing local player's FE images.
// No vehicle class, AI, map item category, or global visibility setting is written.
struct MapImageBackup { DWORD image,guid,texture,color,lastTexture,lastColor; bool changed,geometryChanged; DWORD data,geometry[6],lastGeometry[6]; };
static MapImageBackup mapImages[2];
static DWORD nativeMinimapUpdate=0x0059DB50;
static bool mapEnabled=true;
static DWORD worldPlayerImage,worldCopColor;
static bool worldHasCopColor;
static int worldPolicy;
static DWORD worldSirenItem[16]; // private color/counter context, never attached to AI
extern "C" __declspec(dllexport) DWORD MapVehicle=0;
extern "C" __declspec(dllexport) LONG MapChanges=0;

// Only the current primary local human player may use the helicopter image.
// Independent of audio tasks: COPHELI uses SoundHeli, not SoundCop.
extern "C" __declspec(dllexport) DWORD __cdecl LocalPlayerHelicopter() {
    if(Read(0x00925E90)!=6 || Read(0x0092D884)!=1)return 0;
    DWORD list=Read(0x0092D87C);
    if(!Readable(list,4))return 0;
    DWORD player=Read(list);
    if(!Readable(player,0x2C) || Read(player)!=0x008B0B10 || !Readable(player-8,4))return 0;
    DWORD sim=Read(player-8),iv=sim+0x80;
    if(!Readable(sim,0x170) || Read(sim)!=0x008AA940 || Read(sim+0x58)!=player ||
       Read(iv)!=0x008AA828 || Read(iv+0x94)!=0)return 0;
    DWORD model=Read(iv+0xE0), table=Read(0x009B09D8);
    if(!table || model>=84 || !Readable(table+model*0xD0,0x98))return 0;
    DWORD car=table+model*0xD0;
    if(Read(car)!=0x48504F43 || Read(car+4)!=0x00494C45 || Read(car+0x94)!=1)return 0;
    return iv;
}
// Shared strict eligibility gate supports both AI-data and ordinary cop models.
static DWORD PlayerMapVehicle() {
    DWORD count=Read(0x00913E7C),tasks=Read(0x00913E74);
    if(count>256 || (count && !Readable(tasks,count*4)))return 0;
    for(DWORD i=0;i<count;++i) {
        DWORD task=Read(tasks+4*i);
        if(!Readable(task,12) || Read(task)!=0x00897740)continue;
        DWORD sound=Read(task+8);
        if(!Readable(sound,0x4C))continue;
        DWORD iv=EligibleVehicle(sound);
        if(iv)return iv;
    }
    return 0;
}
// 0=leave original image, 1=native non-pursuit grey, 2=police lights active.
extern "C" __declspec(dllexport) int __cdecl MapPolicy(DWORD iv,BOOL extraOptions,BOOL nonPursuit) {
    if(!mapEnabled || !iv || !extraOptions)return 0;
    if(Read(iv+0x9C)&0x7000)return 2;
    return nonPursuit?1:0;
}
static DWORD MapColor(int state,DWORD phase) {
    if(state!=2)return 0xFFCCCCCC;
    phase%=9;
    if(phase<=2)return 0xFF0000FF;
    if(phase>=4 && phase<=6)return 0xFFA00000;
    return 0xFFCCCCCC;
}
extern "C" __declspec(dllexport) void __cdecl ApplyMapImage(DWORD index,DWORD image,DWORD cop,int state,DWORD phase) {
    if(index>=2 || !Readable(image,0x30) || Read(image+0x18)!=1)return;
    MapImageBackup& saved=mapImages[index];
    typedef DWORD (__cdecl *GetColor)(DWORD);
    typedef void (__cdecl *SetValue)(DWORD,DWORD);
    SetValue texture=(SetValue)0x00515B90, color=(SetValue)0x005157E0;
    DWORD current=Read(image+0x24), tint=((GetColor)0x0051A180)(image), guid=Read(image+0x0C);
    if(saved.image!=image || saved.guid!=guid || !saved.changed ||
       (current!=saved.lastTexture && current!=saved.texture)) {
        saved.image=image;saved.guid=guid;saved.texture=current;saved.color=tint;saved.changed=false;saved.geometryChanged=false;
    }
    const DWORD offsets[6]={0x38,0x3C,0x44,0x48,0x4C,0x50};
    DWORD data=Read(image+0x2C);
    if(state!=3 && saved.geometryChanged && data==saved.data && Readable(data,0x54)) {
        for(unsigned n=0;n<6;++n)if(Read(data+offsets[n])==saved.lastGeometry[n])
            *(DWORD*)(data+offsets[n])=saved.geometry[n];
        *(DWORD*)(image+0x1C)|=0x00400000;saved.geometryChanged=false;
    }
    if(!state || !Readable(cop,0x28) || Read(cop+0x18)!=1 || !Read(cop+0x24)) {
        if(saved.changed) {
            // Restore only properties still carrying our last value.
            if(current==saved.lastTexture)texture(image,saved.texture);
            if(tint==saved.lastColor)color(image,saved.color);
            saved.changed=false;
        }
        return;
    }
    DWORD sourceData=Read(cop+0x2C);
    if(state==3 && Readable(data,0x54) && Readable(sourceData,0x54)) {
        if(!saved.geometryChanged || data!=saved.data) {
            saved.data=data;
            for(unsigned n=0;n<6;++n)saved.geometry[n]=Read(data+offsets[n]);
        }
        for(unsigned n=0;n<6;++n) {
            saved.lastGeometry[n]=Read(sourceData+offsets[n]);
            *(DWORD*)(data+offsets[n])=saved.lastGeometry[n];
        }
        saved.geometryChanged=true;*(DWORD*)(image+0x1C)|=0x00400000;
    }
    DWORD wanted=Read(cop+0x24), wantedColor=state==3?((GetColor)0x0051A180)(cop):MapColor(state,phase);
    texture(image,wanted);
    if(index!=1 || state!=2)color(image,wantedColor);
    if(!saved.changed)++MapChanges;
    saved.lastTexture=wanted;
    saved.lastColor=(index==1 && state==2)?tint:wantedColor;
    saved.changed=true;
}
static int CurrentMapPolicy() {
    DWORD heli=LocalPlayerHelicopter();
    if(mapEnabled && heli){MapVehicle=heli;return 3;}
    // Test the loaded module, not merely the presence of its INI/file.
    bool extra=GetModuleHandleA("NFSMWExtraOptions.asi")!=0;
    DWORD iv=PlayerMapVehicle(); MapVehicle=iv;
    return MapPolicy(iv,extra,*(const BYTE*)0x0091CF00!=0);
}
extern "C" __declspec(dllexport) void __cdecl UpdateHeliCone(DWORD,DWORD,const char*,bool);
static void __fastcall MinimapUpdateHook(DWORD minimap,DWORD,DWORD player) {
    typedef void (__thiscall *Update)(DWORD,DWORD);
    ((Update)nativeMinimapUpdate)(minimap,player);
    if(!Readable(minimap,0xD4))return;
    // +90 is local primary player; +94 is the second player and is untouched.
    int state=CurrentMapPolicy();
    DWORD source=Read(minimap+0xD0);
    if(state==3) {
        typedef DWORD (__cdecl *GetObject)(const char*,DWORD);
        source=((GetObject)0x00524850)((const char*)Read(minimap+0x10),0xF66AC1B6); // HELICOPTER_ICON
    }
    ApplyMapImage(0,Read(minimap+0x90),source,state,Read(minimap+0xC0));
    UpdateHeliCone(0,Read(minimap+0x90),(const char*)Read(minimap+0x10),state==3);
}
static DWORD HashMapName(const char* s) {
    DWORD h=0xFFFFFFFF;while(*s)h=h*33+(BYTE)*s++;return h;
}
#include "PlayerHeliCone.h"
static void UpdateWorldMapIcon() {
    if(Read(0x00925E90)!=6)return;
    RefreshHeliLayerVisibility();
    typedef DWORD (__cdecl *GetObject)(const char*,DWORD);
    GetObject get=(GetObject)0x00524850;
    // This exact image is used by 00530F40 to build the real player's map item.
    DWORD image=get("WorldMapMain.fng",0xDD9EF5FF);
    if(!image) { worldPlayerImage=0;worldPolicy=0;worldHasCopColor=false;return; }
    if(image!=worldPlayerImage) {
        worldPlayerImage=image;worldHasCopColor=false;
        // Use the native constructor's initial counter, not a chosen interval.
        worldSirenItem[0x38/4]=Read(0x0052F62E);
    }
    worldPolicy=CurrentMapPolicy();
    DWORD cop=get("WorldMapMain.fng",worldPolicy==3?0xF66AC1B6:HashMapName("MMICON_COPCAR_0"));
    ApplyMapImage(1,image,cop,worldPolicy,0);
    UpdateHeliCone(1,image,"WorldMapMain.fng",worldPolicy==3);
}
static void RememberWorldColor(DWORD value) {
    if(!worldPlayerImage || worldPolicy!=2)return;
    typedef void (__cdecl *SetColor)(DWORD,DWORD);
    ((SetColor)0x005157E0)(worldPlayerImage,value);
    mapImages[1].lastColor=value;
}
static void __fastcall WorldPlayerUpdateHook(DWORD item,DWORD) {
    // Original MapItem::Update at 0051EFD0 is RET (no operation).
    if(worldPlayerImage && Read(item+0x0C)==worldPlayerImage && worldPolicy==3) {
        UpdateHeliCone(1,worldPlayerImage,"WorldMapMain.fng",true);AnimateHeliCone(1);return;
    }
    if(!worldPlayerImage || Read(item+0x0C)!=worldPlayerImage || worldPolicy!=2)return;
    worldSirenItem[0x0C/4]=worldPlayerImage;
    worldSirenItem[0x34/4]=*(BYTE*)(item+0x34);
    typedef void (__thiscall *NativeCopUpdate)(DWORD);
    // EXACT CopItem::Update: same UI call cadence, color gates, increment and wrap.
    ((NativeCopUpdate)0x0051F6C0)((DWORD)worldSirenItem);
    typedef DWORD (__cdecl *GetColor)(DWORD);
    mapImages[1].lastColor=((GetColor)0x0051A180)(worldPlayerImage);
    if(worldHasCopColor)RememberWorldColor(worldCopColor);
    worldHasCopColor=false; // a disappeared cop cannot freeze our private native cycle
}
static void __fastcall WorldCopUpdateHook(DWORD item,DWORD) {
    typedef void (__thiscall *NativeCopUpdate)(DWORD);
    ((NativeCopUpdate)0x0051F6C0)(item); // real cop executes its original update once
    if(*(BYTE*)(item+0x34) || !Readable(Read(item+0x0C),0x30))return;
    typedef DWORD (__cdecl *GetColor)(DWORD);
    worldCopColor=((GetColor)0x0051A180)(Read(item+0x0C));
    worldHasCopColor=true;
    // Match a real cop's displayed phase regardless of item-update order.
    worldSirenItem[0x38/4]=Read(item+0x38);
    RememberWorldColor(worldCopColor);
}
static bool InstallMapHook() {
    if(Read(0x008A2044)!=nativeMinimapUpdate || Read(0x0089C350)!=0x0051EFD0 ||
       Read(0x0089D774)!=0x0051F6C0 || Read(0x0052F62E)!=0xFFFFFFFF)return false;
    const BYTE tex[]={0x8B,0x44,0x24,0x04,0x85,0xC0};
    if(!Match(0x00515B90,tex,sizeof(tex)))return false;
    if(!InstallHeliScriptHook())return false;
    DWORD old;
    if(!VirtualProtect((void*)0x008A2044,4,PAGE_READWRITE,&old))return false;
    *(DWORD*)0x008A2044=(DWORD)&MinimapUpdateHook;
    DWORD unused;VirtualProtect((void*)0x008A2044,4,old,&unused);
    if(!VirtualProtect((void*)0x0089C350,4,PAGE_READWRITE,&old))return false;
    *(DWORD*)0x0089C350=(DWORD)&WorldPlayerUpdateHook;
    VirtualProtect((void*)0x0089C350,4,old,&unused);
    if(!VirtualProtect((void*)0x0089D774,4,PAGE_READWRITE,&old))return false;
    *(DWORD*)0x0089D774=(DWORD)&WorldCopUpdateHook;
    VirtualProtect((void*)0x0089D774,4,old,&unused);
    return true;
}
