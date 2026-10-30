// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsAggregateTick.h"
#include "BallisticsDetonationComponent.h"
#include "ProjectileBallisticsComponent.h"

void UBallisticsAggregateTick::TickServices(float DeltaTime)
{
	TArray<TWeakObjectPtr<UBallisticsDetonationComponent>, TInlineAllocator<16>> Expired;
	for (int32 Index = ArmedDetonations.Num() - 1; Index >= 0; --Index)
	{
		UBallisticsDetonationComponent* Component = ArmedDetonations[Index].Get();
		if (!Component || !Component->IsFuseArmed())
		{
			ArmedDetonations.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		if (Component->AdvanceFuseFromAggregate(DeltaTime))
		{
			Expired.Add(Component);
			ArmedDetonations.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	for (const TWeakObjectPtr<UBallisticsDetonationComponent>& Weak : Expired)
	{
		if (UBallisticsDetonationComponent* Component = Weak.Get())
		{
			Component->ExpireFuseFromAggregate();
		}
	}

	AggregateUpdate.Broadcast(DeltaTime);
}

void UBallisticsAggregateTick::BatchTick(const TArray<TWeakObjectPtr<UProjectileBallisticsComponent>>& ActiveProjectiles, float DeltaTime)
{
	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak : ActiveProjectiles)
	{
		if (UProjectileBallisticsComponent* Comp = Weak.Get())
		{
			if (Comp->IsInFlight())
			{
				Comp->FastForward(DeltaTime);
			}
		}
	}
}

void UBallisticsAggregateTick::OnProjectileRegistered(UProjectileBallisticsComponent* Component)
{
}

void UBallisticsAggregateTick::OnProjectileUnregistered(UProjectileBallisticsComponent* Component)
{
}

void UBallisticsAggregateTick::RegisterArmedDetonation(
	UBallisticsDetonationComponent* Component)
{
	if (Component)
	{
		ArmedDetonations.AddUnique(Component);
	}
}

void UBallisticsAggregateTick::UnregisterArmedDetonation(
	UBallisticsDetonationComponent* Component)
{
	ArmedDetonations.RemoveAll(
		[Component](const TWeakObjectPtr<UBallisticsDetonationComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Component;
		});
}
