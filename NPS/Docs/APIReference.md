# API Reference

## Components

### UProjectileBallisticsComponent

The core projectile physics component. Attach to any projectile actor.

**Key Functions:**
- `InitializeProjectile(Conditions)` - Start the projectile with given initial conditions. Call once after spawn.
- `GetCurrentSnapshot()` - Get position, velocity, speed, elapsed time, and state without advancing time.
- `EvaluateAtTime(Time)` - Pure math evaluation at a given elapsed time. No side effects.
- `ForceCorrection(Position, Velocity, ElapsedTime)` - Server reconciliation: snap to authoritative state.
- `EnforceSimulationTickOwnership()` - Disable conflicting individual Tick when aggregate simulation owns the projectile.
- `IsInFlight()` - True if InFlight or Ricocheted (still moving).
- `GetCurrentState()` - Returns the current EProjectileState.

**Key Properties:**
- `TraceChannel` - Collision channel for sweeps (default: Visibility).
- `MaxRicochets` - Max bounces before destruction (default: 3).
- `MaxPenetrationDepth` - Max penetration per surface in cm (default: 30).
- `MinFlightSpeed` - Speed below which the projectile stops (default: 500 cm/s).
- `bEnableSliding` - Allow sliding along surfaces after low-energy impacts.
- `bRestOnSurface` - Offset a resting body by its radius; useful for grenades.
- `MaterialDataAsset` - Per-surface ballistic response data asset.
- `DefaultMaterialResponse` - Fallback when no data asset or mapping is found.

**Delegates:**
- `OnImpact` - Broadcasts on every collision with hit result, snapshot, and response type.
- `OnStateChanged` - Broadcasts on state transitions (InFlight -> Ricocheted, etc.).
- `OnProjectileDestroyed` - Broadcasts when the projectile is destroyed or embedded.

### UProjectileLauncherComponent

Projectile-source component. Handles spawning, authority routing, networking,
and launch/impact/fly-by/detonation cosmetic routing.

**Key Functions:**
- `Launch(Origin, Direction)` - Launch a projectile or hitscan trace. Routes through the active authority mode automatically.
- `SetProjectileDataAsset(NewProjectileData)` - Server/standalone-only runtime
  change. The selected asset replicates with the launcher component.
- `GetActiveAuthorityMode()` - Returns the mode this launcher is currently using.
- `GrantLaunchCredits(Count, ExpirySeconds)` - Server-only authorization for
  further remote launches. A non-positive expiry uses the project default.
- `GetAvailableLaunchCredits()` - Return the current unexpired server credit count.
- `ClearLaunchCredits()` - Cancel every outstanding server launch credit.
- `SubmitFlightLog(FlightLog)` - Manually submit a flight log (Client Authoritative mode).
- `SetLaunchCosmeticProfile(Profile)` - Server/standalone-only base launch signature change.
- `SetLaunchEffectSourceComponent(Component)` - Set the component that owns the sockets referenced by launch effects.
- `SetLaunchEffectSourceBinding(Name, Component)` - Add, replace, or clear a named effect source component.
- `GetLaunchEffectSourceComponent(Name)` - Resolve the default or exact named effect source.
- `ResolveLaunchSourceTransform(Binding, Socket, Transform)` - Resolve a contributor-aware source for gameplay launch origin or other systems.
- `BeginLaunchSequence(Origin, Direction, Rate)` / `EndLaunchSequence()` - Reconstruct sustained launch cosmetics remotely.

**Key Properties:**
- `ProjectileDataAsset` - Active projectile.
- `MinLaunchInterval` - Seconds per cadence token. Zero disables cadence. The
  bucket holds three launch tokens.
- `bRequireLaunchCredits` - Require one server credit per accepted remote
  top-level launch. Disabled by default.
- `LaunchCosmeticProfile` - Launcher-specific sound, distant report, ordered Niagara effect array, named bool/float parameters, radius, and propagation settings.
- `LaunchEffectSourceComponent` - Component containing the sockets selected by launch effect entries.
- `LaunchEffectSourceBindings` - Optional named component sources selected per effect.

**Delegates:**
- `OnHitValidationResult` - Server validated/rejected a Client Predicted hit claim.
- `OnFlightLogValidated` - Server validated/rejected a Client Authoritative flight log.
- `OnHitscanComplete` - A hitscan trace completed (with full result data).
- `OnAuthoritativeImpact` - Server-only, once-per-impact gameplay stream shared by projectile, hitscan, and validated client-authoritative paths. It does not depend on cosmetic routing or configured assets.
- `OnAuthoritativeFlyBy` - Server-only, once-per-listener gameplay stream for game-specific AI or gameplay consumers. It does not depend on cosmetic routing or `FlyBySound`.

### UBallisticsLaunchContributorComponent

Add this component to an actor that attaches beneath a launcher. It owns one
optional `UBallisticsLaunchCosmeticModifier` reference plus inline source
overrides. It replicates both and automatically binds or unbinds when its
attachment parent changes. It has no component Tick and creates no timers.

- `SourceOverrides` - Inline binding, component-tag, socket, and optional
  priority entries. Depth selects the winner unless priority is enabled.
- `Modifier` - Optional cosmetic data asset contributed to the nearest launcher.
  Modifiers apply by explicit priority, then attachment depth, then stable
  contributor path. Deeper contributors therefore win by default.
- `SetModifier(Modifier)` - Server/standalone-only runtime change.
- `RefreshLauncherBinding()` - Force an immediate ancestry refresh after an
  unusual reparent. Normal direct attach and detach changes are detected by the
  aggregate poll.
- `GetBoundLauncher()` - Current nearest launcher.

### UBallisticsDetonationComponent

Handles explosive detonation. Attach to any actor that can explode.

**Key Functions:**

`ConfigureExplosive(DataAsset)` resets the component, rebinds triggers, and
arms a configured fuse.
- `Detonate()` - Detonate at the actor's current location. Can only launch once.
- `DetonateAtLocation(Location)` - Detonate at a specific world position.
- `ArmFuse()` - Register a fuse countdown with the shared aggregate update.
- `HasDetonated()` - Whether this component has already detonated.
- `GetRemainingFuseTime()` - Remaining fuse countdown.

**Key Properties:**
- `ExplosiveDataAsset` - The explosive configuration driving this detonation.
- `bAutoArm` - Auto-start fuse on BeginPlay (FuseTimer trigger only).
- `IgnoredActors` - Actors to skip during blast/shrapnel traces.

**Delegates:**
- `OnDetonation` - Broadcasts with full FDetonationResult (blast hits, shrapnel hits, actor count).

### UProjectileTracerComponent

Visual tracer trail with automatic fade-out. Attach alongside the ballistics component.

### UProjectileNetProxyComponent

Compact transform replication for long-lived server-authoritative projectiles.
Add it to a replicated projectile actor and disable standard replicated movement.
On a non-authority replicated copy, NPS automatically disables and unregisters
the Ballistics component so NetProxy interpolation is the only movement owner.

**Key Functions:**
- `StartReplicatingTransform()` - Resume publishing, including after pooled reuse.
- `StopReplicatingTransform()` - Stop publishing after the projectile is spent.

## Subsystem

### UProjectileBallisticsSubsystem

World subsystem for global operations. Access via `GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>()`.

**Key Functions:**
- `PredictTrajectory(Conditions, Duration, StepSize)` - Pure math trajectory prediction. No actors spawned. Returns path points and snapshots.
- `GetProjectilesInRadius(Center, Radius)` - Query active projectiles near a point.
- `GetActiveProjectileCount()` - How many projectiles are currently in flight.
- `AcquireProjectile(Class, Location, Rotation, Owner, Instigator)` - Get a projectile from the pool or spawn fresh. Ownership is assigned before fresh-spawn lifecycle callbacks and before pooled activation.
- `ReleaseProjectile(Projectile)` - Return a projectile to the pool.
- `PreWarmPool(Class, Count)` - Pre-spawn pooled projectiles during level load.
- `GetImpactBudget()` - Access the VFX impact budget manager.
- `GetAggregateTicker()` - Access the aggregate tick handler.
- `GetCosmeticsManager()` - Access the cosmetic event manager.

Projectile registration changes raised by simulation callbacks are deferred until
the active aggregate batch finishes. Impact and detonation handlers can therefore
destroy or pool projectiles without mutating the array being stepped.

## Data Assets

### UBallisticsProjectileDataAsset

Per-projectile-type configuration. Assign to a launcher, swap at runtime.

**Sections:**
- **Identity / Projectile:** DisplayName, ProjectileID, ProjectileClass
- **Ballistics:** InitialSpeed, Mass, Diameter, DragCoefficient,
  WindAcceleration
- **Networking:** bOverrideAuthorityMode, AuthorityModeOverride
- **Hitscan:** bUseHitscan, HitscanConfig
- **Damage:** BaseDamage, DamageFalloffMultiplier, FalloffStartDistance, FalloffEndDistance
- **Visual:** TracerColor, TracerEmissiveIntensity, bUseTracerInterval, TracerInterval
- **Cosmetics:** FlyBySound, ImpactAudibleRadius, per-surface ImpactCosmetics, DefaultImpactCosmetic, bOverrideProjectDefaultImpactCosmetic
- **Spread:** SpreadConfig (reference to UBallisticsSpreadDataAsset)
- **Explosive:** ExplosiveConfig (reference to UBallisticsExplosiveDataAsset)
- **Materials:** MaterialOverride, EmbedBlacklist
- **Physics:** MaxRicochetsOverride, MaxPenetrationDepthOverride, SlidingOverride

**Functions:**
- `ResolveProjectileClass()` - Load the configured projectile class.
- `BuildInitialConditions()` - Build complete default conditions for one launch.
- `ApplyToConditions(BaseConditions)` - Merge projectile properties into initial conditions.
- `CalculateDamageAtDistance(Distance)` - Get damage with falloff applied.
- `UsesSpread()` - Has a spread config.
- `IsExplosive()` - Has an explosive config.
- `ShouldShowTracer(LaunchCounter)` - Whether this launch should use a visible tracer.
- `GetImpactCosmetic(Surface)` - Resolve the projectile surface entry, projectile fallback, project surface entry, or project catch-all in that order.

### UBallisticsExplosiveDataAsset

Per-explosive configuration.

**Sections:**
- **Trigger:** DetonationTrigger (OnImpact, FuseTimer, OnRest, Manual), FuseTime
- **Blast:** BlastRadius, BlastInnerRadius, BlastDamage, BlastMinDamage, bBlastRequiresLineOfSight
- **Shrapnel:** ShrapnelCount, ShrapnelRange, ShrapnelDamage, ShrapnelMass, ShrapnelInitialSpeed, MaxRicochets, MaxPenetrations
- **Cosmetics:** DetonationCosmetic sound, Niagara effect, scale, effect lifetime, pitch, volume, and routing radius

**Functions:**
- `CalculateBlastDamageAtDistance(Distance)` - Blast damage with inner/outer radius falloff.
- `CalculateShrapnelDamageAtDistance(Distance)` - Shrapnel damage with range falloff.
- `HasShrapnel()` / `HasBlastDamage()` - Quick checks.

### UBallisticsLaunchCosmeticProfile

Launcher-specific base signature. Stores launch sound, distant report,
Niagara effects, playback multipliers, listener radius, propagation speed, and
whether audio uses distance-based delay. Each effect can select a named source
binding and socket. `EffectLifeSpan` provides a hard aggregate-managed lifetime
for the spawned Niagara component. Per-effect parameter arrays support bool,
int, float, Vector2, Vector3, linear color, static mesh, material, and texture
Niagara overrides. Names are exact and include `User.` when the Niagara
parameter is exposed as a user parameter. The Niagara system must define the
matching name and type. The resolved payload retains up to 16 valid typed
parameters per effect and applies them before activation.
Profile `BoolParameters` and `FloatParameters`
contain named values with independent audio and Niagara targets. Niagara values
are set before activation, and delayed audio carries the resolved values until
playback.

### UBallisticsLaunchCosmeticModifier

Optional runtime layer for configuration changes. It can override sounds and
edit named Niagara roles while multiplying effect scale, volume, pitch,
audible radius, and visual radius. `EffectOperations` applies Add, Replace, or
Remove by `EffectId`. `SourceOverrides` maps a profile binding to a component
tag on the contributor actor and can replace its socket. This legacy field
remains supported; new position-only attachments can author the same entries
inline on `UBallisticsLaunchContributorComponent`. New modifiers default to
attachment-depth ordering. Enable `bOverridePriority` to apply an explicit
`Priority`; higher values apply later, with depth and contributor path as
tie-breakers. Existing serialized assets are migrated into explicit-priority
mode so their previous ordering remains intact until the override is disabled.
`BoolParameterOverrides` and `FloatParameterOverrides` replace profile entries
with the same name and add entries whose names are not present. Replacing an
effect also replaces its per-effect typed Niagara parameter arrays.
### UProjectileCosmeticsManager

Owned automatically by `UProjectileBallisticsSubsystem`; users do not place or
configure a second subsystem. It batches per-listener network events, creates
owner-only relays, reconstructs sustained sequences, applies speed-of-sound
delay, bounds active Niagara components, stops expired effects without timers,
and exposes `OnCosmeticEvent` for project-specific playback. Remote visual and
audio payloads are separated. The server sends resolved visual assets
immediately and releases resolved audio assets only when their propagation time
has elapsed.

### UBallisticsSpreadDataAsset

Spread pattern configuration for multi-projectile launches.

**Modes:**
- `RandomCone` - Uniform random distribution within a cone angle.
- `FixedPattern` - Designer-placed directional pattern points.
- `HybridPattern` - Fixed pattern with random variance.

**Functions:**
- `GenerateProjectileDirections(Direction, Random, OutDirections)` - Get projectile directions.
- `GenerateProjectileConditions(BaseConditions, Direction, Random, OutConditions)` - Get full per-projectile conditions with velocity/mass variation.

### UBallisticMaterialDataAsset

Maps physical surface types to ballistic responses. Create one, fill in the map, assign to components.

**FBallisticMaterialResponse:**
- `Toughness` - Resistance to penetration (arbitrary units).
- `RicochetAngleThreshold` - Impact angles below this cause ricochet (degrees).
- `CoefficientOfRestitution` - Energy retained on bounce (0-1).
- `DefaultResponse` - Fallback if no angle/energy check overrides it.

## Function Libraries

### UBallisticsHitscanLibrary

Static functions for instant hitscan traces. No actors spawned.

- `PerformHitscan(WorldContext, Origin, Direction, Config)` - Single trace with ricochets and penetration. Returns FHitscanResult with all hits and path points.
- `PerformRadialHitscan(WorldContext, Origin, TraceCount, Config)` - Radial traces for shrapnel/fragmentation. Returns array of results.

### UBallisticsTrajectoryLibrary

Static math functions for trajectory computation.

- `EvaluateTrajectory(Conditions, Time)` - Snapshot at a given time.
- `SolveLaunchAngle(...)` - Find the launch angle to hit a target.
- `SolveLaunchDirection(...)` - Find the full launch direction to hit a moving target.

## Budget System

### UBallisticsImpactBudget

Manages VFX impact throttling. Accessed via the subsystem.

- `SubmitImpact(Request)` - Submit an impact for budget evaluation. Returns Allowed, Reduced, or Denied.
- `GetFrameImpactsUsed()` - How many impacts have been allowed this frame.
- `GetFrameBudgetMax()` - The frame budget limit from settings.

**Delegate:**
- `OnImpactBudgetResult` - Broadcasts for every submitted impact with the budget decision. Bind your VFX spawning logic here.

## Interfaces

### IBallisticsLaunchValidator

Implement on a launcher owner, an actor in its owner chain, or the launching
pawn to apply project rules before NPS accepts a remote launch. NPS supplies an
`FBallisticsLaunchValidationContext` containing authority mode, canonical
origin and direction, projectile ID, client timestamp, and the server-owned
projectile data asset. Return `false` from `ValidateBallisticsLaunch` to reject
the request before projectile spawn or client-authoritative snapshot creation.

All matching validators must accept the request. Typical checks include
ownership, selected projectile, equipped state, ability state, and allowed
launch origin or aim. NPS applies finite-value checks, the hard request ceiling,
optional cadence, launch credits, spread membership, and replay protection
before calling project validators.

### IBallisticsDamageInterface

Implement on any actor that can receive ballistic damage.

- `OnBallisticDamage(Context)` - Called when a projectile hits this actor. FBallisticDamageContext contains: HitResult, ImpactSnapshot, InitialConditions, ImpactResponse, KineticEnergy, DistanceTraveled, PenetrationDepth, Instigator, ProjectileActor, and ProjectileDataAsset.

## Enums

- `EProjectileState` - InFlight, Ricocheted, Penetrating, Embedded, Sliding, Destroyed
- `EImpactResponse` - Ricochet, Penetrate, Embed, Shatter, None
- `ELaunchAuthorityMode` - ServerAuthoritative, ClientPredicted, ClientAuthoritative
- `EServerAuthCosmeticMode` - NoLocalCosmetic, ImmediateLocalCosmetic, FastForwardedCosmetic
- `EDetonationTrigger` - OnImpact, FuseTimer, OnRest, Manual
- `EImpactPriority` - Low, Normal, High, Critical
- `EImpactBudgetResult` - Allowed, Reduced, Denied
- `ESpreadMode` - RandomCone, FixedPattern, HybridPattern
- `EProjectileFlightValidationResult` - Valid, Expired, VisibilityFailed, InvalidRicochetAngle, InvalidPenetration, InvalidTarget, BufferFull, UnknownLaunch, InvalidInitialConditions, InvalidTimestamp, InvalidSegment, PhysicallyImplausible
