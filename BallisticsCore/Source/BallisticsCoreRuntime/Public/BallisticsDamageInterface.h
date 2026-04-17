// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "BallisticsTypes.h"
#include "BallisticsDamageInterface.generated.h"

/**
 * Damage context passed from the ballistics system to your game's damage pipeline.
 * Contains everything you need to calculate and apply damage.
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
	EImpactResponse ImpactResponse;

	/** Kinetic energy at impact: 0.5 * mass * speed^2 (g * cm^2/s^2) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float KineticEnergy = 0.f;

	/** Distance from origin to impact point (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float DistanceTraveled = 0.f;

	/** If penetration occurred, how deep did the projectile go (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	float PenetrationDepth = 0.f;

	/** The actor that owns the projectile (usually the weapon/pawn that fired) */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	TObjectPtr<AActor> Instigator;

	/** The projectile actor itself */
	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Damage")
	TObjectPtr<AActor> ProjectileActor;
};

/**
 * Interface for actors that can receive ballistic damage.
 * Implement this on your characters, destructibles, vehicles, etc.
 *
 * Usage:
 *   // In your character class header:
 *   UCLASS()
 *   class AMyCharacter : public ACharacter, public IBallisticsDamageInterface
 *   {
 *       GENERATED_BODY()
 *       virtual void OnBallisticDamage_Implementation(const FBallisticDamageContext& Context) override;
 *   };
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
	 * Called when this actor is hit by a ballistic projectile.
	 * Implement your damage calculation and application here.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ballistics|Damage")
	void OnBallisticDamage(const FBallisticDamageContext& Context);
};
