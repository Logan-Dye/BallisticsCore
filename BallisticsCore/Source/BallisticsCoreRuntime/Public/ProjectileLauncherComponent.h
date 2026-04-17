// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "BallisticsCoreSettings.h"
#include "ProjectileLauncherComponent.generated.h"

class UProjectileBallisticsComponent;

/**
 * Weapon-side component that handles spawning, replicating, and
 * authority management for projectiles. Attach to your weapon actor.
 *
 * Supports two authority modes (set globally in Project Settings or overridden per-launcher):
 *
 *   ServerAuthoritative:
 *     Client sends fire request → Server spawns + simulates → Clients see replicated result.
 *     Best for: rockets, grenades, arrows, anything where a frame of latency is acceptable.
 *
 *   ClientPredicted:
 *     Client spawns local prediction → Server spawns + fast-forwards → Server validates hits.
 *     Client reconciles if server disagrees. Best for: bullets, hitscan-feeling weapons.
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

	/** Override the global authority mode for this specific launcher. If not set, uses the global setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Authority")
	bool bOverrideAuthorityMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Authority", meta = (EditCondition = "bOverrideAuthorityMode"))
	EFireAuthorityMode AuthorityModeOverride;

	/** Max tolerable time difference for lag compensation (seconds). Only used in ClientPredicted mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Authority", meta = (ClampMin = "0", ClampMax = "0.5"))
	float MaxLagCompensationWindow;

	/** Max distance tolerance when validating client hit claims (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Authority", meta = (ClampMin = "0"))
	float HitValidationTolerance;

	// ─── Firing ──────────────────────────────────

	/**
	 * Fire a projectile. Automatically routes through the correct authority path
	 * based on the active mode (global setting or per-launcher override).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Launcher")
	void Fire(const FVector& Origin, const FVector& Direction);

	/** Returns the authority mode this launcher is currently using */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Launcher")
	EFireAuthorityMode GetActiveAuthorityMode() const;

	// ─── Server RPCs ─────────────────────────────

	/** ServerAuthoritative: client requests the server to fire */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestFire(FProjectileInitialConditions Conditions);

	/** ClientPredicted: client tells server it fired locally, server spawns + fast-forwards */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerNotifyFire(FProjectileInitialConditions Conditions, float ClientTimestamp);

	/** ClientPredicted: client claims a hit, server validates */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCheckClientHit(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition);

	// ─── Client RPCs ─────────────────────────────

	/** Server tells client to reconcile — the predicted projectile was too far off */
	UFUNCTION(Client, Reliable)
	void ClientReconcileProjectile(int32 ProjectileID, FVector CorrectedPosition, FVector CorrectedVelocity, float ServerElapsedTime);

	/** Server tells all clients that a projectile was spawned (for ServerAuthoritative visual sync) */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOnProjectileFired(FProjectileInitialConditions Conditions);

	// ─── Delegates ───────────────────────────────

	/**
	 * Bind on server to provide your game's hit validation logic.
	 * Return true to confirm the hit, false to reject.
	 */
	FOnValidateHit OnValidateHit;

	/** Fires after server validates or rejects a hit claim */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnHitValidationResult OnHitValidationResult;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// ─── Server-Authoritative Flow ───────────────

	void Fire_ServerAuthoritative(const FVector& Origin, const FVector& Direction);

	// ─── Client-Predicted Flow ───────────────────

	void Fire_ClientPredicted(const FVector& Origin, const FVector& Direction);

	// ─── Shared ──────────────────────────────────

	/** Spawn a projectile actor and initialize its ballistics component */
	AActor* SpawnProjectile(const FProjectileInitialConditions& Conditions);

	/** Generate a deterministic seed from fire counter + owner ID */
	int32 GenerateSeed();

	UPROPERTY()
	int32 FireCounter;

	/** Track predicted projectiles for reconciliation (ClientPredicted mode) */
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AActor>> PredictedProjectiles;

	/** Monotonically increasing ID for tracking projectiles across client/server */
	UPROPERTY()
	int32 NextProjectileID;
};
