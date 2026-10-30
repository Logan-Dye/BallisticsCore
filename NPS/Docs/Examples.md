# NPS Example Suite

The plugin includes a self-running example suite under
`NPS/Content/Examples`. The maps, data assets, and example actors are
distributable plugin content; they do not depend on project-specific classes.

Open `NPSProject/NPSTestLab.uproject` and press Play. The project starts in
`NPS_Playground`, a playable first-person target range. Switch between three
weapon child classes, all three authority modes, standard or explosive
projectiles, and calm or crosswind conditions while the game is running.
The example layer adds neither actor Tick nor timers.

Each map contains a directional light, sky atmosphere, movable real-time
skylight, and exponential height fog. The showcase also restores any missing
environment actor when embedded in a custom map.

## In-game guide

The example maps need no Blueprint or UMG setup. A shared C++ HUD provides:

- `H` to show or hide the guide
- `M` to open the example map browser
- Up and Down to select a map
- Enter to open the selected map
- Escape or `M` to close the browser

Standalone players can open any catalog map directly. A listen-server host
travels the connected session. Remote clients can inspect the catalog but the
host controls travel.

Every automated station has a world-space board with its purpose, expected
result, launch request count, target hit count, active projectile count, and
current pool size. After a hit, it also reports lateral impact offset so wind
and spread behavior are measurable. The board is refreshed through the NPS aggregate update
path. It does not add actor Tick or per-station timers.

## Focused maps

| Map | Demonstrates |
| --- | --- |
| `NPS_Playground` | Playable first-person rifle, pistol, and launcher with runtime networking, explosive, and wind controls |
| `NPS_Showcase` | All stations in one flyable lab |
| `NPS_AuthorityModes` | Server-authoritative, client-predicted, and client-authoritative launching |
| `NPS_Trajectories` | High velocity, subsonic, arrow arc, and deterministic crosswind |
| `NPS_MaterialResponses` | Penetration, ricochet, embed, and sliding configuration |
| `NPS_MultiProjectile` | Hitscan, random-cone spread, and fixed-pattern spread |
| `NPS_Explosives` | Impact, fuse, rest, and manual detonation triggers |
| `NPS_Cosmetics` | Base profile, runtime modifier, sustained sequence, impacts, fly-by data, and distant routing |
| `NPS_NetworkScale` | Cosmetic delivery outside a 100 cm actor net-cull distance |
| `NPS_Performance` | Pool-backed 32-projectile bursts with sparse tracers |

## Reusable assets

The catalog contains 53 projectile assets, 4 explosive assets, 3 material
response assets, 3 deterministic spread assets, 1 launch cosmetic profile,
1 launch cosmetic modifier, 1 example catalog, and 10 maps. The playable sample also includes
27 First Person template weapon assets under `NPS/FirstPerson`.

`/NPS/Examples/Data/DA_NPS_ExampleCatalog` is the inspectable source for map
descriptions, station explanations, expected results, linked data assets, and
quick facts. `Docs/NPS_ExampleCatalog.json` contains the same catalog fields for
websites, searchable documentation, and external tooling.

Every projectile asset resolves to `ANPSExampleProjectile`, a complete
reference composition containing:

- `UProjectileBallisticsComponent`
- `UProjectileNetProxyComponent`
- `UProjectileTracerComponent`
- `UBallisticsDetonationComponent`

The assets use soft cosmetic references. Dedicated servers can route events
without loading the referenced Niagara or audio assets.

## Multiplayer inspection

For a local listen-server check:

1. Open `NPS_Playground`.
2. Set **Number of Players** to 2.
3. Set **Net Mode** to **Play As Listen Server**.
4. Start PIE.

The host and remote client can change their own weapon and projectile settings
and fire through the same replicated character path. The authority selection
is validated and applied by the server. Both clients receive the logical
launch, impact, fly-by, and detonation events appropriate to their listener
distance.

For dedicated-server behavior, select **Play As Client** with a dedicated
server enabled. The examples use the same server-owned manager and owner-only
relays as production gameplay.

## Playground controls

| Input | Action |
| --- | --- |
| `WASD`, mouse | Move and look |
| `Space` | Jump |
| Left mouse | Fire |
| `1`, `2`, `3` | Automatic rifle, precision pistol, heavy launcher |
| `F1`, `F2`, `F3` | Server authoritative, client predicted, client authoritative |
| `E` | Toggle explosive projectiles |
| `V` | Toggle crosswind |
| `F4` | Toggle all NPS debug drawing |
| `H` | Show or hide the example guide |
| `M` | Open or close the example map browser |

The HUD displays the current network role and every selected runtime option.

## Debugging

```text
NPS.Debug.Projectiles
NPS.Debug.Hitscan
NPS.Debug.Detonation
NPS.Debug.Budget
NPS.Debug.All
```

Targets display replicated hit count and last kinetic energy. Automation
verifies all 36 playground projectile combinations, each weapon child, the
playground map and environment, client-to-server configuration replication,
and damage through all three authority modes.

## Station reference

| Station | What it proves | Expected result |
| --- | --- | --- |
| Server Authoritative | Server-owned simulation and damage | All clients converge on the server projectile and impact |
| Client Predicted | Immediate owner prediction with server validation | Immediate local presentation with correction when required |
| Client Authoritative | Bounded client flight-log validation | Validated segments drive server damage and remote presentation |
| High Velocity Projectile | Fast, low-drag trajectory | Nearly level path before impact |
| Subsonic Heavy | Slow, heavy projectile response | Earlier drop than the high-velocity lane |
| Arrow - Gravity Arc | Low-speed gravity behavior | Visible arc into the target |
| Crosswind Drift | Positive-Y wind acceleration | Path bends toward positive Y |
| Penetration | Energy-based penetrable material response | Trace continues through the target with reduced energy |
| Ricochet | Shallow-angle ricochet response | Projectile changes direction instead of stopping |
| Embed / Slide | Terminal surface response | Projectile embeds or enters the configured slide response |
| Hitscan | Immediate path resolution through the launch API | Target records an immediate hit |
| Random Cone Spread | Deterministic random-cone sampling | Repeated cycles distribute around the forward axis |
| Fixed Pattern Spread | Authored 2D spread points | Every cycle reproduces the same pattern |
| Impact Detonation | On-impact explosive trigger | Blast and fragments originate at impact |
| Fuse Timer | Aggregate-managed armed fuse | Detonation occurs after 0.65 seconds |
| Detonate On Rest | Rest-triggered explosive | Detonation follows the transition to rest |
| Manual Detonation | Explicit gameplay detonation | The example detonates shortly after launch |
| Base Launch Profile | Socket-bound launch audio and Niagara | Base sound, flash, smoke, and casing effects play |
| Modified Launch Profile | Contributor-based cosmetic modification | Scale, volume, pitch, and radius change |
| Sustained Sequence | Aggregate-managed launch sequence | Presentation repeats at 12 launches per second |
| Cosmetics Beyond 100 cm Net Cull | Cosmetic routing outside actor relevancy | Remote listener receives presentation while the launcher is culled |
| Pooled 32 Projectile Spread | Pooling, sparse tracers, and aggregate load | Each request emits 32 projectiles and retired actors return to the pool |

## Internal catalog generation

The checked-in assets are ready to use. Development builds of `NPSProject`
include a project-local `NPSInternalTools` plugin that rebuilds the catalog from
source definitions:

```powershell
.\RegenerateNPSExamples.bat
```

The commandlet writes the catalog beneath `NPS/Content/Examples` and exports
`NPS/Docs/NPS_ExampleCatalog.json`. It is not a
module or commandlet in the distributable NPS plugin, and `BuildPlugin` does not
package it.
