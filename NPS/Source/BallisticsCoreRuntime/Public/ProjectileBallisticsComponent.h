// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "ProjectileBallisticsComponent.generated.h"

class UProjectileLauncherComponent;
class UBallisticsProjectileDataAsset;

/**
 * Core projectile physics component. Attach to a projectile actor.
 * Drives deterministic trajectory simulation with collision sweeps,
 * ricochet, penetration, embedding, and sliding.
 *
 * Identical initial conditions produce identical trajectories. Randomness
 * uses the seeded FRandomStream.
 */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileBallisticsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileBallisticsComponent();

	// Lifecycle

	/** Kick off the projectile. Call once after spawn. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics")
	void InitializeProjectile(const FProjectileInitialConditions& Conditions);

	// Queries

	/** Current state without advancing time */
	UFUNCTION(BlueprintPure, Category = "Ballistics")
	FProjectileSnapshot GetCurrentSnapshot() const;

	/** Evaluate where the projectile would be at a given elapsed time (pure math, no side effects) */
	UFUNCTION(BlueprintPure, Category = "Ballistics")
	FProjectileSnapshot EvaluateAtTime(float Time) const;

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	bool IsInFlight() const
	{
		// "In flight" = anything still moving and capable of more impacts.
		// Penetrating is a flight state (projectile emerged from a surface and is still flying);
		// Sliding is too (scraping along the surface but still moving).
		// Embedded / Destroyed are terminal.
		return CurrentState == EProjectileState::InFlight
			|| CurrentState == EProjectileState::Ricocheted
			|| CurrentState == EProjectileState::Penetrating
			|| CurrentState == EProjectileState::Sliding;
	}

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	EProjectileState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	float GetElapsedTime() const { return ElapsedTime; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	int32 GetProjectileID() const { return ProjectileID; }

	/** The initial conditions this projectile was launched with. */
	const FProjectileInitialConditions& GetInitialConditions() const { return InitialConditions; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	bool IsLocallyPredicted() const { return bIsLocallyPredicted; }

	// Authority / Prediction

	/** Mark this projectile as a local prediction (not authoritative) */
	void SetIsLocallyPredicted(bool bPredicted) { bIsLocallyPredicted = bPredicted; }

	/** Assign a tracking ID for reconciliation between client and server */
	void SetProjectileID(int32 ID) { ProjectileID = ID; }

	/**
	 * Force-correct position and velocity from server reconciliation.
	 * Snaps the projectile to the server's authoritative state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics")
	void ForceCorrection(const FVector& NewPosition, const FVector& NewVelocity, float NewElapsedTime);

	/** Advance the projectile simulation by DeltaTime outside of the normal tick loop (e.g. server catch-up). */
	void FastForward(float DeltaTime) { TickComponent(DeltaTime, LEVELTICK_All, nullptr); }

	/**
	 * Reassert the configured simulation owner. Returns false after disabling an
	 * individual component Tick that conflicts with aggregate simulation.
	 */
	bool EnforceSimulationTickOwnership();

#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	bool WasSimulationRegisteredForTests() const
	{
		return bSimulationRegisteredForTests;
	}
#endif

	/** Associate the projectile with its creating launcher for automatic
	 * client-authoritative flight-log submission. */
	void SetSourceLauncher(
		UProjectileLauncherComponent* InLauncher,
		UBallisticsProjectileDataAsset* InProjectileData);

	UProjectileLauncherComponent* GetSourceLauncher() const { return SourceLauncher.Get(); }
	UBallisticsProjectileDataAsset* GetSourceProjectileData() const { return SourceProjectileData; }

	// Configuration

	/** Trace channel used for collision sweeps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Max number of ricochets before the projectile is destroyed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxRicochets = 3;

	/** Max penetration depth in cm. Traces backward from this depth to find exit point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics", meta = (ClampMin = "0"))
	float MaxPenetrationDepth = 30.f;

	/** Speed below which the projectile transitions to Sliding or Embedded */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics", meta = (ClampMin = "0"))
	float MinFlightSpeed = 500.f;

	/** Speed below which a sliding projectile comes to rest and embeds/destroys */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics", meta = (ClampMin = "0"))
	float MinSlidingSpeed = 50.f;

	/** Friction coefficient for sliding projectiles (0 = ice, 1 = sandpaper) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics", meta = (ClampMin = "0", ClampMax = "2"))
	float SlidingFriction = 0.5f;

	/** If true, projectiles can slide along surfaces after low-energy impacts. If false, they embed immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics")
	bool bEnableSliding = true;

	/**
	 * Keep a resting projectile body above the contact surface by half its configured diameter.
	 * Leave disabled for projectiles intended to embed at their origin; enable for grenades and other
	 * projectiles whose visible body should rest on the surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Physics")
	bool bRestOnSurface = false;

	/** Default material response when no mapping is found for a surface type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Materials")
	FBallisticMaterialResponse DefaultMaterialResponse;

	// Delegates

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileImpact OnImpact;

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileDestroyed OnProjectileDestroyed;

	/** Broadcast when this projectile passes within FlyByDetectionRadius of a registered listener pawn (supersonic only).
	 *  Hook on the game side to play whizz / supersonic crack 3D audio at ClosestPoint. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileFlyBy OnProjectileFlyBy;

	// Flyby Detection

	/** Distance (cm) within which a projectile's flight is considered audible flyby for a listener pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|FlyBy", meta = (ClampMin = "0"))
	float FlyByDetectionRadius = 150.f;

	/** Minimum speed (cm/s) for audible supersonic flyby. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|FlyBy", meta = (ClampMin = "0"))
	float MinFlyBySpeedCmS = 34000.f;

	// Material Response Override

	/**
	 * Optional: assign a data asset to drive per-surface material responses.
	 * If not set, DefaultMaterialResponse is used for all surfaces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Materials")
	TObjectPtr<class UBallisticMaterialDataAsset> MaterialDataAsset;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	bool bSimulationRegisteredForTests = false;
#endif

	// State

	UPROPERTY(Replicated)
	FProjectileInitialConditions InitialConditions;

	UPROPERTY()
	float ElapsedTime;

	UPROPERTY()
	EProjectileState CurrentState;

	UPROPERTY()
	FVector CurrentPosition;

	UPROPERTY()
	FVector CurrentVelocity;

	UPROPERTY()
	int32 RicochetCount;

	UPROPERTY()
	FRandomStream DeterministicRandom;

	UPROPERTY()
	int32 ProjectileID;

	UPROPERTY()
	bool bIsLocallyPredicted;

	UPROPERTY()
	TWeakObjectPtr<UProjectileLauncherComponent> SourceLauncher;

	UPROPERTY()
	TObjectPtr<UBallisticsProjectileDataAsset> SourceProjectileData;

	FProjectileFlightLog ActiveFlightLog;
	int32 NextFlightLogReportSequence = 0;
	int32 LastSubmittedFlightLogSegmentCount = 0;
	bool bFinalFlightLogSubmitted = false;
	bool bWarnedAboutExternalTick = false;

	/** Pawns this projectile has already triggered a flyby for - prevents per-step retriggering. */
	TSet<TWeakObjectPtr<APawn>> FlyByTriggered;

	/** Test the just-stepped segment against subsystem flyby listeners; launch OnProjectileFlyBy as needed. */
	void CheckFlyByListeners(const FVector& OldPosition, const FVector& NewPosition);
	void RecordFlightSegment(const FVector& Start, const FHitResult& Hit, EImpactResponse Response, float SpeedAtStart);
	void SubmitAccumulatedFlightLog(bool bFinalReport);

	float GetSurfaceRestOffset() const
	{
		return bRestOnSurface
			? FMath::Max(0.5f, InitialConditions.Diameter * 0.5f)
			: 0.5f;
	}

	/** Surface normal used while sliding. */
	UPROPERTY()
	FVector SlidingSurfaceNormal;

	// Constants

	/** Sub-step size for trajectory integration (seconds). Smaller = more accurate, more expensive. */
	static constexpr float TrajectoryStepSize = 0.005f;

	// Internal

	/** Step the trajectory forward by DeltaTime using piecewise constant-acceleration segments */
	void StepTrajectory(float DeltaTime);

	/** Compute acceleration at current velocity (gravity + drag + wind) */
	FVector ComputeAcceleration(const FVector& Velocity) const;

	/** Sweep between two positions, process any hits */
	bool ProcessSweep(const FVector& Start, const FVector& End);

	/** Resolve a collision response. */
	EImpactResponse DecideImpactResponse(const FHitResult& Hit, const FVector& Velocity) const;

	/** Look up material response for a hit surface */
	FBallisticMaterialResponse GetMaterialResponse(const FHitResult& Hit) const;

	/** Handle ricochet: reflect velocity, apply energy loss */
	void HandleRicochet(const FHitResult& Hit, const FBallisticMaterialResponse& Material);

	/** Handle penetration: find exit point, reduce velocity */
	void HandlePenetration(const FHitResult& Hit, const FBallisticMaterialResponse& Material);

	/** Handle embedding: stop the projectile, attach to hit actor */
	void HandleEmbed(const FHitResult& Hit);

	/** Handle sliding: project velocity onto surface, apply friction */
	void HandleSlide(const FHitResult& Hit, const FBallisticMaterialResponse& Material);

	/** Tick the sliding state: friction deceleration, surface following */
	void StepSliding(float DeltaTime);

	/** Transition to a new state and broadcast OnStateChanged. */
	void SetState(EProjectileState NewState);
};
