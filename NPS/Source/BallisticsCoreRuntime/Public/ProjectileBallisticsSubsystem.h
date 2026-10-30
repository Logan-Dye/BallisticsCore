// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BallisticsTypes.h"
#include "ProjectileBallisticsSubsystem.generated.h"

class UProjectileBallisticsComponent;
class UBallisticsAggregateTick;
class UBallisticsImpactBudget;
class UProjectileCosmeticsManager;
class UBallisticsLaunchContributorComponent;
class APawn;
class AActor;
class AController;

/** Owns projectile registration, pooling, prediction, and aggregate updates. */
UCLASS()
class BALLISTICSCORERUNTIME_API UProjectileBallisticsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectileBallisticsSubsystem, STATGROUP_Tickables); }

	// Trajectory Prediction

	/** Predict a trajectory without spawning an actor. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Prediction")
	FTrajectoryPredictionResult PredictTrajectory(
		const FProjectileInitialConditions& Conditions,
		float Duration = 3.f,
		float StepSize = 0.02f
	);

	// Active Projectile Registration

	void RegisterProjectile(UProjectileBallisticsComponent* Component);
	void UnregisterProjectile(UProjectileBallisticsComponent* Component);

	/** Get all active (in-flight) projectiles within a radius */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Query")
	TArray<UProjectileBallisticsComponent*> GetProjectilesInRadius(FVector Center, float Radius);

	UFUNCTION(BlueprintPure, Category = "Ballistics|Query")
	int32 GetActiveProjectileCount() const { return ActiveProjectiles.Num(); }

	// Object Pooling

	/**
	 * Acquire a projectile actor from the pool. If the pool is empty, spawns a new one.
	 * Owner and instigator are assigned before fresh-spawn lifecycle callbacks or pooled activation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	AActor* AcquireProjectile(
		TSubclassOf<AActor> ProjectileClass,
		const FVector& Location,
		const FRotator& Rotation,
		AActor* NewOwner = nullptr,
		APawn* NewInstigator = nullptr);

	/**
	 * Return a projectile actor to the pool instead of destroying it.
	 * The actor is deactivated and hidden until next AcquireProjectile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	void ReleaseProjectile(AActor* Projectile);

	/** Pre-warm the pool by spawning a set number of projectiles. Call during level load. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Pool")
	void PreWarmPool(TSubclassOf<AActor> ProjectileClass, int32 Count);

	UFUNCTION(BlueprintPure, Category = "Ballistics|Pool")
	int32 GetPoolSize() const { return ProjectilePool.Num(); }

	// Aggregate Tick

	/** Get the world aggregate handler. Projectile batching can be disabled without disabling aggregate services. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Performance")
	UBallisticsAggregateTick* GetAggregateTicker() const { return AggregateTicker; }

	// Impact Budget

	/** Get the impact VFX budget manager. Can be null if impact budgeting is disabled. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|VFX")
	UBallisticsImpactBudget* GetImpactBudget() const { return ImpactBudget; }

	/** Get the cosmetic event manager. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	UProjectileCosmeticsManager* GetCosmeticsManager() const { return CosmeticsManager; }

	void RegisterLaunchContributor(UBallisticsLaunchContributorComponent* Component);
	void UnregisterLaunchContributor(UBallisticsLaunchContributorComponent* Component);

	// Projectile Flyby Listeners
	// Pawns that opt-in here get OnProjectileFlyBy delegate calls when projectiles pass close
	// without hitting them. Used for whizz / supersonic crack sounds.

	UFUNCTION(BlueprintCallable, Category = "Ballistics|FlyBy")
	void RegisterFlyByListener(APawn* Listener);

	UFUNCTION(BlueprintCallable, Category = "Ballistics|FlyBy")
	void UnregisterFlyByListener(APawn* Listener);

	/** Read-only access for projectile components iterating listeners. */
	const TSet<TWeakObjectPtr<APawn>>& GetFlyByListeners() const { return FlyByListeners; }

	// Debug

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Debug")
	void DebugDrawActiveTrajectories(float Duration = 0.f, FLinearColor Color = FLinearColor::Red);

private:
	// Active Tracking

	UPROPERTY()
	TArray<TWeakObjectPtr<UProjectileBallisticsComponent>> ActiveProjectiles;

	TArray<TWeakObjectPtr<UProjectileBallisticsComponent>>
		PendingProjectileRegistrations;
	TArray<TWeakObjectPtr<UProjectileBallisticsComponent>>
		PendingProjectileUnregistrations;
	bool bBatchTickInProgress = false;
	bool bPruneAfterBatchTick = false;

	void FlushPendingProjectileRegistrations();
	void PruneStaleProjectiles();

	// Pooling

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> ProjectilePool;

	void DeactivateActor(AActor* Actor);
	void ActivateActor(AActor* Actor, const FVector& Location, const FRotator& Rotation);

	// Aggregate Tick

	UPROPERTY()
	TObjectPtr<UBallisticsAggregateTick> AggregateTicker;

	// Impact Budget

	UPROPERTY()
	TObjectPtr<UBallisticsImpactBudget> ImpactBudget;

	UPROPERTY()
	TObjectPtr<UProjectileCosmeticsManager> CosmeticsManager;

	// Flyby Listeners

	TSet<TWeakObjectPtr<APawn>> FlyByListeners;
	TSet<TWeakObjectPtr<APawn>> ManualFlyByListeners;
	TSet<TWeakObjectPtr<APawn>> AutomaticFlyByListeners;
	TSet<TWeakObjectPtr<APawn>> WatchedFlyByPawns;
	FDelegateHandle ActorSpawnedDelegateHandle;
	float FlyByListenerVerificationAccumulator = 0.f;
	void TrackFlyByPawn(APawn* Pawn);
	void UpdateAutomaticFlyByListener(APawn* Pawn);
	void VerifyWatchedFlyByListeners();
	void HandleActorSpawned(AActor* Actor);

	UFUNCTION()
	void HandleFlyByPawnControllerChanged(
		APawn* Pawn,
		AController* OldController,
		AController* NewController);

	UFUNCTION()
	void HandleFlyByPawnDestroyed(AActor* DestroyedActor);

	TArray<TWeakObjectPtr<UBallisticsLaunchContributorComponent>> LaunchContributors;
	int32 LaunchContributorAuditCursor = 0;
	float LaunchContributorAuditWork = 0.f;
	void RefreshLaunchContributors(float DeltaTime);
};
