// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BallisticsSpreadDataAsset.generated.h"

/**
 * Defines deterministic distribution for a multi-projectile launch.
 * Supports random cones, fixed directional patterns, and hybrid patterns.
 */

UENUM(BlueprintType)
enum class ESpreadMode : uint8
{
	/** Random distribution within a cone. */
	RandomCone,

	/** Fixed directional offsets. */
	FixedPattern,

	/** Fixed pattern with random variance. */
	HybridPattern
};

/**
 * A single point in a fixed spread pattern.
 * Offsets are relative to the forward direction in degrees.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FSpreadPatternPoint
{
	GENERATED_BODY()

	/** Horizontal offset from center (degrees). Positive = right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float YawOffset = 0.f;

	/** Vertical offset from center (degrees). Positive = up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float PitchOffset = 0.f;
};

/**
 * Data asset defining multi-projectile spread behavior.
 * Assigned to projectile configurations that launch multiple projectiles.
 */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsSpreadDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// Core Settings

	/** How projectiles are distributed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	ESpreadMode SpreadMode = ESpreadMode::RandomCone;

	/** Number of projectiles per launch. RandomCone only; fixed modes use the pattern length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread",
		meta = (ClampMin = "1", ClampMax = "64",
			EditCondition = "SpreadMode == ESpreadMode::RandomCone"))
	int32 ProjectileCount = 8;

	// Random Cone Settings

	/** Half-angle of the spread cone (degrees). Larger = wider spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Random Cone",
		meta = (ClampMin = "0", ClampMax = "45",
			EditCondition = "SpreadMode == ESpreadMode::RandomCone || SpreadMode == ESpreadMode::HybridPattern"))
	float ConeHalfAngle = 5.f;

	// Fixed Pattern Settings

	/**
	 * Array of directional offsets defining the spread pattern.
	 * Each entry is one projectile. Used in FixedPattern and HybridPattern modes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Fixed Pattern",
		meta = (EditCondition = "SpreadMode == ESpreadMode::FixedPattern || SpreadMode == ESpreadMode::HybridPattern"))
	TArray<FSpreadPatternPoint> Pattern;

	// Hybrid Settings

	/**
	 * Random variance applied on top of fixed pattern points (degrees).
	 * Each projectile's fixed offset gets a random nudge within this range.
	 * Only used in HybridPattern mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Hybrid",
		meta = (ClampMin = "0", ClampMax = "15",
			EditCondition = "SpreadMode == ESpreadMode::HybridPattern"))
	float PatternVarianceDegrees = 1.5f;

	// Per-Projectile Variation

	/** If true, each projectile gets a slight random initial speed variation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Variation")
	bool bVaryInitialSpeed = false;

	/** Max initial speed variation as a percentage of InitialSpeed (0.05 = +/- 5%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Variation",
		meta = (ClampMin = "0", ClampMax = "0.2",
			EditCondition = "bVaryInitialSpeed"))
	float InitialSpeedVariationPercent = 0.03f;

	/** If true, each projectile gets a slight mass variation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Variation")
	bool bVaryProjectileMass = false;

	/** Max mass variation as a percentage (0.05 = +/- 5%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Variation",
		meta = (ClampMin = "0", ClampMax = "0.2",
			EditCondition = "bVaryProjectileMass"))
	float MassVariationPercent = 0.02f;

	// API

	/**
	 * Generate projectile directions from this spread config.
	 * All directions are deterministic for the given random stream.
	 *
	 * @param ForwardDirection  The normalized launch direction.
	 * @param RandomStream      Seeded random stream for deterministic spread.
	 * @param OutDirections     Output array of projectile directions (one per projectile).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Spread")
	void GenerateProjectileDirections(
		const FVector& ForwardDirection,
		UPARAM(ref) FRandomStream& RandomStream,
		TArray<FVector>& OutDirections
	) const;

	/**
	 * Generate full per-projectile initial conditions including velocity/mass variations.
	 *
	 * @param BaseConditions    The base projectile conditions (from projectile/launcher).
	 * @param LaunchDirection   The normalized launch direction.
	 * @param RandomStream      Seeded random stream.
	 * @param OutConditionsArray Output array of per-projectile initial conditions.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Spread")
	void GenerateProjectileConditions(
		const FProjectileInitialConditions& BaseConditions,
		const FVector& LaunchDirection,
		UPARAM(ref) FRandomStream& RandomStream,
		TArray<FProjectileInitialConditions>& OutConditionsArray
	) const;

private:
	/** Generate a random direction within a cone */
	FVector RandomDirectionInCone(const FVector& Forward, float HalfAngleDeg, FRandomStream& Stream) const;

	/** Apply a fixed pattern point as a rotation offset to a direction */
	FVector ApplyPatternOffset(const FVector& Forward, const FSpreadPatternPoint& Point) const;
};
