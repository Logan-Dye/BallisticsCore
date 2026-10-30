# Quick Start Guide

This guide covers the minimum setup required to launch a projectile.

## Installation

1. Copy the `NPS` folder into your project's `Plugins/` directory.
2. Regenerate project files (right-click your .uproject -> Generate Visual Studio Project Files).
3. Compile.
4. Verify: Open Project Settings -> Game -> you should see "Networked Projectile System."

## Your First Projectile Actor

Create a Blueprint or C++ actor with:
- A `UProjectileBallisticsComponent` (drives the physics)
- A mesh or particle for visuals (optional)
- A `UProjectileTracerComponent` (optional, adds the tracer trail)
- A `UProjectileNetProxyComponent` for long-lived server-authoritative projectiles such as grenades

The ballistics component handles all simulation movement. Do not tick or move
the actor yourself. On replicated client copies that include
`UProjectileNetProxyComponent`, NPS automatically disables local Ballistics
simulation so the compact proxy is the only movement owner.

### Minimal C++ Example

```cpp
// MyProjectile.h
UCLASS()
class AMyProjectile : public AActor
{
    GENERATED_BODY()
public:
    AMyProjectile();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileBallisticsComponent> BallisticsComponent;
};

// MyProjectile.cpp
AMyProjectile::AMyProjectile()
{
    BallisticsComponent = CreateDefaultSubobject<UProjectileBallisticsComponent>(TEXT("Ballistics"));
}
```

The component registers with the subsystem, advances the trajectory, sweeps for
collisions, and resolves configured impact responses.

## Your First Launcher

Add a `UProjectileLauncherComponent` to any actor that launches projectiles:

```cpp
// MyLauncher.h
UPROPERTY(VisibleAnywhere)
TObjectPtr<UProjectileLauncherComponent> LauncherComponent;

// MyLauncher.cpp
AMyLauncher::AMyLauncher()
{
    LauncherComponent = CreateDefaultSubobject<UProjectileLauncherComponent>(TEXT("Launcher"));
}
```

In the editor, assign the projectile data asset described below to
`ProjectileDataAsset` on the launcher.

Then when the player launches:

```cpp
void AMyLauncher::Launch()
{
    FVector LaunchOrigin = GetLaunchOrigin();
    FVector LaunchDirection = GetLaunchDirection();
    LauncherComponent->Launch(LaunchOrigin, LaunchDirection);
}
```

The launcher automatically routes through the correct authority mode, spawns the projectile or runs a hitscan trace, and handles all networking.

## Using Projectile Data Assets

Create a `UBallisticsProjectileDataAsset` in the editor:
1. Content Browser -> Right Click -> Data Asset -> BallisticsProjectileDataAsset
2. Configure the projectile class, authority override, hitscan mode, ballistics
   (including optional deterministic wind acceleration), materials, spread,
   explosive reference, tracer, fly-by, and impact behavior.
3. Assign it to your launcher's `ProjectileDataAsset` property.

Swap projectile at runtime:

```cpp
LauncherComponent->SetProjectileDataAsset(NewProjectileDataAsset);
```

Runtime projectile changes are server authoritative and replicate with the
launcher component. Clients cannot replace the active projectile asset.

## Launch, Fly-By, Impact, and Detonation Effects

Create a `UBallisticsLaunchCosmeticProfile` and assign it to the launcher's
`LaunchCosmeticProfile`. The profile describes the source-specific launch
sound, distant report, Niagara effect array, listener radius, and optional
distance-based audio delay. Each effect selects a socket and has its own
location offset, rotation offset, scale, and lifetime. Assign the usual socket
owner to `LaunchEffectSourceComponent`. For effects that need another component,
add an entry to `LaunchEffectSourceBindings` and select its name in the effect's
`SourceBinding`. A socket name of None uses the origin and direction passed to
`Launch`.

For example, two entries can use `Muzzle` for flash and smoke while another
uses `ShellEject` for an ejection effect. NPS sends the resolved transforms to
remote listeners, so playback does not depend on the launcher actor being
network relevant.

Give every effect a stable `EffectId`, such as `MuzzleFlash`, `Smoke`, or
`Casing`. IDs let attached contributors edit one role without replacing the
whole array.

Each effect also has typed Niagara parameter arrays. NPS supports bool, int,
float, Vector2, Vector3, linear color, static mesh, material, and texture
overrides. Enter the exact exposed Niagara user parameter name, including the
`User.` prefix. The Niagara system must define the matching user parameter with
the same type. For one reusable casing system, configure its `Casing` effect
with entries such as:

- Static Mesh: `User.Casing`
- Vector3: `User.VelocityDirection`
- Float: `User.VelocitySpeed`

These values are resolved and sent with that effect, then applied before the
Niagara component activates. Up to 16 valid typed parameters are retained per
effect. Use profile-level bool and float parameters only when a value should be
shared by the launch sounds or multiple launch effects.

Profiles also contain named bool and float parameters. Each parameter can target
audio, Niagara, or both. NPS applies audio values to every spawned launch sound
and Niagara values before activating each launch effect. Delayed audio keeps the
same resolved values. A modifier replaces matching parameters by name and can
add new names. This supports values such as `Suppressor`, `FireRate`, and
`UseFlash` without creating another playback manager.

For an attachment or other stackable source, add a
`UBallisticsLaunchContributorComponent` to the attached actor. NPS
automatically binds it to the nearest launcher in the attachment ancestry and
removes it after detachment. The component can provide inline source overrides,
an optional `UBallisticsLaunchCosmeticModifier`, or both. A position-only
attachment does not need a modifier data asset.

In multiplayer, attach and detach the contributor actor on the server. The
actor must replicate if clients need the modifier or its source component.

A modifier can:

- override either sound layer;
- add, replace, or remove an effect by `EffectId`;
- replace an effect together with its typed Niagara parameters;
- multiply scale, volume, pitch, audible radius, and visual radius;
- replace named bool and float parameters;
- retain legacy source overrides for existing projects.

Modifier stacks default to attachment depth. Deeper contributors apply later
and win conflicting sound, effect, and named parameter overrides. Enable a
modifier's `Override Priority` only when depth is not the intended order.
Existing saved modifier assets retain their previous priority behavior until
that override is disabled.

For a source override, tag a scene component on the contributor actor, then add
an inline `SourceOverrides` entry with the profile binding and component tag.
The deepest attached contributor that declares the binding wins. Enable the
entry's priority override only when depth is not the desired order. An explicit
invalid override fails closed for that effect instead of falling back to the
unmodified source.

`BindingName` and `SocketNameOverride` do not need to match. If the selected
component does not contain the requested socket, the effect is skipped.

If gameplay launch origin should follow the same contributor-aware source, call
`ResolveLaunchSourceTransform(SourceBinding, SocketName, Transform)` and pass
the returned location and forward direction to `Launch`.

See [Modular Launch Sources](ModularLaunchSources.md) for the receiver, barrel,
launch-device, and suppressor setup.

Projectile data owns fly-by and per-surface impact cosmetics. Unspecified impacts inherit **Project Settings -> Game -> Networked Projectile System -> Defaults -> Default Impact Cosmetics By Surface**, then **Default Impact Cosmetic** as the catch-all. Projectile surface entries and the projectile fallback override those project defaults.
Explosive data owns detonation cosmetics. Authoritative and standalone
detonation routing is owned by the persistent cosmetic manager, so a fuse
effect does not depend on its original launcher actor still existing.

NPS routes these events separately from projectile actor relevancy. For a
sustained launcher, call `BeginLaunchSequence` when it starts and
`EndLaunchSequence` when it stops; continue calling `Launch` for gameplay launches.
The existing ballistics subsystem owns the cosmetic manager automatically.

The initiating player gets immediate local playback. The server resolves the
authoritative cosmetic stack for other listeners, sends visual-only packets
immediately, and holds audio-only packets in one bounded aggregate queue until
the sound reaches each listener. Delays beyond `MaxCosmeticAudioDelay` are
dropped rather than played early. No per-effect timers are created.

For client-authoritative fly-bys, the client reports only the closest point,
direction, miss distance, and speed. The server selects the nearest registered
listener from its own world state and calculates side and actual distance.

Dedicated servers route soft data-asset references and do not load the
referenced sound, Niagara, or decal assets.

## Enabling Hitscan

On the projectile data asset, check `bUseHitscan = true`. Configure `HitscanConfig`
for range, ricochets, and penetration. The launcher then uses
instant traces instead of spawning projectile actors.

## Adding Explosives

1. Create a `UBallisticsExplosiveDataAsset`: blast radius, shrapnel count, damage, trigger type.
2. Add a `UBallisticsDetonationComponent` to your projectile actor.
3. Assign the explosive asset to the projectile asset's `ExplosiveConfig`. The launcher configures the detonation component automatically for every spawned or pooled projectile.
4. For a replicated grenade, add `UProjectileNetProxyComponent`, disable actor replicated movement, and enable `bRestOnSurface` on the ballistics component.
5. Set the trigger (OnImpact for RPGs, FuseTimer for grenades, etc.).

For a placed charge that is not launched from projectile, assign the explosive
directly or call `ConfigureExplosive`.

The detonation component auto-hooks into the sibling ballistics component.
When triggered, it performs blast damage and shrapnel traces in a single frame
and broadcasts the `OnDetonation` delegate.

## Handling Damage

Implement `IBallisticsDamageInterface` on any actor that can take damage:

```cpp
UCLASS()
class AMyCharacter : public ACharacter, public IBallisticsDamageInterface
{
    // ...
    virtual void OnBallisticDamage_Implementation(const FBallisticDamageContext& Context) override
    {
        float Damage = Context.KineticEnergy * DamageMultiplier;
        // Apply project-specific damage using Context.ProjectileDataAsset when needed.
    }
};
```

For explosives, bind to the detonation component's `OnDetonation` delegate and
apply damage from the `FDetonationResult`. Both damage result types carry the
source projectile data asset directly.

## Spread Setup

1. Create a `UBallisticsSpreadDataAsset`: projectile count, spread mode, cone angle, pattern points.
2. Create a `UBallisticsProjectileDataAsset` and assign the spread config to its `SpreadConfig` field.
3. Assign the projectile to your launcher.

When you call `Launch()`, the launcher automatically launches multiple projectiles. Works with both projectile mode and hitscan mode.

## Choosing an Authority Mode

Open Project Settings -> Game -> Networked Projectile System -> **Launch Authority Mode**.

| Mode | Latency | Cheat Resistance | Best For |
|------|---------|-------------------|----------|
| Server Authoritative | High (full RTT) | Maximum | Competitive, anti-cheat critical |
| Client Predicted | Low (instant local) | High | Most multiplayer games |
| Client Authoritative | Lowest | Moderate | Co-op, large player counts, responsiveness priority |

Each projectile configuration can override the global mode.

See [Authority Modes](AuthorityModes.md) for the full breakdown of how each mode works internally.

## Debug Visualization

Open the console (~) and type:
- `NPS.Debug.Projectiles 1` - see live projectile paths
- `NPS.Debug.Hitscan 1` - see hitscan traces
- `NPS.Debug.Detonation 1` - see blast radii and damage
- `NPS.Debug.Projectiles 1` - also shows fragment flight paths
- `NPS.Debug.All` - toggle everything

## Next Steps

- [Authority Modes](AuthorityModes.md) - deep dive into networking
- [API Reference](APIReference.md) - every class and function
- [Settings Reference](SettingsReference.md) - all configurable options
