# Authority Modes

NPS supports three launch authority modes that control how projectile spawning, simulation, and hit validation work across the network. Set the global mode in Project Settings -> Game -> Networked Projectile System, or override per projectile asset.

## Server Authoritative

The safest mode. The server has full control over every projectile.

**Flow:**
1. Client presses launch -> sends origin, direction, and deterministic seed through `ServerRequestLaunch`.
2. Server rebuilds mass, diameter, drag, wind, and initial speed from its projectile data asset, then spawns and simulates the projectile.
3. Replicating projectile actors use `UProjectileNetProxyComponent` for compact, relevant transform updates. Client copies automatically leave Ballistics simulation and use NetProxy interpolation only. Non-replicating projectiles use cosmetic multicast.
4. Clients see the authoritative projectile or its cosmetic representation.

**Cosmetic Modes:**
Because the client waits for the server to spawn the projectile, there's a full round-trip delay before the launching player sees anything. NPS offers three cosmetic options to mitigate this (configurable in settings):

- **NoLocalCosmetic** - Client sees nothing until the server multicast arrives. Simplest, most latency visible.
- **ImmediateLocalCosmetic** - Client spawns a visual-only projectile on input. Hides latency but the cosmetic may diverge from the server's authoritative projectile.
- **FastForwardedCosmetic** - Client spawns a cosmetic that fast-forwards by half-RTT to approximate the server projectile's current position. Best visual match with some complexity.

**When to use:** Competitive games where cheat prevention is the top priority and players accept some input latency (tactical shooters, tournament modes).

## Client Predicted

The balanced mode. Client gets instant feedback, server validates.

**Flow:**
1. Client presses launch -> spawns a local predicted projectile immediately.
2. Client sends origin, direction, deterministic seed, timestamp, and projectile ID through `ServerNotifyLaunch`.
3. Server rebuilds the physical conditions from its projectile data asset, spawns its own authoritative projectile, and fast-forwards it to account for network delay.
4. Server compares the collision-aware result against the expected trajectory.
5. If they diverge beyond `PredictionReconciliationTolerance`, server sends `ClientReconcileProjectile` with the matching projectile ID.
6. Client snaps or interpolates to the corrected state.

**Hit Validation:**
The server's own projectile simulation is authoritative for collision and damage.
Games that also want to submit an explicit client hit claim can call
`ServerCheckClientHit` with the hit actor, timestamp, and position, then bind
`OnValidateHit` to provide their game-specific plausibility rules. Claims are
rejected when no validator is bound.

**When to use:** Most multiplayer shooters. Good balance between responsiveness and server authority. This is the default mode.

## Client Authoritative

The most responsive mode. The client owns the entire launch.

**Flow:**
1. NPS records a launch registration before local simulation starts. Remote clients use a reliable server RPC; standalone and listen-host authority record it locally.
2. The server snapshots the launcher's current projectile data asset and canonical physical conditions by launch ID. The bounded snapshot map uses `MaxSavedFlightLogs` and `MaxFlightLogAge`.
3. The client spawns and fully simulates the projectile locally.
4. When the projectile hits something or finishes its path, the client builds an `FProjectileFlightLog` containing every segment of the path.
5. Client sends `ServerSubmitFlightLog` with the complete launch path.
6. Server consumes the matching launch snapshot and validates each segment:
   - **Initial conditions:** Mass, diameter, drag, wind, speed, origin, direction, and seed must match the server snapshot.
   - **Time:** Future timestamps, expired reports, non-increasing segment times, and excessive travel time are rejected.
   - **Trajectory:** The first impact position and speed must agree with the server-evaluated path within configured tolerances. Every segment start, endpoint, and sweep must remain inside the travel-distance envelope from the previous impact.
   - **Visibility:** Line trace from segment start to end - was anything blocking the path?
   - **Ricochet angles:** Does the claimed bounce angle make physical sense given the surface normal?
   - **Material penetrability:** Is the claimed penetration surface actually penetrable?
7. Server sends `ClientFlightLogResult` - accepted or rejected, with the failed segment index if rejected. A report without a matching snapshot returns `UnknownLaunch`.

Damage and authoritative impact events use the snapshotted projectile data asset,
even if the launcher changes configuration while the projectile is in flight.

Remote cosmetic assets are never selected by the client. NPS resolves them from
the server launcher and routes them through the target player's owner-only
relay. Client-authoritative fly-by claims do not carry a target pawn; the server
chooses a registered listener from server world state and recomputes the
listener-relative values.

Client-authoritative mode intentionally trusts the client to simulate flight.
The server still owns projectile configuration and rejects malformed or
physically implausible reports. Projects can implement
`IBallisticsLaunchValidator` on the launcher owner, an owner ancestor, or the
launching pawn for equipped-state, animation-state, ability, origin, aim, and
other project policy. Every matching validator must accept a remote launch
before NPS spawns a server projectile or records a client-authoritative
snapshot.

**Flight Log Ring Buffer:**
The server stores flight logs in a ring buffer (like Unreal's saved moves for character movement). `MaxSavedFlightLogs` controls the buffer size. Oldest logs are evicted when full.

## Authoritative Gameplay Events

`OnAuthoritativeImpact` and `OnAuthoritativeFlyBy` are server-only gameplay
streams in every authority mode. Each authoritative impact broadcasts once.
Each validated listener fly-by broadcasts once. These delegates remain active
when cosmetic routing is disabled and when no sound, Niagara, decal, profile,
or surface cosmetic is configured. Shooter exclusion runs before fly-by
broadcasts.

**When to use:** Co-op games, large player-count servers (50+ players), or projects that prioritize responsiveness. The server validates the client's simulation results without resimulating the trajectory.

## Per-Projectile Overrides

Different projectile configurations can use different authority modes:

```cpp
ProjectileDataAsset->bOverrideAuthorityMode = true;
ProjectileDataAsset->AuthorityModeOverride = ELaunchAuthorityMode::ServerAuthoritative;
```

Use case: projectiles can remain Client Predicted while a grenade uses Server
Authoritative so every player observes the same long-lived world object.

## Remote Launch Admission

`UProjectileLauncherComponent` owns two optional server-side admission layers
that work identically in all three authority modes.

`MinLaunchInterval` enables a three-token cadence bucket. One token refills per
configured interval. This allows up to three legitimate requests to arrive in
one server frame while bounding the sustained accepted rate. The owning client
uses the same bucket before local prediction or an RPC is produced. The server
bucket remains authoritative.

Set `bRequireLaunchCredits` to require one unexpired credit for each accepted
remote launch. It is disabled by default, preserving unlimited launch behavior.
The authoritative game fire path grants credits after accepting the action and
consuming inventory:

```cpp
Launcher->GrantLaunchCredits(1);
Launcher->Launch(Origin, Direction);
```

For a remote player, the game normally grants on the server and the owning
client calls `Launch`. `GrantLaunchCredits` is server-only. A rejected request
does not spend a credit. Replayed projectile or request IDs cannot spend a
second credit. `GetAvailableLaunchCredits` reports unexpired credits and
`ClearLaunchCredits` cancels outstanding authorization.

One top-level `Launch` call consumes one credit. A spread still consumes one
credit regardless of its projectile count. Every spread member carries the
same launch request ID, its own projectile ID, and a server-checked member index
and count.

Credits expire using `DefaultLaunchCreditExpirySeconds` unless the grant passes
a positive override. NPS clears credit and replay state when launcher ownership
changes and during `EndPlay`. Games should also call `ClearLaunchCredits` on
unequip, reload, death, or any cancellation that invalidates a previously
accepted action.

AI, standalone launches, and listen-host launches bypass remote cadence and
credits. `MaxClientLaunchRequestsPerSecond` remains a separate hard RPC ceiling
for denial-of-service protection.

## Settings Reference

All authority settings are in Project Settings -> Game -> Networked Projectile System. They use `EditCondition` to only show relevant options for the selected mode.

| Setting | Mode | Description |
|---------|------|-------------|
| `ServerAuthCosmeticMode` | Server Auth | How the launching client sees their projectile |
| `MaxLagCompensationWindow` | Client Predicted | Max time window for lag compensation |
| `PredictionReconciliationTolerance` | Client Predicted | Distance tolerance before server corrects |
| `MaxSavedFlightLogs` | Client Auth | Ring buffer size per player |
| `MaxFlightLogAge` | Client Auth | Max age before server rejects a flight log |
| `RicochetAngleValidationTolerance` | Client Auth | Degrees of tolerance for ricochet angle checks |
| `ClientAuthHitLocationTolerance` | Client Auth | Distance tolerance for hit actor validation |
| `ClientAuthFutureTimestampTolerance` | Client Auth | Allowed timestamp lead over server time |
| `ClientAuthTrajectoryTolerance` | Client Auth | Position tolerance for trajectory validation |
| `ClientAuthSpeedTolerance` | Client Auth | Fractional speed tolerance for trajectory validation |
| `MaxClientLaunchRequestsPerSecond` | All remote modes | Hard per-launcher admission ceiling |
| `DefaultLaunchCreditExpirySeconds` | All remote modes | Default lifetime of a granted launch credit |
