// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BallisticsTypes.h"
#include "BallisticsTrajectoryLibrary.generated.h"

/**
 * Static Blueprint function library for ballistics math.
 * These are pure functions — no world context needed, no side effects.
 * Use for UI, AI, or anywhere you need trajectory data without spawning actors.
 */
UCLASS()
class BALLISTICSCORERUNTIME_API UBallisticsTrajectoryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate projectile position at a given time from initial conditions.
	 * Pure math — accounts for gravity, drag, and wind. No collision.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Math")
	static FVector EvaluatePositionAtTime(const FProjectileInitialConditions& Conditions, float Time);

	/**
	 * Evaluate full snapshot (position + velocity) at a given time.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Math")
	static FProjectileSnapshot EvaluateSnapshotAtTime(const FProjectileInitialConditions& Conditions, float Time);

	/**
	 * Compute the acceleration vector for a projectile at a given velocity.
	 * Includes gravity, drag, and wind.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Math")
	static FVector ComputeAcceleration(const FProjectileInitialConditions& Conditions, const FVector& Velocity);

	/**
	 * Estimate time of flight to reach a target distance (straight-line).
	 * Useful for AI lead calculation.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Math")
	static float EstimateTimeToDistance(const FProjectileInitialConditions& Conditions, float Distance);

	/**
	 * Compute a launch direction to hit a target position from an origin,
	 * accounting for gravity and drag. Returns false if no solution exists.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Math")
	static bool SolveLaunchDirection(
		const FVector& Origin,
		const FVector& Target,
		const FProjectileInitialConditions& Conditions,
		FVector& OutDirection,
		float& OutTimeOfFlight
	);

private:
	static constexpr float StepSize = 0.005f;
	static constexpr float GravityZ = -980.f;
	static constexpr float AirDensity = 0.001225f;
};
