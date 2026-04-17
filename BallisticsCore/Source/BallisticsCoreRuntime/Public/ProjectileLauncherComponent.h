// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "ProjectileLauncherComponent.generated.h"

class UProjectileBallisticsComponent;

/**
 * Weapon-side component that handles spawning and client-predicted projectiles.
 * Attach to your weapon actor.
 *
 * ClientPredicted fire flow:
 *   Client spawns local prediction → Server spawns + fast-forwards → Client reconciles if needed
 */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileLauncherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileLauncherComponent();

	// ─── Configuration ───────────────────────────

	/** The projectile actor class to spawn. Must have a UProjectileBallisticsComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Launcher")
	TSubclassOf<AActor> ProjectileClass;

	/** Base initial conditions — direction/origin get overridden at fire time */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Launcher")
	FProjectileInitialConditions BaseConditions;

	/** Max tolerable time difference for lag compensation (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Launcher", meta = (ClampMin = "0", ClampMax = "0.5"))
	float MaxLagCompensationWindow = 0.25f;

	// ─── Firing ──────────────────────────────────

	/**
	 * Fire a projectile with client prediction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Launcher")
	void Fire(const FVector& Origin, const FVector& Direction);

	// ─── Server RPCs ─────────────────────────────

	/** Client tells server it fired locally, server spawns + fast-forwards */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerNotifyFire(FProjectileInitialConditions Conditions, float ClientTimestamp);

	/** Server stub for hit validation (TODO: consumer should implement) */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCheckClientHit(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// ─── Client-Predicted Fire ───────────────────

	void Fire_ClientPredicted(const FVector& Origin, const FVector& Direction);

	// ─── Shared ──────────────────────────────────

	/** Spawn a projectile actor and initialize its ballistics component */
	AActor* SpawnProjectile(const FProjectileInitialConditions& Conditions);

	/** Generate a deterministic seed from fire counter + owner ID */
	int32 GenerateSeed();

	UPROPERTY()
	int32 FireCounter = 0;

	/** Track predicted projectiles for reconciliation */
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AActor>> PredictedProjectiles;

	/** Monotonically increasing ID for tracking projectiles across client/server */
	UPROPERTY()
	int32 NextProjectileID = 0;
};
