// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsDetonationComponent.generated.h"

class UBallisticsAggregateTick;
class UProjectileBallisticsComponent;
class UProjectileLauncherComponent;

/** Executes blast and shrapnel traces for an explosive actor. */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UBallisticsDetonationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBallisticsDetonationComponent();

	// Configuration

	/** The explosive config that drives this detonation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Explosive")
	TObjectPtr<UBallisticsExplosiveDataAsset> ExplosiveDataAsset;

	/** If true, the fuse starts automatically on BeginPlay. If false, call ArmFuse() manually. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Explosive",
		meta = (EditCondition = "ExplosiveDataAsset != nullptr"))
	bool bAutoArm = true;

	/** Actors to ignore during blast/shrapnel traces (e.g. the thrower) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|Explosive")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	// API

	/**
	 * Detonate the explosive at the actor's current location.
	 * Performs blast damage + shrapnel traces. Can only detonate once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Explosive")
	FDetonationResult Detonate();

	/**
	 * Detonate at a specific world location.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Explosive")
	FDetonationResult DetonateAtLocation(const FVector& Location);

	/** Start the aggregate fuse countdown (FuseTimer trigger only). */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Explosive")
	void ArmFuse();

	/** Reset the component and apply an explosive configuration. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Explosive")
	void ConfigureExplosive(UBallisticsExplosiveDataAsset* NewExplosiveDataAsset);

	/** Return the detonation state. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	bool HasDetonated() const { return bHasDetonated; }

	/** True while the fuse is active. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	bool IsFuseArmed() const { return bFuseArmed; }

	/** Get remaining fuse time (0 if not fuse-triggered or already detonated) */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Explosive")
	float GetRemainingFuseTime() const;

	// Delegates

	/** Broadcast when the explosive detonates. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Explosive")
	FOnDetonation OnDetonation;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Detonation state. */
	bool bHasDetonated = false;

	/** Whether the fuse is counting down */
	bool bFuseArmed = false;

	/** Remaining fuse time */
	float FuseTimeRemaining = 0.f;

	/** Launch position used by impact-triggered arming distance. */
	FVector ArmingOrigin = FVector::ZeroVector;

	/** Cached reference to sibling ballistics component (if any) */
	UPROPERTY()
	TWeakObjectPtr<UProjectileBallisticsComponent> CachedBallisticsComponent;

	UPROPERTY()
	TWeakObjectPtr<UProjectileLauncherComponent> CachedSourceLauncher;

	bool bCachedCosmeticRoutingEnabled = true;

	TWeakObjectPtr<UBallisticsAggregateTick> CachedAggregateTicker;

	void BindTrigger();
	void UnbindTrigger();
	void ClearFuseRegistration();
	bool AdvanceFuseFromAggregate(float DeltaTime);
	void ExpireFuseFromAggregate();

	/** Perform the blast sphere overlap and LOS checks */
	void PerformBlastDamage(const FVector& Location, FDetonationResult& OutResult);

	/** Perform shrapnel hitscan traces */
	void PerformShrapnel(const FVector& Location, FDetonationResult& OutResult);

	/** Callback for projectile impact (OnImpact trigger) */
	UFUNCTION()
	void OnProjectileImpact(const FHitResult& HitResult, const FProjectileSnapshot& Snapshot, EImpactResponse Response);

	/** Callback for projectile state change (OnRest trigger) */
	UFUNCTION()
	void OnProjectileStateChanged(EProjectileState OldState, EProjectileState NewState);

	/** Generate a detonation seed from the actor's location */
	int32 GenerateDetonationSeed(const FVector& Location) const;

	friend class UBallisticsAggregateTick;
};
