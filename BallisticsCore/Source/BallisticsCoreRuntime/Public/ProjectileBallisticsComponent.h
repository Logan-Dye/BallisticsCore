// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "ProjectileBallisticsComponent.generated.h"

/**
 * Core projectile physics component. Attach to a projectile actor.
 * Drives deterministic trajectory simulation with collision sweeps,
 * ricochet, penetration, embedding, and sliding.
 *
 * Determinism: given identical FProjectileInitialConditions, this component
 * will produce bit-identical trajectories across calls. All randomness is
 * sourced from the seeded FRandomStream — never from world RNG.
 */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileBallisticsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileBallisticsComponent();

	// ─── Lifecycle ───────────────────────────────

	/** Kick off the projectile. Call once after spawn. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics")
	void InitializeProjectile(const FProjectileInitialConditions& Conditions);

	// ─── Queries ─────────────────────────────────

	/** Current state without advancing time */
	UFUNCTION(BlueprintPure, Category = "Ballistics")
	FProjectileSnapshot GetCurrentSnapshot() const;

	/** Evaluate where the projectile would be at a given elapsed time (pure math, no side effects) */
	UFUNCTION(BlueprintPure, Category = "Ballistics")
	FProjectileSnapshot EvaluateAtTime(float Time) const;

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	bool IsInFlight() const { return CurrentState == EProjectileState::InFlight || CurrentState == EProjectileState::Ricocheted; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	EProjectileState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	float GetElapsedTime() const { return ElapsedTime; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	int32 GetProjectileID() const { return ProjectileID; }

	UFUNCTION(BlueprintPure, Category = "Ballistics")
	bool IsLocallyPredicted() const { return bIsLocallyPredicted; }

	// ─── Authority / Prediction ──────────────────

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

	// ─── Configuration ───────────────────────────

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

	/** Default material response when no mapping is found for a surface type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Materials")
	FBallisticMaterialResponse DefaultMaterialResponse;

	// ─── Delegates ───────────────────────────────

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileImpact OnImpact;

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileDestroyed OnProjectileDestroyed;

	// ─── Material Response Override ──────────────

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

private:
	// ─── State ───────────────────────────────────

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

	/** Surface normal of the surface we're sliding on */
	UPROPERTY()
	FVector SlidingSurfaceNormal;

	// ─── Internal ────────────────────────────────

	/** Step the trajectory forward by DeltaTime using piecewise constant-acceleration segments */
	void StepTrajectory(float DeltaTime);

	/** Compute acceleration at current velocity (gravity + drag + wind) */
	FVector ComputeAcceleration(const FVector& Velocity) const;

	/** Sweep between two positions, process any hits */
	bool ProcessSweep(const FVector& Start, const FVector& End);

	/** Decide what happens when we hit something */
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

	/** Transition to a new state, fire delegate */
	void SetState(EProjectileState NewState);

	// ─── Constants ───────────────────────────────

	static constexpr float TrajectoryStepSize = 0.005f; // 5ms sub-steps for drag accuracy
};
