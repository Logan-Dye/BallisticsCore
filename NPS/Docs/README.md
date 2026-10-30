# Networked Projectile System (NPS)

A deterministic C++ projectile system for Unreal Engine with configurable
multiplayer networking. It supports single and multi-projectile launchers,
explosives, and hitscan through a shared material and authority pipeline.

## Why NPS?

NPS exposes the networking policy directly. Each projectile asset can use the
global authority mode or provide its own override.

Three configurable authority modes expose the networking policy for each game:

- **Server Authoritative** - The server owns projectile simulation and damage.
- **Client Predicted** - Balanced. Client launches locally for instant feedback, server validates via resimulation.
- **Client Authoritative** - Lowest latency. Client owns the launch, server validates the path after the fact.

Zero-actor hitscan and shrapnel traces mean explosions with 200 fragments cost the same as 200 line traces in a single frame - no actors, no replication, no tick cost.

## Quick Start

1. Drop the `NPS` folder into your project's `Plugins/` directory.
2. Compile.
3. Open Project Settings -> Game -> Networked Projectile System to configure global settings.
4. Add a `UProjectileLauncherComponent` to the actor that launches the projectile.
5. Assign a projectile data asset that defines the projectile class and launching behavior.
6. Call `Launch(Origin, Direction)` - the launcher handles everything else.

See the [Quick Start Guide](QuickStart.md) for a detailed walkthrough.

## What's Included

### Core Systems
- Deterministic trajectory simulation (piecewise constant-acceleration, 5ms sub-steps)
- Ricochet, penetration, embedding, sliding state machine
- Per-surface material response system (data asset driven)
- Seeded randomness for multiplayer determinism

### Networking
- Three launch authority modes (global or per-projectile override)
- Server-auth cosmetic modes (none, immediate, fast-forwarded by half-RTT)
- Compact transform replication for long-lived server-authoritative projectiles
- Client prediction with server reconciliation
- Client-authoritative flight log validation (visibility, ricochet angles, material penetrability)
- FProjectileFlightLog ring buffer system (like saved moves)

### Launcher Systems
- Launcher component with automatic authority routing
- Hitscan mode (instant trace through full material pipeline)
- Multi-projectile spread (random cone, fixed pattern, hybrid)
- Projectile data assets (projectile, authority, hitscan, ballistics, materials, spread, explosives)

### Audio and Visual Effects

- Launcher-owned launch profiles for base sound, distant report, and an
  ordered Niagara effect array
- Stackable attachment contributors with depth-ordered sources and modifiers
- Inline attachment source overrides with depth-based selection
- Named Add, Replace, and Remove edits for individual launch effects
- Contributor-aware source overrides for attached launch devices
- Per-effect named source components, sockets, offsets, scale, and hard lifetime
- Projectile-owned fly-by, tracer, and per-surface impact cosmetics
- Explosive-data-owned detonation cosmetics independent of launcher lifetime
- Batched listener routing beyond actor relevancy, with sustained begin/end reconstruction
- Server-enforced speed-of-sound delivery with immediate visual-only packets
- Aggregate-managed Niagara lifetimes stop looping effects without per-effect timers
- Soft cosmetic references keep dedicated servers from loading audio, Niagara, or decals

### Explosives
- Explosive data asset (blast radius, shrapnel, damage falloff)
- Detonation component (4 triggers: OnImpact, FuseTimer, OnRest, Manual)
- Radial shrapnel traces (zero actors, one frame, full material pipeline)
- Blast damage with line-of-sight checks and inner/outer radius

### Performance
- Object pooling (acquire/release, pre-warming)
- Aggregate tick system (subclassable for LOD/priority strategies)
- VFX impact budget (per-frame/per-projectile caps, distance culling)
- Runtime bounds for client cosmetic claims, routing radius, per-frame playback,
  active sequences, aggregate-queued delayed audio, contributor stacks, active Niagara components,
  effect lifetimes, and effect values
- Individual component Tick is automatically disabled and warned when aggregate
  simulation owns the projectile
- Everything compiles to C++ - no Blueprint overhead in the runtime

### Debug Tools
- `NPS.Debug.Projectiles` - live paths with impact markers
- `NPS.Debug.Hitscan` - trace visualization with energy labels
- `NPS.Debug.Detonation` - blast radii, affected actors, and damage
- `NPS.Debug.Explosions` - alias for detonation drawing
- `NPS.Debug.Budget` - on-screen impact budget HUD
- `NPS.Debug.All` - toggle everything

### Testing
- Automated tests covering trajectory math, flight logs, runtime features,
  all multiplayer authority modes, cosmetic delivery beyond actor relevancy,
  the playable first-person range, example content integrity, performance
  safeguards, and stress workloads up to 1M evaluations

## Documentation

- [Offline PDF Manual](NPS_Documentation.pdf)
- [Quick Start Guide](QuickStart.md)
- [Modular Launch Sources](ModularLaunchSources.md)
- [Authority Modes](AuthorityModes.md)
- [API Reference](APIReference.md)
- [Settings Reference](SettingsReference.md)
- [Example Suite](Examples.md)
- [Example Catalog Dataset](NPS_ExampleCatalog.json)

## Requirements

- Unreal Engine 5.8 is the verified release target
- Win64 is the verified platform
- A supported C++ compiler toolchain is required to build from source

## Support

For questions, bugs, or feature requests, contact Persidera Industries LLC.
