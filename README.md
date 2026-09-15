# FairCops

**Version:** v0.1-alpha

**Game:** Need for Speed: Most Wanted (2005), PC v1.3

FairCops is an `.asi` script that distributes active pursuing cop cars approximately evenly across the player and AI racers in a race, preventing the police from exclusively swarming the player or a single AI opponent.

Instead of breaking the game's hardcoded pursuit limits, FairCops dynamically retargets individual cops' existing AI target objects to different racers.

## How It Works

If there are 8 eligible pursuing cops and 4 racers, the script automatically balances the pursuit (roughly 2 cops per racer).

* **Stable Assignments:** Targets are kept stable and only rebalance when cops/racers appear, disappear, or at a configured interval.
* **Smart Rotation:** If there are fewer cops than racers, the targeted racers rotate periodically so no one is permanently ignored.
* **Safe Coexistence:** Hooks seamlessly into `AICopManager::UpdatePursuits` and chains with existing mods like Bartender.

## Installation

1. Ensure you are using the supported `speed.exe` (v1.3, 6,029,312 bytes).
2. Copy `FairCops.asi` and `FairCops.ini` into your `scripts` folder (the same folder containing `NFSMWBartender.asi`).
3. Keep Bartender installed normally. Start with the default `FairCops.ini` settings.
4. Test in a Quick Race with cops enabled and several AI opponents.

### Required Mod-Stack Settings

You must keep the following compatibility settings (which are already required by Bartender):

**`NFSMWExtraOptionsSettings.ini`**

```ini
HeatLevelOverride = 0
PursuitActionMode = 0
ZeroBountyFix = 0

```

**`NFSMWUnlimiterSettings.ini`**

```ini
EnableCopDestroyedStringHook = 0

```

## Configuration (`FairCops.ini`)

* `RebalanceFrames` (Default `30`): How often the distribution is recalculated.
* `UndersupplyRotationFrames` (Default `600`): How often to rotate targets when cops are outnumbered by racers (600 frames ≈ 10 seconds). Set to `0` to disable.
* `SkipStaticRoadblocks` (Default `1`): Leaves roadblock placement and control to vanilla/Bartender logic.
* `AllowChainedCallHook` (Default `1`): Allows coexistence with other mods hooking the same `OnTask` call site.

## Known Limitations (v0.1-alpha)

* **No AI Busting:** Formal vanilla pursuit limits still apply. AI racers can be physically chased and rammed, but there are no independent BUSTED timers or DNF removals for them yet.
* **Exclusions:** Helicopters and roadblock geometry are not distributed across racers.
* **No Spawning:** FairCops only redistributes *already-pursuing* cops; it does not spawn or promote patrol cops itself.

## Technical Details & Offsets

FairCops performs strict vtable and hook-site fingerprinting before patching. If it detects an unknown executable, it will refuse to install the hook.

* **Supported Executable:** `speed.exe` (SHA-256: `80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c`)
* **Game live IVehicle list:** Data: `0x0092CD1C` | Count: `0x0092CD24`
* **AICopManager::UpdatePursuits:** `0x0043E8D0`
* **AICopManager::OnTask CALL patched:** `0x0043EF43` (Original bytes: `E8 88 F9 FF FF`)
* **AITarget::Acquire:** `0x00423860`

*Source code (`FairCops.cpp`) is included for auditing patches and offsets.*
