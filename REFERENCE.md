# BallisticsCore — Implementation Reference Guide

This is a coding reference for building the plugin yourself. It covers the directory layout, every file you need to create, what goes in each one, and example snippets showing the patterns. **These are examples, not copy-paste code** — adapt everything to your own style.

---

## Directory Structure

```
BallisticsCore/
├── BallisticsCore.uplugin
├── Content/                          # Example assets (projectile data tables, etc.)
├── Resources/                        # Plugin icon, etc.
│   └── Icon128.png
└── Source/
    ├── BallisticsCoreRuntime/         # Runtime module — ships with the game
    │   ├── BallisticsCoreRuntime.Build.cs
    │   ├── Public/
    │   │   ├── BallisticsCoreRuntimeModule.h
    │   │   ├── BallisticsTypes.h               # All structs, enums, delegates
    │   │   ├── ProjectileBallisticsComponent.h  # Per-projectile physics
    │   │   ├── ProjectileLauncherComponent.h    # Weapon-side: spawn, replicate, lag-comp
    │   │   ├── ProjectileBallisticsSubsystem.h  # World subsystem for batch ops
    │   │   └── BallisticsTrajectoryLibrary.h    # Static BP library for trajectory prediction
    │   └── Private/
    │       ├── BallisticsCoreRuntimeModule.cpp
    │       ├── ProjectileBallisticsComponent.cpp
    │       ├── ProjectileLauncherComponent.cpp
    │       ├── ProjectileBallisticsSubsystem.cpp
    │       └── BallisticsTrajectoryLibrary.cpp
    │
    ├── BallisticsCoreEditor/          # Editor module — visualizers, gizmos
    │   ├── BallisticsCoreEditor.Build.cs
    │   ├── Public/
    │   │   └── BallisticsCoreEditorModule.h
    │   └── Private/
    │       ├── BallisticsCoreEditorModule.cpp
    │       └── TrajectoryVisualizer.cpp         # Viewport gizmo for trajectory preview
    │
    └── BallisticsCoreTests/           # Automation tests
        ├── BallisticsCoreTests.Build.cs
        └── Private/
            ├── BallisticsCoreTestsModule.cpp
            └── TrajectoryMathTests.cpp
```

---

## File-by-File Breakdown

### 1. BallisticsCore.uplugin

Standard UE plugin descriptor. Key things to get right:

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0.0",
    "FriendlyName": "Ballistics Core",
    "Description": "Deterministic projectile ballistics with multiplayer lag compensation.",
    "Category": "Gameplay",
    "CreatedBy": "Your Name",
    "CreatedByURL": "",
    "DocsURL": "",
    "MarketplaceURL": "",
    "SupportURL": "",
    "CanContainContent": true,
    "IsBetaVersion": false,
    "IsExperimentalVersion": false,
    "Installed": false,
    "Modules": [
        {
            "Name": "BallisticsCoreRuntime",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        },
        {
            "Name": "BallisticsCoreEditor",
            "Type": "Editor",
            "LoadingPhase": "Default"
        },
        {
            "Name": "BallisticsCoreTests",
            "Type": "Developer",
            "LoadingPhase": "Default"
        }
    ]
}
```

**Notes:**
- `Type: "Developer"` for the test module keeps it out of shipping builds.
- `CanContainContent: true` so you can ship example data tables.

---

### 2. Build.cs Files

#### BallisticsCoreRuntime.Build.cs

```csharp
using UnrealBuildTool;

public class BallisticsCoreRuntime : ModuleRules
{
    public BallisticsCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "NetCore",
            "PhysicsCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "RenderCore"    // Only if you do tracer rendering helpers
        });
    }
}
```

#### BallisticsCoreEditor.Build.cs

```csharp
using UnrealBuildTool;

public class BallisticsCoreEditor : ModuleRules
{
    public BallisticsCoreEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "BallisticsCoreRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "ComponentVisualizers"
        });
    }
}
```

#### BallisticsCoreTests.Build.cs

```csharp
using UnrealBuildTool;

public class BallisticsCoreTests : ModuleRules
{
    public BallisticsCoreTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "BallisticsCoreRuntime"
        });
    }
}
```

---

### 3. BallisticsTypes.h — The Struct Bible

This is where all your data types live. Keep it as the single source of truth so other headers just include this one.

**Key types you'll want:**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BallisticsTypes.generated.h"

// --- Enums ---

UENUM(BlueprintType)
enum class EProjectileState : uint8
{
    InFlight,
    Ricocheted,
    Penetrating,
    Embedded,
    Sliding,
    Destroyed
};

UENUM(BlueprintType)
enum class EImpactResponse : uint8
{
    Ricochet,
    Penetrate,
    Embed,
    Shatter    // Projectile is destroyed on impact
};

// --- Structs ---

// Initial conditions — everything needed to deterministically reproduce a trajectory
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileInitialConditions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Origin;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Direction;       // Normalized

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MuzzleVelocity;    // cm/s (UE units)

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Mass;              // grams

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Diameter;          // cm — for drag cross-section

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DragCoefficient;   // Cd, dimensionless (0.3-0.5 typical for bullets)

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 RandomSeed;        // For deterministic ricochet/spread variance
};

// Snapshot of projectile state at a point in time
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FVector Position;

    UPROPERTY(BlueprintReadOnly)
    FVector Velocity;

    UPROPERTY(BlueprintReadOnly)
    float ElapsedTime;

    UPROPERTY(BlueprintReadOnly)
    EProjectileState State;
};

// Per-material response data — you'll pull this from physical materials
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticMaterialResponse
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Toughness;                // Resistance to penetration (arbitrary units, tune by feel)

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RicochetAngleThreshold;   // Degrees — below this, ricochet; above, penetrate

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CoefficientOfRestitution; // 0-1, how much energy retained on bounce

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EImpactResponse DefaultResponse;
};

// --- Delegates ---

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnProjectileImpact,
    const FHitResult&, HitResult,
    const FProjectileSnapshot&, SnapshotAtImpact,
    EImpactResponse, Response
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnProjectileStateChanged,
    EProjectileState, OldState,
    EProjectileState, NewState
);
```

**Design notes:**
- `FProjectileInitialConditions` is the key to determinism. If two machines have the same initial conditions + same seed, they produce the same trajectory. This struct gets replicated.
- Keep `Mass` and `Diameter` separate from drag coefficient — lets designers tune without understanding physics.
- The `Toughness` value in `FBallisticMaterialResponse` is intentionally arbitrary units. Trying to simulate real penetration physics (joules vs BHN hardness) is a rabbit hole. Tune by feel, document the tuning curves.

---

### 4. Trajectory Math — The Carpentier Approach

This is the heart of the plugin. The core idea from Carpentier's approach:

**Instead of tick-accumulating** (position += velocity * dt each frame, which drifts), you compute position as a **closed-form function of time from initial conditions**. This means:
- No drift over time
- Any machine can compute any point on the trajectory without simulating all prior frames
- Replays are trivially deterministic

#### The math (for your implementation):

**Without drag (simple parabolic):**
```
Position(t) = Origin + Direction * MuzzleVelocity * t + 0.5 * Gravity * t^2
Velocity(t) = Direction * MuzzleVelocity + Gravity * t
```

**With drag (the interesting part):**

Drag force: `F_drag = 0.5 * AirDensity * Cd * CrossSectionArea * |V|^2` (opposing velocity)

This makes the ODE nonlinear, so there's no exact closed-form. Carpentier's approach uses a **piecewise linear approximation** — subdivide the trajectory into small segments where drag is approximately constant, then solve each segment analytically.

**Example pattern for your solver:**

```cpp
// This is the conceptual pattern — not production code, just to show the approach

FVector EvaluatePosition(const FProjectileInitialConditions& Init, float Time, const FVector& Wind)
{
    // Constants
    const float AirDensity = 1.225f; // kg/m^3 at sea level (convert to UE units)
    const float CrossSection = PI * FMath::Square(Init.Diameter * 0.5f);
    const float Gravity = -980.f;    // cm/s^2

    // For the drag case, step through small analytic segments
    // Each segment: assume drag is constant over the interval,
    // compute velocity decay, then position from that

    FVector Pos = Init.Origin;
    FVector Vel = Init.Direction * Init.MuzzleVelocity;
    float Remaining = Time;
    const float StepSize = 0.01f;  // 10ms segments — tune this

    while (Remaining > 0.f)
    {
        float dt = FMath::Min(StepSize, Remaining);

        // Drag deceleration magnitude this segment
        float Speed = Vel.Size();
        float DragAccel = 0.f;
        if (Speed > KINDA_SMALL_NUMBER)
        {
            // F = 0.5 * rho * Cd * A * v^2, then a = F/m
            DragAccel = (0.5f * AirDensity * Init.DragCoefficient * CrossSection * Speed * Speed)
                        / (Init.Mass * 0.001f); // Mass in kg
        }

        // Apply drag (opposes velocity) + gravity + wind
        FVector Accel = FVector(0.f, 0.f, Gravity);
        if (Speed > KINDA_SMALL_NUMBER)
        {
            Accel -= Vel.GetSafeNormal() * DragAccel;
        }
        Accel += Wind; // Wind as acceleration (simplification)

        // Analytic step: constant acceleration over dt
        Pos += Vel * dt + 0.5f * Accel * dt * dt;
        Vel += Accel * dt;

        Remaining -= dt;
    }

    return Pos;
}
```

**Important implementation choices:**
- The step size (10ms above) controls accuracy vs cost. For trajectory *prediction* (drawing a line), you can use larger steps. For actual simulation during gameplay, match your physics tick.
- **Determinism trap:** `FMath::Sin`, `FMath::Cos`, `FMath::Sqrt` are IEEE 754 on the same platform but can differ across platforms due to FMA (fused multiply-add). If you need cross-platform determinism, you may need to force specific instruction ordering. For same-platform multiplayer (client + server both on x86), this usually isn't an issue.
- Wind as a constant acceleration is a simplification. Real wind varies with altitude and terrain. For v1.0, constant per-projectile is fine. v1.1 can add wind zones.

---

### 5. ProjectileBallisticsComponent — Pattern

This is a `UActorComponent` that lives on your projectile actor and owns the physics state.

**Key responsibilities:**
- Store initial conditions
- Tick: evaluate position at current time, sweep for collisions between old and new position
- On hit: decide response (ricochet/penetrate/embed) based on material + angle + energy
- Broadcast delegates on impact and state change

**Example structure to follow:**

```cpp
UCLASS(ClassGroup=(Ballistics), meta=(BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileBallisticsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UProjectileBallisticsComponent();

    // Call this to start the projectile (usually from the launcher)
    UFUNCTION(BlueprintCallable, Category = "Ballistics")
    void InitializeProjectile(const FProjectileInitialConditions& Conditions);

    // Get current snapshot without advancing time
    UFUNCTION(BlueprintPure, Category = "Ballistics")
    FProjectileSnapshot GetCurrentSnapshot() const;

    // Fast-forward to a specific time (for server catch-up / replays)
    UFUNCTION(BlueprintCallable, Category = "Ballistics")
    FProjectileSnapshot EvaluateAtTime(float Time) const;

    // --- Delegates ---
    UPROPERTY(BlueprintAssignable, Category = "Ballistics")
    FOnProjectileImpact OnImpact;

    UPROPERTY(BlueprintAssignable, Category = "Ballistics")
    FOnProjectileStateChanged OnStateChanged;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    // The initial conditions — replicated for client prediction
    UPROPERTY(Replicated)
    FProjectileInitialConditions InitialConditions;

    UPROPERTY()
    float ElapsedTime;

    UPROPERTY()
    EProjectileState CurrentState;

    UPROPERTY()
    FRandomStream DeterministicRandom;

    // Internal: sweep between two positions, handle hits
    bool ProcessSweep(const FVector& Start, const FVector& End, float DeltaTime);

    // Internal: decide what happens on impact
    EImpactResponse DecideImpactResponse(const FHitResult& Hit, const FVector& Velocity);
};
```

**Tick pattern:**
```
TickComponent:
  1. Compute new elapsed time
  2. Evaluate position at (ElapsedTime - DeltaTime) and at ElapsedTime
  3. Line trace / sweep between old and new position
  4. If hit:
     a. Compute exact hit time (interpolate)
     b. Look up material response
     c. Call DecideImpactResponse
     d. Branch: Ricochet → reflect velocity, continue; Penetrate → backtrack exit point;
        Embed → stop; Shatter → destroy
     e. Fire OnImpact delegate
  5. Set actor location to new position
```

---

### 6. Ricochet / Penetration Decision Logic

The angle-based decision is straightforward:

```
ImpactAngle = angle between velocity and surface normal
If ImpactAngle < material.RicochetAngleThreshold:
    → Ricochet (shallow angle, glances off)
Else if KineticEnergy > material.Toughness:
    → Penetrate (enough energy to go through)
Else:
    → Embed (not enough energy to punch through, not shallow enough to bounce)
```

**Ricochet reflection:**
```cpp
// Reflect velocity off surface normal, apply energy loss
FVector ReflectedVel = Velocity - 2.f * FVector::DotProduct(Velocity, Normal) * Normal;
ReflectedVel *= Material.CoefficientOfRestitution;
```

**Penetration exit-point backtracing:**
```cpp
// Trace backward from a point past the surface to find where the projectile exits
// 1. Continue the trajectory line past the hit point by some max penetration depth
// 2. Trace BACKWARD from that extended point toward the hit point
// 3. The first hit on the backtrack trace is the exit point
// 4. Reduce velocity based on material toughness and distance traveled through the object
```

---

### 7. Lag Compensation (ProjectileLauncherComponent)

This is the multiplayer secret sauce. The pattern:

```
Client fires:
  1. Record local timestamp and initial conditions
  2. Spawn local predicted projectile immediately (feels responsive)
  3. Send RPC to server: "I fired at time T with these initial conditions"

Server receives:
  1. Note current server time vs client's claimed fire time
  2. Spawn authoritative projectile
  3. Fast-forward the projectile by (ServerTime - ClientFireTime) to catch up
  4. Authoritative projectile now simulates in real-time from here

On hit (client claims a hit):
  1. Client sends: "I hit actor X at time T at position P"
  2. Server evaluates: where was actor X at time T? (use server movement buffer)
  3. Server evaluates: where was the projectile at time T? (deterministic, just call EvaluateAtTime)
  4. If positions are close enough → confirm hit
  5. If not → reject (possible cheat or extreme lag)
```

**The key helper function on the launcher:**
```cpp
UFUNCTION(Server, Reliable)
void ServerFire(FProjectileInitialConditions Conditions, float ClientTimestamp);

UFUNCTION(Server, Reliable)
void ServerCheckClientHit(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition);
```

---

### 8. Subsystem (ProjectileBallisticsSubsystem)

A `UWorldSubsystem` for global operations:

```cpp
UCLASS()
class BALLISTICSCORERUNTIME_API UProjectileBallisticsSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // Predict a trajectory without spawning anything — for UI, aim assist, etc.
    UFUNCTION(BlueprintCallable, Category = "Ballistics")
    TArray<FVector> PredictTrajectory(
        const FProjectileInitialConditions& Conditions,
        float Duration,
        float StepSize = 0.02f
    );

    // Query all active projectiles in a radius (for proximity fuse, etc.)
    UFUNCTION(BlueprintCallable, Category = "Ballistics")
    TArray<UProjectileBallisticsComponent*> GetProjectilesInRadius(
        FVector Center, float Radius
    );

    // Debug draw all active trajectories
    UFUNCTION(BlueprintCallable, Category = "Ballistics|Debug")
    void DebugDrawActiveTrajectories(float Duration = 0.f);
};
```

---

### 9. Automation Tests Pattern

```cpp
// TrajectoryMathTests.cpp
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTrajectoryDeterminismTest,
    "BallisticsCore.Trajectory.Determinism",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryDeterminismTest::RunTest(const FString& Parameters)
{
    // Setup identical initial conditions
    FProjectileInitialConditions Conditions;
    // ... fill in ...

    // Evaluate the same trajectory twice
    FVector Pos1 = EvaluatePosition(Conditions, 2.0f, FVector::ZeroVector);
    FVector Pos2 = EvaluatePosition(Conditions, 2.0f, FVector::ZeroVector);

    // Must be BIT-IDENTICAL, not just "close"
    TestEqual("X must match", Pos1.X, Pos2.X);
    TestEqual("Y must match", Pos1.Y, Pos2.Y);
    TestEqual("Z must match", Pos1.Z, Pos2.Z);

    return true;
}

// Also test: different seeds produce different ricochet angles
// Also test: same seed always produces same ricochet angle
// Also test: trajectory with zero drag matches analytic parabola exactly
// Also test: projectile reaches terminal velocity under drag
```

---

### 10. Editor Trajectory Visualizer

Register a component visualizer in the editor module so designers can see the predicted path in-viewport when they select a launcher.

```cpp
// In your editor module's StartupModule():
if (GUnrealEd)
{
    TSharedPtr<FComponentVisualizer> Visualizer = MakeShareable(new FTrajectoryVisualizer);
    GUnrealEd->RegisterComponentVisualizer(
        UProjectileLauncherComponent::StaticClass()->GetFName(),
        Visualizer
    );
}
```

The visualizer calls `PredictTrajectory` and draws a spline in the viewport using `DrawDashedLine`.

---

## Build Order (Recommended)

1. **Types first** — `BallisticsTypes.h` with all your structs/enums/delegates
2. **Math second** — Trajectory evaluation as a standalone function (not in a component yet). Test it in isolation.
3. **Component third** — `UProjectileBallisticsComponent` using the math function. Get a bullet flying in a test map.
4. **Impact logic fourth** — Ricochet/penetrate/embed. This is where most of the tuning time goes.
5. **Launcher fifth** — `UProjectileLauncherComponent` with basic spawn + replication.
6. **Lag comp sixth** — Server fast-forward and hit validation. Test with artificial latency.
7. **Subsystem seventh** — Trajectory prediction API, debug drawing.
8. **Editor last** — Visualizer, gizmos.
9. **Tests alongside everything** — Write them as you go, not at the end.

---

## Gotchas to Watch For

- **UE units are centimeters.** Gravity is ~980 cm/s^2, not 9.8. Muzzle velocities for rifles are ~90,000 cm/s (900 m/s). These numbers look huge but they're correct.
- **Replication of initial conditions** — replicate the struct, not individual fields. Use `GetLifetimeReplicatedProps` + `DOREPLIFETIME`.
- **Don't use `GetWorld()->GetTimeSeconds()` for elapsed time in the projectile.** Accumulate your own time from DeltaTime. World time can jump (pauses, time dilation), and you want your elapsed time to be the projectile's own clock.
- **Physical materials** — UE's `UPhysicalMaterial` has a `SurfaceType` but no built-in toughness. You'll want a data asset or data table that maps surface types to `FBallisticMaterialResponse`. This keeps your system decoupled from engine internals.
- **Sweep vs line trace** — For bullets (tiny cross-section), a line trace is fine. For arrows or frag, you'll want a sphere sweep with a small radius. Make it configurable per-projectile.
