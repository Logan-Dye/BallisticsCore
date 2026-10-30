// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "BallisticsTypes.h"
#include "BallisticsDamageInterface.generated.h"

class UBallisticsProjectileDataAsset;

/**
 * Damage context passed to project damage handling.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticDamageContext
{
	GENERATED_BODY()

	/** The hit result from the collision trace */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	FHitResult HitResult;

	/** Projectile snapshot at the moment of impact */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	FProjectileSnapshot ImpactSnapshot;

	/** Initial conditions of the projectile (for computing total energy, distance traveled, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	FProjectileInitialConditions InitialConditions;

	/** What the projectile did on impact */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	EImpactResponse ImpactResponse = EImpactResponse::Embed;

	/** Kinetic energy at impact, in Joules. */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float KineticEnergy = 0.f;

	/** True if this hit is a secondary fragment from a Shatter event (e.g. spall off an armor plate).
	 *  Victim damage code typically multiplies base damage down for fragment hits. */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	bool bIsFragment = false;

	/** Distance from origin to impact point (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float DistanceTraveled = 0.f;

	/** If penetration occurred, how deep did the projectile go (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float PenetrationDepth = 0.f;

	/** The actor that owns the projectile source. */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	TObjectPtr<AActor> Instigator;

	/** The projectile actor itself */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	TObjectPtr<AActor> ProjectileActor;

	/** Projectile definition used for this damage event. */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;
};

/**
 * Interface for ballistic damage receivers.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UBallisticsDamageInterface : public UInterface
{
	GENERATED_BODY()
};

class BALLISTICSCORERUNTIME_API IBallisticsDamageInterface
{
	GENERATED_BODY()

public:
	/**
	 * Called when a ballistic projectile hits the actor.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ballistics|Damage")
	void OnBallisticDamage(const FBallisticDamageContext& Context);
};
