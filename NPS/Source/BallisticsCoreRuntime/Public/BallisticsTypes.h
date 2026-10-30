// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "BallisticsTypes.generated.h"

// Converts raw KE (mass in grams x speed^2 in cm^2/s^2) to Joules.
// 1 g*cm^2/s^2 = 1e-7 J. Toughness values are authored in Joules.
static constexpr float KE_RAW_TO_JOULES = 1.0e-7f;
// Enums

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
	Shatter,
	/** Grazing impact that slides along the surface until friction stops it. */
	Slide,
	/** No impact - segment continued in flight (used in flight log segments) */
	None
};
// Structs

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

	/** Initial launch speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0"))
	float InitialSpeed = 90000.f;

	/** Projectile mass in grams */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0.01"))
	float Mass = 9.5f;

	/** Projectile diameter in cm - used for drag cross-section */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0.01"))
	float Diameter = 0.9f;

	/** Drag coefficient (dimensionless). 0.3-0.5 typical for projectiles */
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
 * Ballistic response for a physical surface type.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticMaterialResponse
{
	GENERATED_BODY()

	/** Resistance to penetration, in Joules. Projectile penetrates if its kinetic energy exceeds this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0"))
	float Toughness = 2000.f;

	/** Impact angles below this -> ricochet. Above -> penetrate or embed. (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0", ClampMax = "90"))
	float RicochetAngleThreshold = 20.f;

	/** Energy retained on bounce (0 = dead stop, 1 = perfect bounce) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics", meta = (ClampMin = "0", ClampMax = "1"))
	float CoefficientOfRestitution = 0.3f;

	/** Fallback response if no angle/energy check overrides it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	EImpactResponse DefaultResponse = EImpactResponse::Embed;

	// Shatter / Fragmentation (only used when DefaultResponse or the resolved response is Shatter)

	/** Minimum fragment count when the projectile barely failed to penetrate (KE close to Toughness). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0"))
	int32 ShatterMinFragments = 2;

	/** Maximum fragment count when the projectile was vastly underpowered (severity -> 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0"))
	int32 ShatterMaxFragments = 8;

	/** Fraction of incoming speed each fragment carries (0.4 = fragments fly at 40% of projectile speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0", ClampMax = "1"))
	float ShatterSpeedFraction = 0.4f;

	/** Total cone spread (degrees) for the fragment dispersion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0", ClampMax = "180"))
	float ShatterConeAngleDeg = 60.f;

	/** Cone base direction: 0 = pure backsplash toward shooter, 1 = pure slide along surface.
	 *  Higher values model spall that scrapes along the armor (e.g. up the plate into the neck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0", ClampMax = "1"))
	float ShatterSlideBias = 0.7f;

	/** Maximum trace range for each fragment (cm). Fragments attenuate fast - 200 cm is typical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0"))
	float ShatterMaxRange = 200.f;

	/** Mass share (of original projectile) each fragment represents. Lower = smaller fragments = less damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Shatter", meta = (ClampMin = "0", ClampMax = "1"))
	float ShatterFragmentMassFraction = 0.1f;
};

/**
 * Result of a trajectory prediction - a series of points along the path.
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
// Client Authoritative Flight Logging

/**
 * A single segment of a client-authoritative launch path.
 * Each segment represents a straight-line trace between two points,
 * with what happened at the end (ricochet, penetration, hit, etc.).
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileFlightSegment
{
	GENERATED_BODY()

	/** Start of this segment (world space) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	FVector StartLocation = FVector::ZeroVector;

	/** End of this segment (world space) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	FVector EndLocation = FVector::ZeroVector;

	/** What happened at the end of this segment */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	EImpactResponse ImpactResponse = EImpactResponse::None;

	/** Physical surface type hit at the end of this segment (for penetrability/ricochet validation) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	TEnumAsByte<EPhysicalSurface> SurfaceType = EPhysicalSurface::SurfaceType_Default;

	/** Impact normal at the end of this segment (used for ricochet angle validation) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	FVector ImpactNormal = FVector::UpVector;

	/** Actor hit at the end of this segment (nullptr if hit static world geometry) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	TWeakObjectPtr<AActor> HitActor;

	/** Bone name if hit a skeletal mesh */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	FName HitBoneName;

	/** Projectile speed at this segment start (cm/s) - used for energy checks */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	float SpeedAtStart = 0.f;

	/** Projectile flight time when this impact was recorded. */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	float TimeAtImpact = 0.f;
};

/**
 * A cumulative flight report sent from client to server in Client Authoritative mode.
 * Reports preserve the complete accepted prefix so the server can validate continuity
 * while dispatching newly reported impacts exactly once.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileFlightLog
{
	GENERATED_BODY()

	/** Unique ID for this launch (matches client-side projectile ID) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	int32 LaunchID = -1;

	/** Client timestamp when the launch was launched (world seconds) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	float ClientTimestamp = 0.f;

	/** The initial conditions of the projectile */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	FProjectileInitialConditions InitialConditions;

	/** Monotonic report number for this launch. */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	int32 ReportSequence = 0;

	/** True when no later report will be sent for this launch. */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	bool bFinalReport = true;

	/** All segments recorded so far, from launch through the latest impact. */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics")
	TArray<FProjectileFlightSegment> Segments;
};

/**
 * Result of a server validation of a client-authoritative launch segment.
 */
UENUM(BlueprintType)
enum class EProjectileFlightValidationResult : uint8
{
	/** Launch validated - all segments passed */
	Valid,
	/** Launch too old - exceeded max launch age */
	Expired,
	/** A segment failed visibility (obstruction between start and end) */
	VisibilityFailed,
	/** Ricochet angle mismatch with the surface normal. */
	InvalidRicochetAngle,
	/** Material at impact point is not penetrable but client claims penetration */
	InvalidPenetration,
	/** Hit actor not found or not at claimed location */
	InvalidTarget,
	/** Flight log buffer full and this launch was dropped. */
	BufferFull,
	/** No server-side launch snapshot exists for this client report. */
	UnknownLaunch,
	/** Client launch conditions do not match the server-owned launch snapshot. */
	InvalidInitialConditions,
	/** Timestamp is non-finite, too far in the future, or otherwise invalid. */
	InvalidTimestamp,
	/** A segment contains malformed or disconnected data. */
	InvalidSegment,
	/** Reported timing, position, or speed is outside the configured physical tolerance. */
	PhysicallyImplausible
};
// Data Assets

/**
 * Per-launcher override for a spawned projectile's sliding behavior.
 * If bOverride is true, the launcher writes these values into the projectile's
 * ballistics component at spawn time. Leave bOverride false to inherit from
 * project-wide defaults (see UBallisticsCoreSettings).
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FSlidingOverride
{
	GENERATED_BODY()

	/** If true, apply the override values below to projectiles spawned by this launcher. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding")
	bool bOverride = false;

	/** Enable sliding for spawned projectiles. Typically false for arrows/bolts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding",
		meta = (EditCondition = "bOverride"))
	bool bEnableSliding = true;

	/** Min speed (cm/s) required for sliding to trigger instead of instant embed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding",
		meta = (EditCondition = "bOverride", ClampMin = "0"))
	float MinSlidingSpeed = 50.f;

	/** Friction for sliding projectiles (0 = ice, 1 = sandpaper). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding",
		meta = (EditCondition = "bOverride", ClampMin = "0", ClampMax = "2"))
	float SlidingFriction = 0.5f;
};

/**
 * Maps UE physical surface types to ballistic material responses.
 */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticMaterialDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Map from physical surface type to ballistic response. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	TMap<TEnumAsByte<EPhysicalSurface>, FBallisticMaterialResponse> SurfaceResponseMap;

	/** Fallback response for unmapped surface types. */
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
// Delegates

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

/** A projectile passed close to a registered listener pawn without hitting it. Use for whizz / supersonic crack. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnProjectileFlyBy,
	APawn*, NearbyPawn,
	FVector, ClosestPoint,
	float, MissDistance,
	float, ProjectileSpeed
);

/**
 * Delegate for server-side hit validation.
 * Launcher callback for hit confirmation logic.
 * Return true to confirm the hit, false to reject.
 */
DECLARE_DELEGATE_RetVal_ThreeParams(
	bool,
	FOnValidateHit,
	AActor*, /* HitActor */
	float,   /* ClaimTimestamp */
	FVector  /* ClaimPosition */
);

/**
 * Broadcast after server validates or rejects a client hit claim (Client Predicted mode).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnHitValidationResult,
	AActor*, HitActor,
	FVector, ClaimPosition,
	bool, bConfirmed
);

/**
 * Broadcast after server validates a client-authoritative flight log.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnProjectileFlightLogValidated,
	const FProjectileFlightLog&, FlightLog,
	EProjectileFlightValidationResult, Result,
	int32, FailedSegmentIndex
);
