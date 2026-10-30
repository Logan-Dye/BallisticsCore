# Networked Projectile System

Networked Projectile System (NPS) is an Unreal Engine C++ plugin for deterministic projectile, hitscan, and explosive simulation. It supports server-authoritative, client-predicted, and client-authoritative launches through the same data-driven runtime.

## Source structure

- `Source/BallisticsCoreRuntime` contains simulation, launch authority, pooling, damage, material response, explosives, cosmetics, and Blueprint-facing APIs.
- `Source/BallisticsCoreEditor` contains asset validation and trajectory visualization tools.
- `Source/BallisticsCoreTests` contains Unreal Automation coverage for math, networking, lifecycle, cosmetics, performance safeguards, and regressions.
- `Source/NPSExamples` contains the playable first-person range, focused feature demonstrations, inspectable example catalog, C++ guide HUD, and map browser.
- `Config` contains plugin defaults and class redirects required by existing projects.

## Design decisions

- Projectile behavior is composed from data assets for physical properties, material response, spread, explosives, and cosmetics. Game modules do not need NPS-specific weapon definitions.
- A world subsystem owns pooling, listener registration, cosmetic routing, and one aggregate update path. Projectile components cannot enable an independent tick while aggregate simulation owns them.
- Registration changes are deferred while a projectile batch is advancing, so impact and detonation callbacks can retire or create projectiles without mutating the active array during iteration.
- Fresh and pooled projectile actors receive their owner and instigator before spawn or activation callbacks run.
- Every authority mode uses the same damage context and authoritative impact and fly-by events. Cosmetic configuration cannot suppress gameplay events.
- Server-authoritative and client-predicted RPCs carry only launch geometry and a deterministic seed. The server rebuilds physical conditions from its projectile data asset.
- Client-authoritative flight logs are matched to a server launch snapshot and checked for canonical physical values, monotonic time, trajectory position, speed, travel distance, visibility, material response, and target proximity.
- `IBallisticsLaunchValidator` lets a game enforce weapon ownership, ammunition, cadence, ability state, and origin rules without adding firearm-specific policy to NPS.
- Hitscan and explosive fragments use deterministic trace paths instead of spawning short-lived projectile actors.
- Launch cosmetics are separate from projectile physics. Named effects can resolve different components and sockets, while attachment contributors apply source and cosmetic overrides in deterministic attachment-depth order.
- Missing cosmetic bindings and sockets fail closed. Soft asset references prevent dedicated servers from loading client-only audio, Niagara, and decal assets.
- Delayed audio, fuse advancement, and effect lifetimes use bounded aggregate queues rather than per-projectile or per-effect timers.

## Review path

Start with these files:

1. `Source/BallisticsCoreRuntime/Public/ProjectileLauncherComponent.h` defines the launch API, authority modes, validation entry points, and gameplay events.
2. `Source/BallisticsCoreRuntime/Private/ProjectileLauncherComponent.cpp` implements launch routing, prediction, flight-log validation, cosmetics, and contributor resolution.
3. `Source/BallisticsCoreRuntime/Public/ProjectileBallisticsComponent.h` and its implementation contain the projectile state machine and deterministic flight integration.
4. `Source/BallisticsCoreRuntime/Private/ProjectileBallisticsSubsystem.cpp` contains aggregate scheduling, mutation-safe registration, pooling, and automatic fly-by listeners.
5. `Source/BallisticsCoreRuntime/Private/BallisticsHitscanLibrary.cpp` contains hitscan, penetration, ricochet, and fragment trace handling.
6. `Source/BallisticsCoreTests/Private` contains the automation specifications that exercise these contracts.
7. `Source/NPSExamples/Public/NPSExampleCatalog.h` defines the map and station metadata used by the example asset, HUD, world boards, documentation dataset, and content tests.

## Building and testing

Place the `NPS` directory under an Unreal project's `Plugins` directory and build the project's Editor target. The `BallisticsCoreTests` developer module registers the automation suite. In the development checkout, `NPSProject/RunNPSTests.bat` builds the test project and runs the complete `NPS.*` suite headlessly.

The verified development target is Unreal Engine 5.8 on Win64.

## Iris and legacy replication

NPS uses Unreal's actor and actor-component fragment registration for its replicated
properties and RPCs. The runtime module calls `SetupIrisSupport(Target)` and its four
replicated components and cosmetic relay select registered subobject lists under
`UE_WITH_IRIS`, the engine-provided macro. Existing lifetime-property registrations,
RepNotify callbacks, quantized engine types, and RPC validation remain in use.
Cosmetic batches use a compact bitstream with matching legacy and native Iris
serializers. Server and client must run the same NPS wire protocol version.

Each owner relay registers static profiles reliably. First-use records follow the
definition reliably until its acknowledgement; established profiles continue through
prioritized unreliable multicast without waiting for new definitions. Begin/End
commands remain reliable and ordered on the owner relay. Transform updates remain
unreliable and cannot create a sequence before Begin or restart one after End.

Static templates are prepared when the launcher's resolved contributor cache rebuilds,
including separate visual/audio keys. Auxiliary ammo/surface/flyby/detonation caches
validate mutable scalar and soft-reference fields before reuse. Relay lookup hashes
only the prepared key, with no per-listener static serialization or deep comparison.
Generic rich-event callers can prepare and retain an immutable template with
`BallisticsCosmeticWire::Prepare`; clear it after changing static fields. Routing
prepares a missing template once per channel before listener fan-out.

Profiles contain assets and parameters, shared across equivalent weapon instances.
Each relay retains up to 1024 immutable shared templates for its lifetime. IDs never
reuse, and established content continues if new content exceeds the hard cap; new
content is rejected with a diagnostic. The pending send queue holds 1024 records.
Auxiliary caches retain 64 entries. Relay destruction releases the catalog.

NPS launchers allocate non-reused 64-bit audio tokens in a process namespace. Profile
definitions carry that namespace once per content registration. Dynamic records carry
a packed token (usually 1-3 bytes), preserving weapon concurrency without requiring a
mapped actor or a per-weapon relay entry. Multiple launchers on an actor remain distinct.
Generic rich-event callers can use `BallisticsCosmeticWire::NewAudioSourceID` too.
Arbitrary external GUIDs use a separate lifetime compatibility table capped at 1024
identities; new identities beyond that bound are rejected diagnostically. Definitions
register reliably without audio acknowledgements. Received records that overtake their
compatibility definition wait in a bounded 1024-record buffer until it arrives.

Packed engine actor references resolve only through the receiving world's package map
or Iris bridge. Fallback poses remain available for irrelevant or destroyed sources.
Iris pacing counts native state retained by unsent RPC blobs, including shrink-wrap
ownership, through public serializer clone/free callbacks. Local queue release returns
capacity without waiting for network delivery. Legacy pacing uses the public actor
channel update timestamp. Neither path forces connection flushing or changes engine
queue limits; prioritized disposable RPC delivery remains in place.

Unreliable batch parameters are measured using the wire serializer and capped at
1536 bits (192 bytes), reserving a conservative 64 bytes for the RPC envelope under
the 256-byte attachment threshold. This measures parameters, not the complete
engine RPC envelope; zero multicast-splitting log entries in actual network tests
provide the complementary engine check. Records exceeding the parameter cap use
reliable delivery. NPS does not raise engine attachment thresholds.
Ordinary native Iris state is under256bytes. A rare oversized record uses a separate
reliable byte-array RPC, bounded to512bytes for the existing16-effect pose limit.
Records have explicit bit lengths: unknown profiles or invalid records are skipped,
while malformed framing aborts only its own batch.
On the legacy driver, a relay-owned call count resets when the public actor-channel
replication timestamp advances. Additional batches remain pending at the engine's
per-update multicast call limit and drain through prioritized actor replication.
NPS does not inspect actor-replicator RPC internals or change the limit.
Iris limits unsent native RPC ownership to its existing per-object queue capacity.
Locally drained batches release capacity without a delivery ACK or RTT window.

The consuming project selects the replication backend. NPS does not change its
configuration or activate Iris globally. In UE 5.8, `SetupIrisSupport` always adds
IrisCore and defines `UE_WITH_IRIS=1`; the backend is selected at runtime. On older
engines the helper determines the available Iris dependencies and macro value.
Compatibility with those engine versions requires separate compilation and testing.

For an Iris project, enable Unreal's `Iris` plugin in the consuming project (or use
`-EnablePlugins=Iris` for a validation process). The build dependency alone does not
load IrisCore. Enable the registered subobject list before net drivers and actors
are created, for example in the consuming project's `DefaultEngine.ini`:

```ini
[SystemSettings]
net.SubObjects.DefaultUseSubObjectReplicationList=1
```

Owning actors supplied by the game must also use registered subobject lists. Use
`AddReplicatedSubObject` for game-owned replicated UObject subobjects; an override of
`ReplicateSubobjects` is not an Iris replication path. NPS asset references identify
assets; they do not replicate runtime mutations to the contents of those assets.

After manually building the external plugin through the test project's Editor
target, move the four NPS module DLLs into the plugin binary directory. Keeping
copies in both binary directories can register the same classes twice when the
internal tools module loads. `RunNPSTests.bat` performs this staging automatically.
For a manual build, stage once from PowerShell before launching the editor:

```powershell
foreach ($module in 'BallisticsCoreRuntime', 'BallisticsCoreEditor', 'BallisticsCoreTests', 'NPSExamples') {
    Move-Item -LiteralPath "C:\BallisticsPlugin\NPSProject\Binaries\Win64\UnrealEditor-$module.dll" -Destination 'C:\BallisticsPlugin\NPS\Binaries\Win64\' -Force
}
```

Run two separate editor processes from PowerShell, waiting for each to exit:

```powershell
& 'C:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\BallisticsPlugin\NPSProject\NPSTestLab.uproject' -unattended -nosplash -UseIrisReplication=0 -NPSExpectedIris=0 '-ExecCmds=Automation RunTests NPS.' '-TestExit=Automation Test Queue Empty' -log
& 'C:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\BallisticsPlugin\NPSProject\NPSTestLab.uproject' -unattended -nosplash -EnablePlugins=Iris -UseIrisReplication=1 -NPSExpectedIris=1 '-ini:Engine:[SystemSettings]:net.SubObjects.DefaultUseSubObjectReplicationList=1' '-ExecCmds=Automation RunTests NPS.' '-TestExit=Automation Test Queue Empty' -log
```

The authority, cosmetic, and playground network suites report `NPS backend: Legacy`
or `NPS backend: Iris` for the server and each client. `NPSExpectedIris` makes a
backend mismatch fail the test instead of accepting silent fallback. Compare the
automation results for both runs, including client-authoritative moving-target hit
validation, pooled projectiles, proxy updates, reliable sequence transitions,
owner-only cosmetic batches, and acoustic timing. These instructions describe
validation to perform; adding support and tests does not establish runtime success.
Headless `-NullRHI -nosound` tests verify transport and playback dispatch, not rendered
effects or audible output. After rebuilding OP4 with matching source, manually run
two-client PIE combat with the M4 and AK, check launch/impact effects, sustained fire
and audio, and inspect the log for multicast-splitting warnings.

## Source review archive

The portfolio archive contains this README, the plugin descriptor, configuration, source, and icon. Packaged binaries, intermediate build products, documentation exports, and binary content assets are excluded.
