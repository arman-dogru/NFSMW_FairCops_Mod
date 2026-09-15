FairCops v0.1-alpha
Need for Speed: Most Wanted (2005), PC v1.3
============================================================

WHAT THIS BUILD DOES
--------------------
FairCops distributes active pursuing cop cars approximately evenly across the
player and the AI racers in a race.

It deliberately DOES NOT remove NFSMW's "one formal non-player pursuit" limit
and DOES NOT create additional AIPursuit objects. Instead, after the game's
normal pursuit manager (and Bartender's changes) has run, FairCops retargets
individual cops' existing AITarget objects to different racers.

With 8 eligible pursuing cops and 4 racers, the intended assignment is roughly:

    Player        <- 2 cops
    AI racer 1    <- 2 cops
    AI racer 2    <- 2 cops
    AI racer 3    <- 2 cops

Assignments are kept stable and only rebalanced as cops/racers appear or
disappear, or at the configured interval. If there are fewer cops than racers,
the covered racers rotate periodically so the same racers are not permanently
ignored.

INSTALLATION
------------
1. Copy FairCops.asi and FairCops.ini into the same scripts folder that contains
   NFSMWBartender.asi.
2. Keep Bartender installed normally.
3. Start with the supplied FairCops.ini unchanged.
4. Test in a Quick Race with cops enabled and several AI opponents.

Your supplied speed.exe was verified as the supported 6,029,312-byte v1.3
executable (SHA-256 below). FairCops also fingerprints several exact vtables and
the intended hook site before patching. If they do not match, it refuses to
install its hook rather than writing into an unknown build.

BARTENDER / CURRENT MOD-STACK SETTINGS
--------------------------------------
Keep the Bartender compatibility settings already required by Bartender itself:

NFSMWExtraOptionsSettings.ini:
    HeatLevelOverride = 0
    PursuitActionMode = 0
    ZeroBountyFix = 0

NFSMWUnlimiterSettings.ini:
    EnableCopDestroyedStringHook = 0

These are Bartender's own documented compatibility requirements; FairCops does
not replace them.

HOW THE HOOK COEXISTS WITH BARTENDER
------------------------------------
The v1.3 game runs this AICopManager pipeline every frame:

    UpdatePatrols
    UpdatePursuits
    UpdateRoadBlocks
    UpdateSpawnRequests

FairCops replaces only the five-byte CALL instruction to UpdatePursuits inside
AICopManager::OnTask (0x0043EF43). Its wrapper first calls the original/current
CALL target, then performs FairCops targeting. This means Bartender remains in
control of pursuit settings, spawning, heat, support, and its other patches.

If another mod has already changed that site to another E8 CALL, FairCops can
chain it when AllowChainedCallHook=1. If the site is no longer a CALL at all,
FairCops refuses to overwrite it.

FairCops changes only the AITarget of eligible cop cars that are already marked
as being in pursuit. It does not allocate an extra pursuit object.

CONFIGURATION
-------------
[Main]
Enabled=1
    1 = run FairCops targeting; 0 = leave the hook loaded but do no targeting.

[Distribution]
RebalanceFrames=30
    How often the assignment distribution is recalculated. FairCops still
    enforces the chosen target after every normal pursuit-manager update.

IncludePlayer=1
IncludeAIRacers=1
    Normally both should remain 1 for fair race pursuits.

UndersupplyRotationFrames=600
    When there are fewer eligible pursuing cops than racers, rotate the subset
    of racers receiving cops. 600 is about 10 seconds at 60 FPS. Set 0 to
    disable this rotation.

[Compatibility]
SkipStaticRoadblocks=1
    Recommended. Cops currently holding the StaticRoadBlock goal are not
    retargeted, leaving roadblock placement/control to NFSMW/Bartender.

AllowChainedCallHook=1
    Chains a pre-existing E8 CALL hook at the same OnTask call site. A non-CALL
    patch is never overwritten.

WHAT COUNTS AS AN ELIGIBLE COP
------------------------------
FairCops currently delegates normal cop-car AI only when all of these are true:
- the vehicle is live, active, and not destroyed;
- DriverClass is Cop;
- the AI object is an AIVehicleCopCar;
- its pursuit subobject reports that it is currently in pursuit;
- it is not currently a StaticRoadBlock holder when SkipStaticRoadblocks=1.

The helicopter is intentionally excluded. Rhino/cop SUVs use the normal cop-car
AI class and may therefore be delegated if they are actively pursuing, but
FairCops does not create extra Rhino strategy requests.

KNOWN LIMITATIONS OF v0.1-alpha
-------------------------------
1. Formal pursuit state is still vanilla/Bartender state. NFSMW still has only
   one formal non-player pursuit at a time.
2. Extra AI racers can be physically chased/rammed/PITed by delegated cops, but
   this build does not add independent vanilla BUSTED timers or DNF/removal for
   every AI racer. That requires a separate per-racer bust layer.
3. Roadblock geometry is not delegated across racers in this build.
4. The helicopter is not delegated across racers.
5. Only already-pursuing cop cars are redistributed. FairCops does not itself
   spawn or promote patrol cops.
6. This build was statically validated against the exact binaries you supplied,
   but the game itself cannot be executed in the build sandbox. Treat this as a
   first testable alpha, not as an in-game-certified release.

RECOMMENDED FIRST TEST
----------------------
Use a 4-car Quick Race with race cops enabled. Prefer enough active cops to have
at least one per racer (for example 6-8). Watch whether cops separate and stay
with different opponents rather than all converging on the player plus one NPC.

For the first test, leave:
    SkipStaticRoadblocks=1
    RebalanceFrames=30
    UndersupplyRotationFrames=600

If the game crashes at startup, remove FairCops.asi. If it starts but cop
behavior is wrong, set Enabled=0 to confirm whether the targeting layer is the
cause, then report exactly what happened.

BUILD / REVERSE-ENGINEERED ANCHORS
----------------------------------
Game live IVehicle list:
    data  = 0x0092CD1C
    count = 0x0092CD24

AICopManager::UpdatePursuits:
    0x0043E8D0

AICopManager::OnTask CALL patched by FairCops:
    0x0043EF43
    supplied speed.exe original bytes: E8 88 F9 FF FF

AITarget::Acquire:
    0x00423860

The code is included as FairCops.cpp so every patch and offset used by this
build is auditable.

SUPPLIED speed.exe
------------------
Size:    6,029,312 bytes
SHA-256: 80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c
