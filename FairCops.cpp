// FairCops - Need for Speed: Most Wanted (2005) v1.3
// Drop-in ASI for the retail 6,029,312-byte speed.exe.
//
// Purpose:
//   Keep NFSMW/Bartender's single formal non-player AIPursuit intact, but
//   distribute individual active pursuit cop targets fairly across the player
//   and AI racers. This avoids allocating additional non-player pursuit objects.
//
// Compatibility strategy:
//   * Hook only AICopManager::OnTask's CALL to UpdatePursuits at 0x43EF43.
//   * Chain any pre-existing E8 CALL target at that site.
//   * Run after the normal/Bartender pursuit update.
//   * Do not change cop spawning, pursuit objects, roadblock director, heat,
//     support timers, or racer/race-manager state.
//   * Change only each eligible cop AITarget via AITarget::Acquire().
//
// This file intentionally uses no CRT or Windows import library. It calls a
// few Win32 APIs through speed.exe's existing IAT so it can be linked as a
// freestanding 32-bit ASI in this build environment.

// ---- Minimal Win32 / integer types -------------------------------------------------
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed long    s32;
typedef int            BOOL;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          LPVOID;
typedef const char*    LPCSTR;
typedef unsigned long  DWORD;
typedef unsigned int   UINT;

#define WINAPI   __stdcall
#define DLL_PROCESS_DETACH 0u
#define DLL_PROCESS_ATTACH 1u
#define PAGE_EXECUTE_READWRITE 0x40u

// ---- Exact v1.3 addresses verified against the supplied speed.exe -----------------
static const u32 kImageBase                    = 0x00400000u;
static const u32 kHookSite                     = 0x0043EF43u; // CALL AICopManager::UpdatePursuits
static const u32 kVanillaUpdatePursuits        = 0x0043E8D0u;
static const u32 kAITargetAcquire               = 0x00423860u;

static const u32 kVehicleListData               = 0x0092CD1Cu; // IVehicle**
static const u32 kVehicleListCount              = 0x0092CD24u;
static const u32 kIVehicleVTable                = 0x008AA828u;
static const u32 kCopIVehicleAIVTable           = 0x00892480u;
static const u32 kCopPursuitVTable              = 0x008923E8u;

// IVehicle virtual slots, byte-verified for this executable.
static const u32 kSlotGetSimable                = 1u;
static const u32 kSlotGetDriverClass            = 22u;
static const u32 kSlotIsDestroyed               = 31u;
static const u32 kSlotIsActive                  = 34u;
static const u32 kSlotGetAIVehiclePtr           = 43u;

// IVehicleAI / cop layout relative to the IVehicleAI interface pointer.
static const u32 kAITargetPtrOffset              = 0x54u;
static const u32 kCurrentGoalOffset              = 0x78u;  // controller(+0xC4), interface is controller+0x4C
static const u32 kPursuitSubobjectOffset         = 0x70Cu; // cop pursuit subobject
static const u32 kPursuitInPursuitOffset         = 0x08u;

// AITarget fields.
static const u32 kAITargetSimableOffset          = 0x1Cu;
static const u32 kAITargetValidOffset            = 0x2Cu;

// AIGoalStaticRoadBlock hash; static roadblock holders are excluded by default.
static const u32 kGoalStaticRoadBlock            = 0x9E55B2E3u;

// speed.exe IAT entries used without importing kernel32 into FairCops itself.
static const u32 kIATGetModuleHandleA            = 0x0049008Cu;
static const u32 kIATGetProcAddress              = 0x00490094u;
static const u32 kIATOutputDebugStringA          = 0x00490190u;
static const u32 kIATGetModuleFileNameA          = 0x004901D4u;
static const u32 kIATVirtualProtect              = 0x00490294u;

// ---- Game enums -------------------------------------------------------------------
enum DriverClass : u32 {
    DriverHuman   = 0u,
    DriverTraffic = 1u,
    DriverCop     = 2u,
    DriverRacer   = 3u,
    DriverNone    = 4u,
    DriverNIS     = 5u,
    DriverRemote  = 6u
};

// ---- API function types ------------------------------------------------------------
typedef HMODULE (WINAPI *GetModuleHandleAFn)(LPCSTR);
typedef LPVOID  (WINAPI *GetProcAddressFn)(HMODULE, LPCSTR);
typedef void    (WINAPI *OutputDebugStringAFn)(LPCSTR);
typedef DWORD   (WINAPI *GetModuleFileNameAFn)(HMODULE, char*, DWORD);
typedef BOOL    (WINAPI *VirtualProtectFn)(LPVOID, DWORD, DWORD, DWORD*);
typedef UINT    (WINAPI *GetPrivateProfileIntAFn)(LPCSTR, LPCSTR, int, LPCSTR);
typedef BOOL    (WINAPI *FlushInstructionCacheFn)(HANDLE, const void*, unsigned long);
typedef HANDLE  (WINAPI *GetCurrentProcessFn)();

typedef void    (__thiscall *UpdatePursuitsFn)(void*);
typedef void    (__thiscall *AITargetAcquireFn)(void*, void*);
typedef u32     (__thiscall *GetDriverClassFn)(void*);
typedef bool    (__thiscall *VehicleBoolFn)(void*);
typedef void*   (__thiscall *VehiclePtrFn)(void*);

// ---- Configuration ----------------------------------------------------------------
struct Config {
    BOOL enabled;
    u32 rebalanceFrames;
    BOOL includePlayer;
    BOOL includeAIRacers;
    BOOL skipRoadblocks;
    u32 undersupplyRotationFrames;
    BOOL allowChainedCallHook;
};

static Config g_cfg = { 1, 30u, 1, 1, 1, 600u, 1 };
static char g_iniPath[260];

// ---- Runtime state -----------------------------------------------------------------
static HMODULE g_selfModule = 0;
static UpdatePursuitsFn g_previousUpdatePursuits = 0;
static BOOL g_hookInstalled = 0;
static u8 g_originalHookBytes[5] = {0,0,0,0,0};
static u32 g_frameCounter = 0u;
static u32 g_rotationCounter = 0u;
static u32 g_rotationOffset = 0u;

static GetPrivateProfileIntAFn g_GetPrivateProfileIntA = 0;
static FlushInstructionCacheFn g_FlushInstructionCache = 0;
static GetCurrentProcessFn g_GetCurrentProcess = 0;

static const u32 kMaxRacers = 16u;
static const u32 kMaxCops = 96u;

struct RacerInfo {
    void* vehicle; // IVehicle*
    void* simable; // ISimable*
};

struct CopInfo {
    void* vehicle; // IVehicle*
    void* ai;      // IVehicleAI* interface pointer
    void* pursuit; // AIVehiclePursuit subobject
};

struct Assignment {
    void* copVehicle;
    void* racerVehicle;
};

static RacerInfo g_racers[kMaxRacers];
static CopInfo g_cops[kMaxCops];
static Assignment g_assignments[kMaxCops];
static u32 g_racerCount = 0u;
static u32 g_copCount = 0u;
static u32 g_assignmentCount = 0u;

// ---- Small freestanding helpers ----------------------------------------------------
template <typename T>
static T ReadPtr(u32 address) {
    return *reinterpret_cast<T*>(address);
}

template <typename T>
static T VFunc(void* object, u32 slot) {
    u32* table = *reinterpret_cast<u32**>(object);
    return reinterpret_cast<T>(table[slot]);
}

static void DebugOut(const char* text) {
    OutputDebugStringAFn fn = ReadPtr<OutputDebugStringAFn>(kIATOutputDebugStringA);
    if (fn) fn(text);
}


static u32 ClampU32(u32 value, u32 lo, u32 hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static BOOL IsExpectedGameBuild() {
    // MZ signature and a few byte-verified interface/vtable anchors.
    if (*reinterpret_cast<u16*>(kImageBase) != 0x5A4Du) return 0;
    if (*reinterpret_cast<u32*>(kIVehicleVTable + 0u * 4u)  != 0x00688330u) return 0;
    if (*reinterpret_cast<u32*>(kIVehicleVTable + 22u * 4u) != 0x006880B0u) return 0;
    if (*reinterpret_cast<u32*>(kIVehicleVTable + 34u * 4u) != 0x00688200u) return 0;
    if (*reinterpret_cast<u32*>(kIVehicleVTable + 43u * 4u) != 0x00688230u) return 0;
    if (*reinterpret_cast<u32*>(kCopPursuitVTable + 6u * 4u) != 0x0042AB80u) return 0;
    return 1;
}

static void BuildIniPath() {
    g_iniPath[0] = 0;
    GetModuleFileNameAFn getModuleFileName = ReadPtr<GetModuleFileNameAFn>(kIATGetModuleFileNameA);
    if (!getModuleFileName) return;

    DWORD n = getModuleFileName(g_selfModule, g_iniPath, 259u);
    if (n == 0u || n >= 259u) {
        g_iniPath[0] = 0;
        return;
    }
    g_iniPath[n] = 0;

    // Keep the directory and replace this ASI's filename with FairCops.ini.
    s32 slash = -1;
    for (u32 i = 0; i < n; ++i) {
        if (g_iniPath[i] == '\\' || g_iniPath[i] == '/') slash = static_cast<s32>(i);
    }
    u32 pos = (slash >= 0) ? static_cast<u32>(slash + 1) : 0u;
    const char tail[] = "FairCops.ini";
    u32 j = 0u;
    while (tail[j] && pos + j < 259u) {
        g_iniPath[pos + j] = tail[j];
        ++j;
    }
    g_iniPath[pos + j] = 0;
}

static int ReadIniInt(const char* section, const char* key, int defaultValue) {
    if (!g_GetPrivateProfileIntA || !g_iniPath[0]) return defaultValue;
    return static_cast<int>(g_GetPrivateProfileIntA(section, key, defaultValue, g_iniPath));
}

static void LoadConfig() {
    g_cfg.enabled = ReadIniInt("Main", "Enabled", 1) != 0;
    g_cfg.rebalanceFrames = ClampU32(static_cast<u32>(ReadIniInt("Distribution", "RebalanceFrames", 30)), 1u, 600u);
    g_cfg.includePlayer = ReadIniInt("Distribution", "IncludePlayer", 1) != 0;
    g_cfg.includeAIRacers = ReadIniInt("Distribution", "IncludeAIRacers", 1) != 0;
    g_cfg.skipRoadblocks = ReadIniInt("Compatibility", "SkipStaticRoadblocks", 1) != 0;
    int rotation = ReadIniInt("Distribution", "UndersupplyRotationFrames", 600);
    g_cfg.undersupplyRotationFrames = (rotation <= 0) ? 0u : ClampU32(static_cast<u32>(rotation), 60u, 7200u);
    g_cfg.allowChainedCallHook = ReadIniInt("Compatibility", "AllowChainedCallHook", 1) != 0;
}

static void ResolveOptionalKernel32Functions() {
    GetModuleHandleAFn getModuleHandle = ReadPtr<GetModuleHandleAFn>(kIATGetModuleHandleA);
    GetProcAddressFn getProcAddress = ReadPtr<GetProcAddressFn>(kIATGetProcAddress);
    if (!getModuleHandle || !getProcAddress) return;

    HMODULE kernel32 = getModuleHandle("kernel32.dll");
    if (!kernel32) return;

    g_GetPrivateProfileIntA = reinterpret_cast<GetPrivateProfileIntAFn>(getProcAddress(kernel32, "GetPrivateProfileIntA"));
    g_FlushInstructionCache = reinterpret_cast<FlushInstructionCacheFn>(getProcAddress(kernel32, "FlushInstructionCache"));
    g_GetCurrentProcess = reinterpret_cast<GetCurrentProcessFn>(getProcAddress(kernel32, "GetCurrentProcess"));
}

// ---- Vehicle collection -------------------------------------------------------------
static BOOL IsLiveVehicleObject(void* vehicle) {
    if (!vehicle) return 0;
    if (*reinterpret_cast<u32*>(vehicle) != kIVehicleVTable) return 0;

    VehicleBoolFn isActive = VFunc<VehicleBoolFn>(vehicle, kSlotIsActive);
    VehicleBoolFn isDestroyed = VFunc<VehicleBoolFn>(vehicle, kSlotIsDestroyed);
    if (!isActive || !isDestroyed) return 0;
    if (!isActive(vehicle)) return 0;
    if (isDestroyed(vehicle)) return 0;
    return 1;
}

static void CollectVehicles() {
    g_racerCount = 0u;
    g_copCount = 0u;

    u32 count = *reinterpret_cast<u32*>(kVehicleListCount);
    void** data = *reinterpret_cast<void***>(kVehicleListData);
    if (!data) return;
    if (count > 512u) count = 512u; // corrupt-list guard

    for (u32 i = 0u; i < count; ++i) {
        void* vehicle = data[i];
        if (!IsLiveVehicleObject(vehicle)) continue;

        GetDriverClassFn getDriverClass = VFunc<GetDriverClassFn>(vehicle, kSlotGetDriverClass);
        if (!getDriverClass) continue;
        u32 driverClass = getDriverClass(vehicle);

        if ((driverClass == DriverHuman && g_cfg.includePlayer) ||
            (driverClass == DriverRacer && g_cfg.includeAIRacers)) {
            if (g_racerCount >= kMaxRacers) continue;
            VehiclePtrFn getSimable = VFunc<VehiclePtrFn>(vehicle, kSlotGetSimable);
            if (!getSimable) continue;
            void* simable = getSimable(vehicle);
            if (!simable) continue;
            g_racers[g_racerCount].vehicle = vehicle;
            g_racers[g_racerCount].simable = simable;
            ++g_racerCount;
            continue;
        }

        if (driverClass != DriverCop || g_copCount >= kMaxCops) continue;

        VehiclePtrFn getAI = VFunc<VehiclePtrFn>(vehicle, kSlotGetAIVehiclePtr);
        if (!getAI) continue;
        void* ai = getAI(vehicle);
        if (!ai) continue;

        // Exact cop-car IVehicleAI interface expected. Helicopter/other AI is excluded.
        if (*reinterpret_cast<u32*>(ai) != kCopIVehicleAIVTable) continue;

        u8* aiBytes = reinterpret_cast<u8*>(ai);
        void* pursuit = aiBytes + kPursuitSubobjectOffset;
        if (*reinterpret_cast<u32*>(pursuit) != kCopPursuitVTable) continue;
        if (*(aiBytes + kPursuitSubobjectOffset + kPursuitInPursuitOffset) == 0u) continue;

        if (g_cfg.skipRoadblocks) {
            u32 currentGoal = *reinterpret_cast<u32*>(aiBytes + kCurrentGoalOffset);
            if (currentGoal == kGoalStaticRoadBlock) continue;
        }

        g_cops[g_copCount].vehicle = vehicle;
        g_cops[g_copCount].ai = ai;
        g_cops[g_copCount].pursuit = pursuit;
        ++g_copCount;
    }
}

static s32 FindRacerIndex(void* racerVehicle) {
    for (u32 i = 0u; i < g_racerCount; ++i)
        if (g_racers[i].vehicle == racerVehicle) return static_cast<s32>(i);
    return -1;
}

static s32 FindCopIndex(void* copVehicle) {
    for (u32 i = 0u; i < g_copCount; ++i)
        if (g_cops[i].vehicle == copVehicle) return static_cast<s32>(i);
    return -1;
}


static u32 FindMinCountRacer(const u32* counts) {
    u32 best = 0u;
    for (u32 i = 1u; i < g_racerCount; ++i)
        if (counts[i] < counts[best]) best = i;
    return best;
}

static u32 FindMaxCountRacer(const u32* counts) {
    u32 best = 0u;
    for (u32 i = 1u; i < g_racerCount; ++i)
        if (counts[i] > counts[best]) best = i;
    return best;
}

static void RebuildAssignments(BOOL rotateUndersupplied) {
    Assignment old[kMaxCops];
    u32 oldCount = g_assignmentCount;
    for (u32 i = 0u; i < oldCount && i < kMaxCops; ++i) old[i] = g_assignments[i];

    g_assignmentCount = 0u;
    if (g_racerCount < 2u || g_copCount == 0u) return;

    u32 counts[kMaxRacers];
    for (u32 i = 0u; i < kMaxRacers; ++i) counts[i] = 0u;

    // When there are fewer cops than racers, periodically rotate the represented
    // subset so a permanently under-supplied race does not starve the same cars.
    if (g_copCount < g_racerCount && rotateUndersupplied) {
        g_rotationOffset = (g_rotationOffset + 1u) % g_racerCount;
        for (u32 i = 0u; i < g_copCount; ++i) {
            u32 racerIndex = (i + g_rotationOffset) % g_racerCount;
            g_assignments[g_assignmentCount].copVehicle = g_cops[i].vehicle;
            g_assignments[g_assignmentCount].racerVehicle = g_racers[racerIndex].vehicle;
            ++g_assignmentCount;
        }
        return;
    }

    // Preserve valid old assignments first. This avoids visible target thrashing.
    for (u32 ci = 0u; ci < g_copCount; ++ci) {
        void* copVehicle = g_cops[ci].vehicle;
        void* oldRacer = 0;
        for (u32 oi = 0u; oi < oldCount; ++oi) {
            if (old[oi].copVehicle == copVehicle) {
                oldRacer = old[oi].racerVehicle;
                break;
            }
        }
        s32 ri = FindRacerIndex(oldRacer);
        if (ri < 0) continue;

        g_assignments[g_assignmentCount].copVehicle = copVehicle;
        g_assignments[g_assignmentCount].racerVehicle = oldRacer;
        ++g_assignmentCount;
        ++counts[static_cast<u32>(ri)];
    }

    // Add newly spawned/unassigned cops to the currently least-covered racer.
    for (u32 ci = 0u; ci < g_copCount; ++ci) {
        void* copVehicle = g_cops[ci].vehicle;
        BOOL alreadyAssigned = 0;
        for (u32 ai = 0u; ai < g_assignmentCount; ++ai) {
            if (g_assignments[ai].copVehicle == copVehicle) {
                alreadyAssigned = 1;
                break;
            }
        }
        if (alreadyAssigned) continue;

        u32 ri = FindMinCountRacer(counts);
        g_assignments[g_assignmentCount].copVehicle = copVehicle;
        g_assignments[g_assignmentCount].racerVehicle = g_racers[ri].vehicle;
        ++g_assignmentCount;
        ++counts[ri];
    }

    // Equalize existing assignments. A count difference of at most one is fair.
    for (;;) {
        u32 minR = FindMinCountRacer(counts);
        u32 maxR = FindMaxCountRacer(counts);
        if (counts[maxR] <= counts[minR] + 1u) break;

        BOOL moved = 0;
        void* maxVehicle = g_racers[maxR].vehicle;
        for (u32 ai = 0u; ai < g_assignmentCount; ++ai) {
            if (g_assignments[ai].racerVehicle == maxVehicle) {
                g_assignments[ai].racerVehicle = g_racers[minR].vehicle;
                --counts[maxR];
                ++counts[minR];
                moved = 1;
                break;
            }
        }
        if (!moved) break;
    }
}

static void EnforceAssignments() {
    if (g_racerCount < 2u || g_copCount == 0u) return;

    AITargetAcquireFn acquire = reinterpret_cast<AITargetAcquireFn>(kAITargetAcquire);

    for (u32 i = 0u; i < g_assignmentCount; ++i) {
        s32 ci = FindCopIndex(g_assignments[i].copVehicle);
        s32 ri = FindRacerIndex(g_assignments[i].racerVehicle);
        if (ci < 0 || ri < 0) continue;

        CopInfo& cop = g_cops[static_cast<u32>(ci)];
        RacerInfo& racer = g_racers[static_cast<u32>(ri)];
        u8* aiBytes = reinterpret_cast<u8*>(cop.ai);

        // Recheck state because vanilla/Bartender ran immediately before us.
        if (*reinterpret_cast<u32*>(cop.ai) != kCopIVehicleAIVTable) continue;
        if (*reinterpret_cast<u32*>(cop.pursuit) != kCopPursuitVTable) continue;
        if (*(reinterpret_cast<u8*>(cop.pursuit) + kPursuitInPursuitOffset) == 0u) continue;
        if (g_cfg.skipRoadblocks && *reinterpret_cast<u32*>(aiBytes + kCurrentGoalOffset) == kGoalStaticRoadBlock) continue;

        void* target = *reinterpret_cast<void**>(aiBytes + kAITargetPtrOffset);
        if (!target) continue;

        u8* targetBytes = reinterpret_cast<u8*>(target);
        void* currentSimable = *reinterpret_cast<void**>(targetBytes + kAITargetSimableOffset);
        u8 targetValid = *(targetBytes + kAITargetValidOffset);

        if (!targetValid || currentSimable != racer.simable) {
            acquire(target, racer.simable);
        }
    }
}

static void FairCopsTick() {
    if (!g_cfg.enabled) return;

    CollectVehicles();
    if (g_racerCount < 2u) {
        g_assignmentCount = 0u;
        g_rotationCounter = 0u;
        return;
    }

    ++g_frameCounter;
    ++g_rotationCounter;

    BOOL rotate = 0;
    if (g_copCount < g_racerCount && g_cfg.undersupplyRotationFrames != 0u &&
        g_rotationCounter >= g_cfg.undersupplyRotationFrames) {
        g_rotationCounter = 0u;
        rotate = 1;
    }

    BOOL needRebalance = (g_assignmentCount == 0u) || rotate ||
                         ((g_frameCounter % g_cfg.rebalanceFrames) == 0u);

    // If a cop/racer vanished, don't wait for the configured interval.
    if (!needRebalance) {
        for (u32 i = 0u; i < g_assignmentCount; ++i) {
            if (FindCopIndex(g_assignments[i].copVehicle) < 0 ||
                FindRacerIndex(g_assignments[i].racerVehicle) < 0) {
                needRebalance = 1;
                break;
            }
        }
        if (g_assignmentCount != g_copCount) needRebalance = 1;
    }

    if (needRebalance) RebuildAssignments(rotate);
    EnforceAssignments();
}

// ---- Hook --------------------------------------------------------------------------
extern "C" __declspec(noinline) void __fastcall FairCops_UpdatePursuitsHook(void* manager, void*) {
    UpdatePursuitsFn previous = g_previousUpdatePursuits;
    if (previous) previous(manager);
    FairCopsTick();
}

static BOOL WriteHookCall(void* target) {
    VirtualProtectFn virtualProtect = ReadPtr<VirtualProtectFn>(kIATVirtualProtect);
    if (!virtualProtect) return 0;

    u8* site = reinterpret_cast<u8*>(kHookSite);
    DWORD oldProtect = 0u;
    if (!virtualProtect(site, 5u, PAGE_EXECUTE_READWRITE, &oldProtect)) return 0;

    site[0] = 0xE8u;
    s32 relative = static_cast<s32>(reinterpret_cast<u32>(target) - (kHookSite + 5u));
    *reinterpret_cast<s32*>(site + 1) = relative;

    if (g_FlushInstructionCache && g_GetCurrentProcess)
        g_FlushInstructionCache(g_GetCurrentProcess(), site, 5u);

    DWORD ignored = 0u;
    virtualProtect(site, 5u, oldProtect, &ignored);
    return 1;
}

static BOOL InstallHook() {
    if (!IsExpectedGameBuild()) {
        DebugOut("[FairCops] Unsupported speed.exe build; no hook installed.\n");
        return 0;
    }

    u8* site = reinterpret_cast<u8*>(kHookSite);
    for (u32 i = 0u; i < 5u; ++i) g_originalHookBytes[i] = site[i];

    if (site[0] != 0xE8u) {
        DebugOut("[FairCops] Hook site is not a CALL; refusing to overwrite another mod.\n");
        return 0;
    }

    s32 existingRel = *reinterpret_cast<s32*>(site + 1);
    u32 existingTarget = kHookSite + 5u + static_cast<u32>(existingRel);
    if (existingTarget != kVanillaUpdatePursuits && !g_cfg.allowChainedCallHook) {
        DebugOut("[FairCops] Another mod already owns the UpdatePursuits call; chaining disabled.\n");
        return 0;
    }

    if (existingTarget == reinterpret_cast<u32>(&FairCops_UpdatePursuitsHook)) return 1;

    g_previousUpdatePursuits = reinterpret_cast<UpdatePursuitsFn>(existingTarget);
    if (!WriteHookCall(reinterpret_cast<void*>(&FairCops_UpdatePursuitsHook))) {
        g_previousUpdatePursuits = 0;
        DebugOut("[FairCops] VirtualProtect/patch failed; no hook installed.\n");
        return 0;
    }

    DebugOut("[FairCops] Installed. Fair racer targeting is active.\n");
    return 1;
}

static void RemoveHook() {
    if (!g_hookInstalled) return;

    u8* site = reinterpret_cast<u8*>(kHookSite);
    if (site[0] != 0xE8u) return;
    s32 rel = *reinterpret_cast<s32*>(site + 1);
    u32 target = kHookSite + 5u + static_cast<u32>(rel);
    if (target != reinterpret_cast<u32>(&FairCops_UpdatePursuitsHook)) return;

    VirtualProtectFn virtualProtect = ReadPtr<VirtualProtectFn>(kIATVirtualProtect);
    if (!virtualProtect) return;
    DWORD oldProtect = 0u;
    if (!virtualProtect(site, 5u, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    for (u32 i = 0u; i < 5u; ++i) site[i] = g_originalHookBytes[i];
    if (g_FlushInstructionCache && g_GetCurrentProcess)
        g_FlushInstructionCache(g_GetCurrentProcess(), site, 5u);
    DWORD ignored = 0u;
    virtualProtect(site, 5u, oldProtect, &ignored);
}

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = module;
        ResolveOptionalKernel32Functions();
        BuildIniPath();
        LoadConfig();
        g_hookInstalled = InstallHook();
    } else if (reason == DLL_PROCESS_DETACH) {
        RemoveHook();
    }
    return 1;
}
