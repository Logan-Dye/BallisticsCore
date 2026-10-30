// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BallisticsAggregateTick.generated.h"

class UProjectileBallisticsComponent;
class UBallisticsDetonationComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBallisticsAggregateUpdate, float);

/** Batches projectile simulation and ballistics services into one world update. */
UCLASS(BlueprintType, Blueprintable)
class BALLISTICSCORERUNTIME_API UBallisticsAggregateTick : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Advance aggregate-owned services such as explosive fuses and registered
	 * lightweight schedulers. Called exactly once per world frame by the
	 * ballistics subsystem, independent of projectile batching configuration.
	 */
	void TickServices(float DeltaTime);

	/**
	 * Tick all active projectiles. Called once per frame by the subsystem
	 * when tick batching is enabled.
	 *
	 * Override this to implement custom tick strategies.
	 * The default implementation ticks every in-flight projectile with the full DeltaTime.
	 *
	 * @param ActiveProjectiles  The current set of registered projectile components.
	 * @param DeltaTime          Frame delta time in seconds.
	 */
	// TArray<TWeakObjectPtr<...>> keeps this interface C++-only.
	virtual void BatchTick(const TArray<TWeakObjectPtr<UProjectileBallisticsComponent>>& ActiveProjectiles, float DeltaTime);

	/**
	 * Called when a projectile is registered with the subsystem.
	 * Override to maintain custom data structures (spatial hash, priority queue, etc.).
	 */
	virtual void OnProjectileRegistered(UProjectileBallisticsComponent* Component);

	/**
	 * Called when a projectile is unregistered from the subsystem.
	 */
	virtual void OnProjectileUnregistered(UProjectileBallisticsComponent* Component);

	/** Register an armed fuse for the aggregate countdown pass. */
	void RegisterArmedDetonation(UBallisticsDetonationComponent* Component);

	/** Remove a fuse from the aggregate countdown pass. */
	void UnregisterArmedDetonation(UBallisticsDetonationComponent* Component);

	/** Native extension point for manager/director work that belongs on the aggregate update. */
	FOnBallisticsAggregateUpdate& OnAggregateUpdate() { return AggregateUpdate; }

	int32 GetArmedDetonationCount() const { return ArmedDetonations.Num(); }

private:
	TArray<TWeakObjectPtr<UBallisticsDetonationComponent>> ArmedDetonations;
	FOnBallisticsAggregateUpdate AggregateUpdate;
};
