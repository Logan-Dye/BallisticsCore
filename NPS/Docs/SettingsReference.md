# Settings Reference

All settings are in **Project Settings -> Game -> Networked Projectile System** and stored in `DefaultGame.ini` under `[/Script/BallisticsCoreRuntime.BallisticsCoreSettings]`.

## Authority

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `LaunchAuthorityMode` | Enum | ClientPredicted | Global authority mode for all launchers |

## Server Authoritative Settings

*Only visible when LaunchAuthorityMode = ServerAuthoritative*

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `ServerAuthCosmeticMode` | Enum | ImmediateLocalCosmetic | How the launching client sees their own projectile |

## Client Predicted Settings

*Only visible when LaunchAuthorityMode = ClientPredicted*

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `MaxLagCompensationWindow` | Float | 0.25s | Max time window for lag compensation |
| `PredictionReconciliationTolerance` | Float | 10 cm | Distance tolerance before server corrects client |

## Client Authoritative Settings

*Only visible when LaunchAuthorityMode = ClientAuthoritative*

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `MaxSavedFlightLogs` | Int | 96 | Ring buffer size per player (8-256) |
| `MaxFlightLogAge` | Float | 2.0s | Max time from launching through flight-log receipt; increase for long-flight projectiles |
| `RicochetAngleValidationTolerance` | Float | 10 degrees | Degrees of tolerance for ricochet angle checks |
| `ClientAuthHitLocationTolerance` | Float | 100 cm | Distance tolerance for hit actor validation |
| `ClientAuthFutureTimestampTolerance` | Float | 0.05s | Maximum timestamp lead over server time |
| `ClientAuthTrajectoryTolerance` | Float | 500 cm | Position tolerance for trajectory plausibility checks |
| `ClientAuthSpeedTolerance` | Float | 0.1 | Fractional tolerance for reported speed |

## Remote Launch Admission

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `MaxClientLaunchRequestsPerSecond` | Int | 256 | Hard per-launcher ceiling for remote launch RPC admission |
| `DefaultLaunchCreditExpirySeconds` | Float | 1.0s | Lifetime used when a credit grant does not provide an override |

Per-launcher admission is configured on `UProjectileLauncherComponent`:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `MinLaunchInterval` | Float | 0 | Enables a three-token remote cadence bucket; one token refills per interval |
| `bRequireLaunchCredits` | Bool | false | Requires one unexpired server grant per remote top-level launch |

Call `GrantLaunchCredits` only from an authoritative game fire path after the
action and inventory cost are accepted. One spread consumes one credit.
`ClearLaunchCredits` cancels grants on unequip, reload, death, or another
project-owned cancellation. AI, standalone, and listen-host launches bypass
both admission layers.

## Performance

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `bEnableProjectilePooling` | Bool | true | Enable object pooling for projectile actors |
| `DefaultProjectilePoolSize` | Int | 64 | Pre-spawn pool size (4-512) |
| `bEnableTickBatching` | Bool | true | Batch-tick via aggregate ticker instead of individual component ticks |
| `AggregateTickClass` | Class | UBallisticsAggregateTick | Custom tick class for priority/LOD strategies |

## Impact Budget

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `bEnableImpactBudget` | Bool | true | Enable VFX impact throttling |
| `MaxImpactsPerFrame` | Int | 32 | Max full-VFX impacts per frame (1-256) |
| `MaxImpactsPerProjectile` | Int | 8 | Max impacts per projectile lifetime (0=unlimited, 0-32) |
| `ImpactBudgetFadeDistance` | Float | 30000 cm | Distance beyond which impact priority is demoted (0=disabled) |

## Cosmetics

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `bEnableBuiltInCosmetics` | Bool | true | Enable built-in launch, fly-by, impact, and detonation playback |
| `MaxCosmeticEventsPerConnectionPerFrame` | Int | 64 | Hard per-connection and local-world event ceiling (1-256) |
| `MaxActiveLaunchSequencesPerClient` | Int | 32 | Maximum reconstructed sustained sequences (1-128) |
| `MaxLaunchCosmeticRate` | Float | 60/s | Maximum sustained launch playback rate (1-120/s) |
| `MaxClientCosmeticClaimsPerSecond` | Int | 256 | Server admission ceiling per client-owned launcher (1-512/s) |
| `MaxCosmeticRoutingRadius` | Float | 2000000 cm | Runtime routing-distance ceiling (1000-10000000 cm) |
| `MaxPendingCosmeticAudioEvents` | Int | 256 | Maximum server acoustic deliveries or local delayed sounds retained by an aggregate queue (1-2048) |
| `MaxActiveCosmeticEffects` | Int | 256 | Maximum Niagara components retained by the aggregate cleanup queue (1-2048) |
| `MaxCosmeticAudioDelay` | Float | 15 s | Maximum distance-propagation delay; longer deliveries are dropped instead of played early (0-30 s) |
| `bAutoRegisterPlayerFlyByListeners` | Bool | true | Automatically register local and remote player pawns for fly-by checks |

## Physics

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `GravityZ` | Float | -980 cm/s^2 | Global gravity. Negative = downward. 0 = zero-G. |
| `AirDensity` | Float | 0.001225 g/cm^3 | Global air density. Sea level Earth default. |

## Defaults

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `DefaultImpactCosmeticsBySurface` | Map: Physical Surface to Impact Cosmetic | Empty | Project-wide surface responses inherited by projectile assets that do not override the matching surface |
| `DefaultImpactCosmetic` | Impact Cosmetic | Empty | Project-wide Niagara, sound, decal, scale, volume, pitch, size, and lifetime catch-all for surfaces not present in either map |
| `DefaultMaterialDataAsset` | Material Data Asset | None | Project-wide material response asset used when the projectile and component do not provide one |
| `DefaultProjectileClass` | Actor Class | None | Project-wide projectile class used when the active projectile has no class |

Impact cosmetics resolve in this order: a matching `ImpactCosmetics` entry on the projectile asset, its authored `DefaultImpactCosmetic`, a matching project `DefaultImpactCosmeticsBySurface` entry, then the project-wide `DefaultImpactCosmetic`. Enable `bOverrideProjectDefaultImpactCosmetic` on a projectile to intentionally replace both project fallbacks with an empty cosmetic.

## INI Example

```ini
[/Script/BallisticsCoreRuntime.BallisticsCoreSettings]
LaunchAuthorityMode=ClientPredicted
MaxLagCompensationWindow=0.25
PredictionReconciliationTolerance=10.0
bEnableProjectilePooling=true
DefaultProjectilePoolSize=64
bEnableTickBatching=true
bEnableImpactBudget=true
MaxImpactsPerFrame=32
MaxImpactsPerProjectile=8
ImpactBudgetFadeDistance=30000.0
bEnableBuiltInCosmetics=true
MaxCosmeticEventsPerConnectionPerFrame=64
MaxActiveLaunchSequencesPerClient=32
MaxLaunchCosmeticRate=60.0
MaxClientCosmeticClaimsPerSecond=256
MaxCosmeticRoutingRadius=2000000.0
MaxPendingCosmeticAudioEvents=256
MaxActiveCosmeticEffects=256
MaxCosmeticAudioDelay=15.0
bAutoRegisterPlayerFlyByListeners=true
GravityZ=-980.0
AirDensity=0.001225
```
