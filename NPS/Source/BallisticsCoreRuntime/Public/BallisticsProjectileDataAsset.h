// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "BallisticsTypes.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsProjectileDataAsset.generated.h"

class UBallisticsSpreadDataAsset;
class UBallisticMaterialDataAsset;
class UBallisticsExplosiveDataAsset;

/** Ballistic, impact, spread, explosive, and projectile cosmetic configuration. */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsProjectileDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// Identity

	/** Display name for this projectile type (e.g. "High Velocity Projectile") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Identity")
	FText DisplayName;

	/** Projectile actor class. Must contain UProjectileBallisticsComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Projectile", meta = (MetaClass = "/Script/Engine.Actor"))
	TSoftClassPtr<AActor> ProjectileClass;

	/** Short identifier for this projectile type (e.g. "high_velocity") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Identity")
	FName ProjectileID;

	// Ballistic Properties

	/** Initial speed in cm/s. Overrides BaseConditions.InitialSpeed on the launcher. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Ballistics", meta = (ClampMin = "0"))
	float InitialSpeed = 90000.f;

	/** Projectile mass in grams */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Ballistics", meta = (ClampMin = "0.01"))
	float Mass = 9.5f;

	/** Projectile diameter in cm (for drag cross-section) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Ballistics", meta = (ClampMin = "0.01"))
	float Diameter = 0.9f;

	/** Drag coefficient (dimensionless). 0.3-0.5 typical for projectiles, 0.6+ for arrows */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Ballistics", meta = (ClampMin = "0"))
	float DragCoefficient = 0.4f;

	/**
	 * Constant world-space acceleration from wind or another environmental force.
	 * Zero disables it. This becomes part of the deterministic launch conditions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Ballistics")
	FVector WindAcceleration = FVector::ZeroVector;

	// Damage Profile

	/** Base damage value passed through the damage context. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Damage")
	float BaseDamage = 25.f;

	/** Damage multiplier at maximum effective range (falloff). 1.0 = no falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float DamageFalloffMultiplier = 0.5f;

	/** Distance (cm) at which damage starts falling off */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Damage", meta = (ClampMin = "0"))
	float DamageFalloffStartDistance = 5000.f;

	/** Distance (cm) at which damage reaches the falloff multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Damage", meta = (ClampMin = "0"))
	float DamageFalloffEndDistance = 15000.f;

	// Visual

	/** Tracer color for this projectile type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visual")
	FLinearColor TracerColor = FLinearColor(1.f, 0.8f, 0.2f, 1.f);

	/** Tracer emissive intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visual", meta = (ClampMin = "0"))
	float TracerEmissiveIntensity = 5.f;

	/** Enable interval-based tracer visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visual")
	bool bUseTracerInterval = false;

	/** Use a visible tracer every N launches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visual",
		meta = (ClampMin = "1", ClampMax = "20",
			EditCondition = "bUseTracerInterval"))
	int32 TracerInterval = 5;

	// Audio

	/** Sound or MetaSound played at the closest-approach point when this projectile
	 *  passes a registered listener pawn. NPS supplies optional MetaSound inputs:
	 *  "Direction" (bool), "MissDistance" (cm), and "ProjectileSpeed" (cm/s).
	 *  Assets that do not expose those inputs continue to play normally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Audio")
	TSoftObjectPtr<USoundBase> FlyBySound;

	/** Maximum distance (cm) at which the impact decal/audio is dispatched to other players.
	 *  Used by the server when relaying impact FX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Audio", meta = (ClampMin = "0"))
	float ImpactAudibleRadius = 30000.f;

	/** Maximum distance (cm) at which impact Niagara and decals are dispatched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Impact Cosmetics", meta = (ClampMin = "0"))
	float ImpactVisualRadius = 30000.f;

	/** Per-surface impact cosmetics for this projectile type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Impact Cosmetics")
	TMap<TEnumAsByte<EPhysicalSurface>, FBallisticsImpactCosmetic> ImpactCosmetics;

	/** Used when the impacted surface has no entry in ImpactCosmetics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Impact Cosmetics")
	FBallisticsImpactCosmetic DefaultImpactCosmetic;

	/** Force the projectile fallback to override the project default even when it contains no assets or changed values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Projectile|Impact Cosmetics")
	bool bOverrideProjectDefaultImpactCosmetic = false;

	// Spread (Spread)

	/** If set, this projectile launches multiple projectiles using the spread config. Leave null for a single projectile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Spread")
	TObjectPtr<UBallisticsSpreadDataAsset> SpreadConfig;

	// Explosive

	/** Explosive configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Explosive")
	TObjectPtr<UBallisticsExplosiveDataAsset> ExplosiveConfig;

	/** Check if this projectile type is explosive */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	bool IsExplosive() const { return ExplosiveConfig != nullptr; }

	/** Resolve the built-in cosmetic response for a physical surface. */
	const FBallisticsImpactCosmetic& GetImpactCosmetic(
		TEnumAsByte<EPhysicalSurface> Surface) const
	{
		if (const FBallisticsImpactCosmetic* Found = ImpactCosmetics.Find(Surface))
		{
			return *Found;
		}
		if (bOverrideProjectDefaultImpactCosmetic || !DefaultImpactCosmetic.IsDefaultValue())
		{
			return DefaultImpactCosmetic;
		}
		return UBallisticsCoreSettings::Get()->GetImpactCosmetic(Surface);
	}

	// Surface Interactions

	/** Material data asset for this projectile type's surface interactions. Overrides the component's default if set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Materials")
	TObjectPtr<UBallisticMaterialDataAsset> MaterialOverride;

	/** Override the project-wide authority mode for this projectile type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Networking")
	bool bOverrideAuthorityMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Networking",
		meta = (EditCondition = "bOverrideAuthorityMode"))
	ELaunchAuthorityMode AuthorityModeOverride = ELaunchAuthorityMode::ClientPredicted;

	/** Launch an instant trace instead of spawning a projectile actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Hitscan")
	bool bUseHitscan = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Hitscan",
		meta = (EditCondition = "bUseHitscan"))
	FHitscanConfig HitscanConfig;

	/**
	 * Surface types that force a shatter response instead of embedding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Materials")
	TArray<TEnumAsByte<EPhysicalSurface>> EmbedBlacklist;

	// Physics Overrides

	/** Max ricochets for this projectile type. -1 = use component default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Physics")
	int32 MaxRicochetsOverride = -1;

	/** Max penetration depth (cm) for this projectile type. -1 = use component default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Physics")
	float MaxPenetrationDepthOverride = -1.f;

	/** Optional sliding behavior for this projectile type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Physics")
	FSlidingOverride SlidingOverride;

	// API

	/** Resolve the configured projectile class, or null when no class is configured. */
	UClass* ResolveProjectileClass() const
	{
		return ProjectileClass.IsNull() ? nullptr : ProjectileClass.LoadSynchronous();
	}

	/** Build the default initial conditions for one launch. */
	FProjectileInitialConditions BuildInitialConditions() const
	{
		return ApplyToConditions(FProjectileInitialConditions());
	}

	/**
	 * Apply this projectile's properties to a set of initial conditions.
	 * Called by the launcher before launching.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Projectile")
	FProjectileInitialConditions ApplyToConditions(const FProjectileInitialConditions& BaseConditions) const;

	/**
	 * Calculate damage for a hit at the given distance, applying falloff.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	float CalculateDamageAtDistance(float Distance) const;

	/**
	 * Check if this projectile type is a spread (has a spread config).
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	bool UsesSpread() const { return SpreadConfig != nullptr; }

	/**
	 * Check if the given surface type is in the embed blacklist.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	bool IsSurfaceBlacklisted(TEnumAsByte<EPhysicalSurface> Surface) const;

	/**
	 * Check tracer visibility for the launch counter.
	 */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	bool ShouldShowTracer(int32 LaunchCounter) const;
};
