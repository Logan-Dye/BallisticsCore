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
 *   - Debug drawing
 */
UCLASS()
class BALLISTICSCORERUNTIME_API UProjectileBallisticsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

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

	// ─── Debug ───────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Debug")
	void DebugDrawActiveTrajectories(float Duration = 0.f, FLinearColor Color = FLinearColor::Red);

private:
	// ─── Active Tracking ─────────────────────────

	UPROPERTY()
	TArray<TWeakObjectPtr<UProjectileBallisticsComponent>> ActiveProjectiles;

	void PruneStaleProjectiles();
};
