// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BallisticsTypes.h"
#include "BallisticsHitscanLibrary.generated.h"

class UBallisticsProjectileDataAsset;

/**
 * A single hit along a hitscan trace, including material response info.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FHitscanHit
{
	GENERATED_BODY()

	/** The raw UE hit result */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	FHitResult HitResult;

	/** What the material pipeline decided (ricochet, penetrate, embed, shatter) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	EImpactResponse ImpactResponse = EImpactResponse::Embed;

	/** The material response data for this surface */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	FBallisticMaterialResponse MaterialResponse;

	/** Remaining energy fraction at this hit (1.0 = full, 0.0 = spent) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	float RemainingEnergyFraction = 1.f;

	/** Which trace segment this hit belongs to (0 = initial, increments on ricochet/penetration) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	int32 SegmentIndex = 0;
};

/**
 * Full result of a hitscan trace, including all hits across ricochets and penetrations.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FHitscanResult
{
	GENERATED_BODY()

	/** All hits along the trace path, in order */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	TArray<FHitscanHit> Hits;

	/** Trace path points - start, each impact, and final end. Useful for drawing debug lines or tracers. */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	TArray<FVector> PathPoints;

	/** Total distance traveled across all segments */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	float TotalDistance = 0.f;

	/** Number of ricochets that occurred */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	int32 RicochetCount = 0;

	/** Number of penetrations that occurred */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	int32 PenetrationCount = 0;

	/** True if the trace was fully stopped (embedded, shattered, or ran out of energy) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Hitscan")
	bool bFullyStopped = false;
};

/**
 * Configuration for a hitscan trace.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FHitscanConfig
{
	GENERATED_BODY()

	/** Maximum range of the hitscan trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0"))
	float MaxRange = 100000.f;

	/** Simulated projectile mass (grams) for energy calculations against material toughness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0.01"))
	float SimulatedMass = 9.5f;

	/** Simulated initial speed (cm/s) for kinetic energy calculations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0"))
	float SimulatedInitialSpeed = 90000.f;

	/** Max ricochets allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxRicochets = 3;

	/** Max penetrations allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxPenetrations = 2;

	/** Max penetration depth per surface (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan", meta = (ClampMin = "0"))
	float MaxPenetrationDepth = 30.f;

	/** Collision channel to trace on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Material data asset for per-surface responses. If null, uses DefaultMaterialResponse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan")
	TObjectPtr<UBallisticMaterialDataAsset> MaterialDataAsset;

	/** Fallback material response when no data asset or surface mapping is found */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan")
	FBallisticMaterialResponse DefaultMaterialResponse;

	/** Actors to ignore during the trace */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|Hitscan")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	/** Seed for deterministic ricochet variance. 0 = no variance (perfect reflection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Hitscan")
	int32 RandomSeed = 0;
};

	/** Broadcast when a hitscan trace completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnHitscanComplete,
	const FHitscanResult&, Result
);

/**
 * Static function library for instant hitscan traces that run through
 * the NPS material response pipeline (ricochet, penetration, surface lookup).
 *
 * No projectile actors are spawned. The entire trace completes in a single frame.
 * Supports direct traces, shrapnel or fragmentation, and instant validation.
 */
UCLASS()
class BALLISTICSCORERUNTIME_API UBallisticsHitscanLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Perform a hitscan trace from Origin along Direction.
	 * Traces through surfaces using the material pipeline - ricochets off shallow angles,
	 * penetrates soft materials, embeds in hard ones. Returns all hits in order.
	 *
	 * @param WorldContext  World context for the trace.
	 * @param Origin       Start point of the trace.
	 * @param Direction    Normalized direction to launch.
	 * @param Config       Trace configuration (range, material asset, ricochet limits, etc.).
	 * @return Full hitscan result with all hits, path points, and metadata.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Hitscan", meta = (WorldContext = "WorldContext"))
	static FHitscanResult PerformHitscan(
		const UObject* WorldContext,
		const FVector& Origin,
		const FVector& Direction,
		const FHitscanConfig& Config
	);

	/**
	 * Perform multiple hitscan traces in a radial pattern (for shrapnel/fragmentation).
	 * Directions are generated deterministically from the given seed.
	 *
	 * @param WorldContext  World context for the traces.
	 * @param Origin       Detonation/origin point.
	 * @param TraceCount   Number of traces to launch radially.
	 * @param Config       Trace configuration applied to each trace.
	 * @return Array of results, one per trace.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Hitscan", meta = (WorldContext = "WorldContext"))
	static TArray<FHitscanResult> PerformRadialHitscan(
		const UObject* WorldContext,
		const FVector& Origin,
		int32 TraceCount,
		const FHitscanConfig& Config
	);

	/**
	 * Spawn Shatter fragments at an impact and dispatch damage to anything they hit.
	 * Fragment count scales with how much the material's Toughness exceeded the projectile's KE.
	 * Directions are a cone biased between backsplash and surface-slide by Material.ShatterSlideBias.
	 * Seed the random stream with a stable value (e.g. LaunchID) for net-deterministic dispatch.
	 *
	 * @param WorldContext       World context for the traces.
	 * @param Hit                The impact hit result (ImpactPoint + ImpactNormal used).
	 * @param IncomingVelocity   Velocity of the projectile at the moment of impact (cm/s).
	 * @param Conditions         Initial conditions of the source projectile (mass used for fragment KE).
	 * @param Material           Material response for the impacted surface (reads Shatter* fields).
	 * @param KineticEnergyJ     Projectile KE at impact, in Joules.
	 * @param InstigatorActor    The instigating pawn to attribute damage to.
	 * @param ProjectileActor    The source projectile actor (for damage context).
	 * @param RandomSeed         Deterministic seed - pass the projectile/launch ID to keep client/server in sync.
	 * @param TraceChannel       Collision channel used for fragment traces.
	 */
	static void DispatchShatterFragments(
		const UObject* WorldContext,
		const FHitResult& Hit,
		const FVector& IncomingVelocity,
		const FProjectileInitialConditions& Conditions,
		const FBallisticMaterialResponse& Material,
		float KineticEnergyJ,
		AActor* InstigatorActor,
		AActor* ProjectileActor,
		UBallisticsProjectileDataAsset* ProjectileDataAsset,
		int32 RandomSeed,
		ECollisionChannel TraceChannel
	);

private:
	/** Look up material response for a hit */
	static FBallisticMaterialResponse GetMaterialResponse(
		const FHitResult& Hit,
		const FHitscanConfig& Config
	);

	/** Decide impact response based on angle and energy */
	static EImpactResponse DecideImpactResponse(
		const FHitResult& Hit,
		const FVector& TraceDirection,
		float CurrentEnergy,
		const FBallisticMaterialResponse& Material,
		int32 RicochetCount,
		int32 MaxRicochets
	);

	/** Generate uniformly distributed directions on a sphere */
	static TArray<FVector> GenerateSphericalDirections(int32 Count, FRandomStream& Random);
};
