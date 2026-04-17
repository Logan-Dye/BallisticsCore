// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BallisticsTypes.h"
#include "ProjectileBallisticsSubsystem.generated.h"

class UProjectileBallisticsComponent;

/**
 * World subsystem for global ballistics operations:
 *   - Trajectory prediction (no actors spawned)
 *   - Active projectile queries
 *   - Object pooling (reduces spawn/destroy overhead)
 *   - Tick batching (better perf for high projectile counts)
 *   - Debug drawing
 */
UCLASS()
class BALLISTICSCORERUNTIME_API UProjectileBallisticsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// ─── Trajectory Prediction ───────────────────

	/**
	 * Predict a trajectory path without spawning anything.
	 * Great for UI aim lines, AI target prediction, etc.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Prediction")
	FTrajectoryPredictionResult PredictTrajectory(
		const FProjectileInitialConditions& Conditions,
		float Duration = 3.f,
		float StepSize = 0.02f
	);

	// ─── Active Projectile Registration ──────────

	void RegisterProjectile(UProjectileBallisticsComponent* Component);
	void UnregisterProjectile(UProjectileBallisticsComponent* Component);

	/** Get all active (in-flight) projectiles within a radius */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Query")
	TArray<UProjectileBallisticsComponent*> GetProjectilesInRadius(FVector Center, float Radius);

	UFUNCTION(BlueprintPure, Category = "Ballistics|Query")
	int32 GetActiveProjectileCount() const { return ActiveProjectiles.Num(); }

	// ─── Object Pooling ──────────────────────────

	/**
	 * Acquire a projectile actor from the pool. If the pool is empty, spawns a new one.
	 * The actor is activated and ready for InitializeProjectile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	AActor* AcquireProjectile(TSubclassOf<AActor> ProjectileClass, const FVector& Location, const FRotator& Rotation);

	/**
	 * Return a projectile actor to the pool instead of destroying it.
	 * The actor is deactivated and hidden until next AcquireProjectile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	void ReleaseProjectile(AActor* Projectile);

	/** Pre-warm the pool by spawning a set number of projectiles. Call during level load. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	void PreWarmPool(TSubclassOf<AActor> ProjectileClass, int32 Count);

	UFUNCTION(BlueprintPure, Category = "Ballistics|Pool")
	int32 GetPoolSize() const { return ProjectilePool.Num(); }

	// ─── Debug ───────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Debug")
	void DebugDrawActiveTrajectories(float Duration = 0.f, FLinearColor Color = FLinearColor::Red);

private:
	// ─── Active Tracking ─────────────────────────

	UPROPERTY()
	TArray<TWeakObjectPtr<UProjectileBallisticsComponent>> ActiveProjectiles;

	void PruneStaleProjectiles();

	// ─── Pooling ─────────────────────────────────

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> ProjectilePool;

	/** Deactivate an actor for pooling (hide, disable tick, disable collision) */
	void DeactivateActor(AActor* Actor);

	/** Reactivate a pooled actor */
	void ActivateActor(AActor* Actor, const FVector& Location, const FRotator& Rotation);

	// ─── Tick Batching ───────────────────────────

	/** When enabled, this subsystem ticks all projectiles instead of each component ticking itself */
	void BatchTickProjectiles(float DeltaTime);
};
