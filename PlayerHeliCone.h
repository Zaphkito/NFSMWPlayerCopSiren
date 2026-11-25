// A private, package-owned clone of the native visual cone. No AI targeting state.
struct PlayerHeliCone { DWORD owner,guid,ownerHash,image,cloneGuid,rotor,rotorGuid; float rotorOffset[3]; DWORD nativeItem[16]; };
static PlayerHeliCone heliCones[2];
// Native FE scripts animate translation as well as rotation. Correct only our
// clone after its script runs, including when it is nested inside an FE group.
extern "C" __declspec(dllexport) DWORD __fastcall PlayerHeliScriptHook(DWORD package,DWORD,DWORD object,DWORD dt) {
    typedef DWORD (__thiscall *Update)(DWORD,DWORD,DWORD);
    DWORD result=((Update)0x005C17E0)(package,object,dt);
    for(unsigned i=0;i<2;++i) {
        PlayerHeliCone& c=heliCones[i];
        if(object!=c.rotor || !Readable(object,0x60) || Read(object+0x0C)!=c.rotorGuid ||
           !Readable(c.owner,0x5C) || Read(c.owner+0x0C)!=c.guid || Read(c.owner+0x10)!=c.ownerHash)continue;
        DWORD data=Read(object+0x2C),playerData=Read(c.owner+0x2C);
        if(!Readable(data,0x54) || !Readable(playerData,0x44))continue;
        for(unsigned n=0;n<3;++n)*(float*)(data+0x1C+n*4)=
            *(float*)(playerData+0x1C+n*4)+c.rotorOffset[n];
        // Mark the corrected transform dirty for this frame's renderer.
        *(DWORD*)(object+0x1C)|=0x02400000;
    }
    return result;
}
static bool InstallHeliScriptHook() {
    const DWORD sites[]={0x0057310B,0x005B1689,0x005C1B66};
    for(unsigned i=0;i<3;++i)
        if(*(BYTE*)sites[i]!=0xE8 || sites[i]+5+Read(sites[i]+1)!=0x005C17E0)return false;
    DWORD old[3];unsigned acquired=0;
    for(;acquired<3;++acquired)
        if(!VirtualProtect((void*)sites[acquired],5,PAGE_EXECUTE_READWRITE,&old[acquired]))break;
    if(acquired!=3) {
        while(acquired) { --acquired;DWORD unused;VirtualProtect((void*)sites[acquired],5,old[acquired],&unused); }
        return false;
    }
    for(unsigned i=0;i<3;++i) {
        *(DWORD*)(sites[i]+1)=(DWORD)&PlayerHeliScriptHook-sites[i]-5;
        DWORD unused;VirtualProtect((void*)sites[i],5,old[i],&unused);
        FlushInstructionCache(GetCurrentProcess(),(void*)sites[i],5);
    }
    return true;
}
static DWORD FindPlayerImageList(DWORD list,DWORD target,unsigned depth) {
    if(depth>16 || !Readable(list,16))return 0;
    DWORD count=Read(list+4),node=Read(list+8);
    if(count>4096)return 0;
    for(DWORD i=0;i<count && node;++i) {
        if(!Readable(node,0x5C))return 0;
        if(node==target)return list;
        if(Read(node+0x18)==5 && Readable(node,0x6C)) {
            DWORD child=FindPlayerImageList(node+0x5C,target,depth+1);
            if(child)return child;
        }
        node=Read(node+4);
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD __cdecl AttachPlayerCone(DWORD list,DWORD playerImage,DWORD source) {
    if(!Readable(list,16) || !Readable(playerImage,0x5C) || !Readable(source,0x60) ||
       Read(source)!=0x008A2E34 || Read(source+0x18)!=1 || Read(0x008A2E38)!=0x005C44B0)return 0;
    DWORD previous=Read(playerImage+8);
    if(previous) { if(!Readable(previous,12) || Read(previous+4)!=playerImage)return 0; }
    else if(Read(list+8)!=playerImage)return 0;
    if(Read(list+4)>=4096)return 0;
    typedef DWORD (__thiscall *Clone)(DWORD,bool);
    DWORD cone=((Clone)0x005C44B0)(source,false);
    if(!cone)return 0;
    // The native clone owns distinct FEObjData; never change a shared AI buffer.
    if(!Readable(cone,0x60))return 0;
    DWORD data=Read(cone+0x2C);
    if(data==Read(source+0x2C) || !Readable(data,0x54) || !Readable(Read(source+0x2C),0x54)) {
        typedef void (__thiscall *Destroy)(DWORD,bool);
        ((Destroy)Read(Read(cone)))(cone,true);
        return 0;
    }
    // Clone creates its own data but does not copy the current authored state.
    for(DWORD o=0;o<0x54;o+=4)*(DWORD*)(data+o)=Read(Read(source+0x2C)+o);
    // Keep the native clone-owned current script valid: the UI updater requires it.
    // The stock cone script supplies color; position/heading are set below.
    *(DWORD*)(cone+0x10)=HashMapName(Read(source+0x10)==0x2C68F8B8?"PLAYERCOPSIREN_PRIVATE_HELI_BLADES":"PLAYERCOPSIREN_PRIVATE_HELI_CONE");
    *(DWORD*)(cone+0x58)=0; // never reuse a renderer cached for the source object
    *(DWORD*)(cone+4)=playerImage;*(DWORD*)(cone+8)=previous;
    if(previous)*(DWORD*)(previous+4)=cone;else *(DWORD*)(list+8)=cone;
    *(DWORD*)(playerImage+8)=cone;
    ++*(DWORD*)(list+4);
    return cone; // list/package destruction now owns the complete clone
}
static void AnimateHeliCone(DWORD index) {
    if(index>1 || !heliCones[index].image)return;
    heliCones[index].nativeItem[0x3C/4]=heliCones[index].image;
    typedef void (__thiscall *HeliVisualUpdate)(DWORD);
    ((HeliVisualUpdate)0x0051F730)((DWORD)heliCones[index].nativeItem);
}
static bool PlayerHeliHudVisible() {
    if(Read(0x0092D884)!=1)return false;
    DWORD list=Read(0x0092D87C);if(!Readable(list,4))return false;
    DWORD player=Read(list);if(!Readable(player,0x2C) || Read(player)!=0x008B0B10)return false;
    DWORD hud=Read(player+0x28);if(!Readable(hud,0x2C) || Read(hud)!=0x008A2538)return false;
    typedef bool (__thiscall *Visible)(DWORD);typedef bool (__cdecl *Paused)();
    return ((Visible)0x00569680)(hud) && !((Paused)0x00468390)();
}
extern "C" __declspec(dllexport) void __cdecl RefreshHeliLayerVisibility() {
    if(PlayerHeliHudVisible())return;
    PlayerHeliCone& c=heliCones[0];
    if(!c.owner || Read(0x0092D884)!=1)return;
    DWORD list=Read(0x0092D87C);if(!Readable(list,4))return;
    DWORD player=Read(list);if(!Readable(player,0x2C))return;
    DWORD hud=Read(player+0x28);if(!Readable(hud,0x2C))return;
    typedef DWORD (__cdecl *GetObject)(const char*,DWORD);
    DWORD live=((GetObject)0x00524850)((const char*)Read(hud+0x24),c.ownerHash);
    if(live!=c.owner || !Readable(live,0x5C) || Read(live+0x0C)!=c.guid)return;
    typedef void (__cdecl *Hide)(DWORD);
    if(c.image && Readable(c.image,0x60) && Read(c.image+0x0C)==c.cloneGuid)((Hide)0x00514C70)(c.image);
    if(c.rotor && Readable(c.rotor,0x60) && Read(c.rotor+0x0C)==c.rotorGuid)((Hide)0x00514C70)(c.rotor);
}
extern "C" __declspec(dllexport) void __cdecl UpdateHeliCone(DWORD index,DWORD playerImage,const char* packageName,bool active) {
    if(index>1 || !Readable(playerImage,0x5C))return;
    PlayerHeliCone& context=heliCones[index];
    if(context.owner!=playerImage || context.guid!=Read(playerImage+0x0C)) {
        context.owner=playerImage;context.guid=Read(playerImage+0x0C);context.ownerHash=Read(playerImage+0x10);context.image=0;context.rotor=0;
        for(unsigned n=0;n<16;++n)context.nativeItem[n]=0;
        context.nativeItem[0x38/4]=0xFFFFFFFF;
    }
    if(context.image && (!Readable(context.image,0x60) || Read(context.image+0x0C)!=context.cloneGuid))context.image=0;
    typedef void (__cdecl *Visibility)(DWORD);
    if(index==0 && !PlayerHeliHudVisible())active=false;
    if(!active) {
        if(context.image)((Visibility)0x00514C70)(context.image);
        if(context.rotor && Readable(context.rotor,0x60) && Read(context.rotor+0x0C)==context.rotorGuid)((Visibility)0x00514C70)(context.rotor);
        return;
    }
    if(!context.image) {
        typedef DWORD (__cdecl *GetObject)(const char*,DWORD);
        DWORD source=((GetObject)0x00524850)(packageName,0x21390E47); // HELICOPTER_LINE_OF_SIGHT
        typedef DWORD (__thiscall *GetPackage)(DWORD,const char*);
        DWORD package=((GetPackage)0x00516AF0)(Read(0x0091CADC),packageName);
        if(!Readable(package,0x54))return;
        DWORD list=FindPlayerImageList(package+0x44,playerImage,0);
        if(!list)return;
        context.image=AttachPlayerCone(list,playerImage,source);
        if(!context.image)return;
        context.cloneGuid=Read(context.image+0x0C);
    }
    if(!context.rotor) {
        typedef DWORD (__cdecl *GetObject)(const char*,DWORD);
        typedef DWORD (__thiscall *GetPackage)(DWORD,const char*);
        DWORD source=((GetObject)0x00524850)(packageName,0x2C68F8B8);
        DWORD body=((GetObject)0x00524850)(packageName,0xF66AC1B6);
        DWORD package=((GetPackage)0x00516AF0)(Read(0x0091CADC),packageName);
        DWORD list=Readable(package,0x54)?FindPlayerImageList(package+0x44,playerImage,0):0;
        if(list && Readable(source,0x60) && Readable(body,0x60) &&
           Readable(Read(source+0x2C),0x54) && Readable(Read(body+0x2C),0x54)) {
            context.rotor=AttachPlayerCone(list,playerImage,source);
            if(context.rotor) {
                context.rotorGuid=Read(context.rotor+0x0C);
                for(unsigned n=0;n<3;++n)context.rotorOffset[n]=
                    *(float*)(Read(source+0x2C)+0x1C+n*4)-*(float*)(Read(body+0x2C)+0x1C+n*4);
            }
        }
    }
    DWORD cone=context.image,data=Read(cone+0x2C),playerData=Read(playerImage+0x2C);
    if(!Readable(data,0x54) || !Readable(playerData,0x44))return;
    // Same parent coordinate system: inherit the real player's position/rotation.
    // Keep the native cone's pivot, dimensions, UVs, texture and translucent color.
    for(DWORD o=0x1C;o<0x38;o+=4)*(DWORD*)(data+o)=Read(playerData+o);
    *(DWORD*)(cone+0x1C)|=0x00400000;
    ((Visibility)((Read(playerImage+0x1C)&1)?0x00514C70:0x00514CC0))(cone);
    // Minimap range is steady. Only the world-map item callback runs its native pulse.
    if(context.rotor && Readable(context.rotor,0x60) && Read(context.rotor+0x0C)==context.rotorGuid) {
        DWORD rotorData=Read(context.rotor+0x2C);
        if(Readable(rotorData,0x54)) {
            for(unsigned n=0;n<3;++n)*(float*)(rotorData+0x1C+n*4)=
                *(float*)(playerData+0x1C+n*4)+context.rotorOffset[n];
            *(DWORD*)(context.rotor+0x1C)|=0x00400000;
            ((Visibility)((Read(playerImage+0x1C)&1)?0x00514C70:0x00514CC0))(context.rotor);
            // Rotation remains driven by this clone's native HELICOPTER_BLADES script.
        }
    }
}
