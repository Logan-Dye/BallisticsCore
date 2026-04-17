// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsTypes.generated.h"

// ─────────────────────────────────────────────
//  Enums
// ─────────────────────────────────────────────

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
	Shatter
};

// ─────────────────────────────────────────────
//  Structs
// ─────────────────────────────────────────────

/**
 * Everything needed to deterministically reproduce a projectile trajectory.
 * Replicate this struct to let clients predict locally.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileInitialConditions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	FVector Origin = FVector::ZeroVector;

	/** Normalized launch direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	FVector Direction = FVector::ForwardVector;

	/** cm/s — UE units. A rifle round is ~90000 cm/s (900 m/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0"))
	float MuzzleVelocity = 90000.f;

	/** Projectile mass in grams */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0.01"))
	float Mass = 9.5f;

	/** Projectile diameter in cm — used for drag cross-section */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0.01"))
	float Diameter = 0.9f;

	/** Drag coefficient (dimensionless). 0.3-0.5 typical for bullets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0"))
	float DragCoefficient = 0.4f;

	/** Seed for deterministic random (ricochet variance, spread, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	int32 RandomSeed = 0;

	/** World-space wind acceleration applied to this projectile (cm/s^2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	FVector WindAcceleration = FVector::ZeroVector;
};

/**
 * Snapshot of a projectile at a single point in time.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	float ElapsedTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	EProjectileState State = EProjectileState::InFlight;
};

/**
 * Per-material ballistic response. Map your physical material surface types
 * to these via a data table or data asset.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticMaterialResponse
{
	GENERATED_BODY()

	/** Resistance to penetration — arbitrary units, tune by feel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0"))
	float Toughness = 500.f;

	/** Impact angles below this → ricochet. Above → penetrate or embed. (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0", ClampMax = "90"))
	float RicochetAngleThreshold = 20.f;

	/** Energy retained on bounce (0 = dead stop, 1 = perfect bounce) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0", ClampMax = "1"))
	float CoefficientOfRestitution = 0.3f;

	/** Fallback response if no angle/energy check overrides it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	EImpactResponse DefaultResponse = EImpactResponse::Embed;
};

/**
 * Result of a trajectory prediction — a series of points along the path.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FTrajectoryPredictionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	TArray<FVector> Points;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	TArray<FProjectileSnapshot> Snapshots;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	float TotalDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics")
	float TotalTime = 0.f;
};

// ─────────────────────────────────────────────
//  Data Assets
// ─────────────────────────────────────────────

/**
 * Data asset that maps UE physical surface types to ballistic material responses.
 * Create one of these in your content folder, fill in the map, and assign it
 * to your ProjectileBallisticsComponent.
 */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticMaterialDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Map from physical surface type to ballistic response. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	TMap<TEnumAsByte<EPhysicalSurface>, FBallisticMaterialResponse> SurfaceResponseMap;

	/** Fallback response if the surface type isn't in the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	FBallisticMaterialResponse DefaultResponse;

	/** Look up the response for a surface type. Returns DefaultResponse if not found. */
	UFUNCTION(BlueprintPure, Category = "Ballistics")
	FBallisticMaterialResponse GetResponseForSurface(TEnumAsByte<EPhysicalSurface> Surface) const
	{
		if (const FBallisticMaterialResponse* Found = SurfaceResponseMap.Find(Surface))
		{
			return *Found;
		}
		return DefaultResponse;
	}
};

// ─────────────────────────────────────────────
//  Delegates
// ─────────────────────────────────────────────

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnProjectileDestroyed,
	const FProjectileSnapshot&, FinalSnapshot
);

/**
 * Delegate for server-side hit validation.
 * Bind this on the launcher to provide your game's hit confirmation logic.
 * Return true to confirm the hit, false to reject.
 */
DECLARE_DELEGATE_RetVal_ThreeParams(
	bool,
	FOnValidateHit,
	AActor*, /* HitActor */
	float,   /* ClaimTimestamp */
	FVector  /* ClaimPosition */
);

/** Broadcast after server confirms or rejects a client hit claim */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnHitValidationResult,
	AActor*, HitActor,
	FVector, HitPosition,
	bool, bConfirmed
);
