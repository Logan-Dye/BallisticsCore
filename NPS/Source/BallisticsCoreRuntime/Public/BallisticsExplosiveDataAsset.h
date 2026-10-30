// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsTypes.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsExplosiveDataAsset.generated.h"

class UBallisticMaterialDataAsset;
class UBallisticsProjectileDataAsset;

/**
 * How the explosive detonation is triggered.
 */
UENUM(BlueprintType)
enum class EDetonationTrigger : uint8
{
	/** Detonates on first impact (RPG, impact grenade) */
	OnImpact,

	/** Detonates after a fixed time from launch (cooked grenade, timed charge) */
	FuseTimer,

	/** Detonates when speed drops below threshold - projectile has stopped moving (sticky grenade, landmine) */
	OnRest,

	/** Manual detonation via code/Blueprint (remote C4, claymore, triggered IED) */
	Manual
};

/**
 * A single hit from a shrapnel trace during detonation.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FShrapnelHit
{
	GENERATED_BODY()

	/** The actor that was hit by this fragment */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	TWeakObjectPtr<AActor> HitActor;

	/** Hit result for this fragment */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	FHitResult HitResult;

	/** Direction the fragment was traveling */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	FVector FragmentDirection = FVector::ZeroVector;

	/** Distance from detonation to hit */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	float Distance = 0.f;

	/** Damage for this fragment after distance falloff */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	float Damage = 0.f;
};

/**
 * Full result of a detonation event - blast damage hits + shrapnel hits.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FDetonationResult
{
	GENERATED_BODY()

	/** World location where the detonation occurred */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	FVector DetonationLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;

	/** Actors hit by the blast radius (overlap sphere), with distance-based damage */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	TArray<FShrapnelHit> BlastHits;

	/** Shrapnel fragment hits (hitscan traces from detonation point) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	TArray<FShrapnelHit> ShrapnelHits;

	/** Total actors damaged (unique count across blast + shrapnel) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Explosive")
	int32 TotalActorsDamaged = 0;
};

/**
 * Broadcast when a detonation occurs.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnDetonation,
	const FDetonationResult&, Result
);

/** Blast, shrapnel, damage, and trigger configuration. */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsExplosiveDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// Identity

	/** Display name (e.g. "M67 Fragmentation Grenade") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Identity")
	FText DisplayName;

	// Detonation Trigger

	/** What causes this explosive to detonate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Trigger")
	EDetonationTrigger DetonationTrigger = EDetonationTrigger::FuseTimer;

	/** Fuse time in seconds (only used if DetonationTrigger == FuseTimer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Trigger",
		meta = (ClampMin = "0", EditCondition = "DetonationTrigger == EDetonationTrigger::FuseTimer"))
	float FuseTime = 3.f;

	/** Minimum travel distance before an impact-triggered explosive can detonate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Trigger",
		meta = (ClampMin = "0", EditCondition = "DetonationTrigger == EDetonationTrigger::OnImpact"))
	float ArmingDistance = 0.f;

	// Blast Damage

	/** Enable blast radius damage (sphere overlap for nearby actors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast")
	bool bEnableBlastDamage = true;

	/** Blast radius (cm). Actors within this radius take blast damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (ClampMin = "0", EditCondition = "bEnableBlastDamage"))
	float BlastRadius = 500.f;

	/** Inner radius (cm) where full blast damage is applied. Beyond this, damage falls off linearly to the outer BlastRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (ClampMin = "0", EditCondition = "bEnableBlastDamage"))
	float BlastInnerRadius = 100.f;

	/** Maximum blast damage at the center / inner radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (ClampMin = "0", EditCondition = "bEnableBlastDamage"))
	float BlastDamage = 150.f;

	/** Minimum blast damage at the outer edge of the blast radius. 0 = no damage at edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (ClampMin = "0", EditCondition = "bEnableBlastDamage"))
	float BlastMinDamage = 10.f;

	/** If true, blast checks line of sight - actors behind cover take reduced/no blast damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (EditCondition = "bEnableBlastDamage"))
	bool bBlastRequiresLineOfSight = true;

	/** Collision channel for blast line-of-sight checks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Blast",
		meta = (EditCondition = "bEnableBlastDamage && bBlastRequiresLineOfSight"))
	TEnumAsByte<ECollisionChannel> BlastTraceChannel = ECC_Visibility;

	// Shrapnel

	/** Enables shrapnel hitscan traces outward from the detonation point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel")
	bool bEnableShrapnel = true;

	/** Number of shrapnel fragments to trace. More = denser coverage, higher cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", ClampMax = "1000", EditCondition = "bEnableShrapnel"))
	int32 ShrapnelCount = 200;

	/** Max range of each shrapnel trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", EditCondition = "bEnableShrapnel"))
	float ShrapnelRange = 3000.f;

	/** Shrapnel damage at point-blank (falls off with distance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", EditCondition = "bEnableShrapnel"))
	float ShrapnelDamage = 40.f;

	/** Simulated mass of each shrapnel fragment (grams) - affects penetration through materials */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0.01", EditCondition = "bEnableShrapnel"))
	float ShrapnelMass = 1.5f;

	/** Simulated initial speed of shrapnel fragments (cm/s) for kinetic energy calculations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", EditCondition = "bEnableShrapnel"))
	float ShrapnelInitialSpeed = 50000.f;

	/** Max ricochets per shrapnel fragment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", ClampMax = "5", EditCondition = "bEnableShrapnel"))
	int32 ShrapnelMaxRicochets = 1;

	/** Max penetrations per shrapnel fragment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (ClampMin = "0", ClampMax = "5", EditCondition = "bEnableShrapnel"))
	int32 ShrapnelMaxPenetrations = 1;

	/** Collision channel for shrapnel traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (EditCondition = "bEnableShrapnel"))
	TEnumAsByte<ECollisionChannel> ShrapnelTraceChannel = ECC_Visibility;

	/** Material data asset for shrapnel surface interactions. If null, uses default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Shrapnel",
		meta = (EditCondition = "bEnableShrapnel"))
	TObjectPtr<UBallisticMaterialDataAsset> ShrapnelMaterialDataAsset;

	/** Built-in detonation audio/VFX, routed independently of projectile relevancy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosive|Cosmetics")
	FBallisticsDetonationCosmetic DetonationCosmetic;

	// API

	/** Check if this explosive has shrapnel */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	bool HasShrapnel() const { return bEnableShrapnel && ShrapnelCount > 0; }

	/** Check if this explosive has blast damage */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	bool HasBlastDamage() const { return bEnableBlastDamage && BlastRadius > 0.f; }

	/**
	 * Calculate blast damage at a given distance from detonation.
	 * Returns 0 if beyond blast radius.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	float CalculateBlastDamageAtDistance(float Distance) const;

	/**
	 * Calculate shrapnel damage at a given distance from detonation.
	 * Linear falloff from ShrapnelDamage to 0 across ShrapnelRange.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	float CalculateShrapnelDamageAtDistance(float Distance) const;

	/**
	 * Build a hitscan config for shrapnel traces from this explosive's settings.
	 */
	FHitscanConfig BuildShrapnelHitscanConfig(
		int32 Seed,
		const TArray<TObjectPtr<AActor>>& IgnoredActors) const;
};
