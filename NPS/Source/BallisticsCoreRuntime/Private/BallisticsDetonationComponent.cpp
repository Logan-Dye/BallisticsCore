// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsDetonationComponent.h"
#include "BallisticsAggregateTick.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsDebugDraw.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "ProjectileLauncherComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

UBallisticsDetonationComponent::UBallisticsDetonationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBallisticsDetonationComponent::BeginPlay()
{
	Super::BeginPlay();
	ConfigureExplosive(ExplosiveDataAsset);
}

void UBallisticsDetonationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearFuseRegistration();
	UnbindTrigger();
	Super::EndPlay(EndPlayReason);
}

void UBallisticsDetonationComponent::ConfigureExplosive(
	UBallisticsExplosiveDataAsset* NewExplosiveDataAsset)
{
	UnbindTrigger();
	ClearFuseRegistration();
	bHasDetonated = false;
	bFuseArmed = false;
	FuseTimeRemaining = 0.f;
	IgnoredActors.Reset();
	ExplosiveDataAsset = NewExplosiveDataAsset;
	ArmingOrigin = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	CachedSourceLauncher.Reset();
	bCachedCosmeticRoutingEnabled = true;

	if (!ExplosiveDataAsset || !IsRegistered())
	{
		return;
	}

	BindTrigger();
	if (UProjectileBallisticsComponent* Ballistics = CachedBallisticsComponent.Get())
	{
		if (UProjectileLauncherComponent* Launcher = Ballistics->GetSourceLauncher())
		{
			CachedSourceLauncher = Launcher;
			bCachedCosmeticRoutingEnabled = Launcher->bEnableCosmeticRouting;
		}
	}

	if (bAutoArm && ExplosiveDataAsset->DetonationTrigger == EDetonationTrigger::FuseTimer)
	{
		ArmFuse();
	}
}

void UBallisticsDetonationComponent::BindTrigger()
{
	AActor* Owner = GetOwner();
	if (!Owner || !ExplosiveDataAsset)
	{
		return;
	}

	UProjectileBallisticsComponent* BallisticsComp =
		Owner->FindComponentByClass<UProjectileBallisticsComponent>();
	if (!BallisticsComp)
	{
		return;
	}

	CachedBallisticsComponent = BallisticsComp;
	switch (ExplosiveDataAsset->DetonationTrigger)
	{
	case EDetonationTrigger::OnImpact:
		BallisticsComp->OnImpact.AddUniqueDynamic(
			this, &UBallisticsDetonationComponent::OnProjectileImpact);
		break;

	case EDetonationTrigger::OnRest:
		BallisticsComp->OnStateChanged.AddUniqueDynamic(
			this, &UBallisticsDetonationComponent::OnProjectileStateChanged);
		break;

	default:
		break;
	}
}

void UBallisticsDetonationComponent::UnbindTrigger()
{
	if (UProjectileBallisticsComponent* BallisticsComp = CachedBallisticsComponent.Get())
	{
		BallisticsComp->OnImpact.RemoveDynamic(
			this, &UBallisticsDetonationComponent::OnProjectileImpact);
		BallisticsComp->OnStateChanged.RemoveDynamic(
			this, &UBallisticsDetonationComponent::OnProjectileStateChanged);
	}
	CachedBallisticsComponent.Reset();
}

void UBallisticsDetonationComponent::ClearFuseRegistration()
{
	if (UBallisticsAggregateTick* AggregateTicker = CachedAggregateTicker.Get())
	{
		AggregateTicker->UnregisterArmedDetonation(this);
	}
	CachedAggregateTicker.Reset();
}

bool UBallisticsDetonationComponent::AdvanceFuseFromAggregate(float DeltaTime)
{
	if (!bFuseArmed || bHasDetonated)
	{
		return false;
	}

	FuseTimeRemaining = FMath::Max(
		0.f,
		FuseTimeRemaining - FMath::Max(DeltaTime, 0.f));
	return FuseTimeRemaining <= 0.f;
}

void UBallisticsDetonationComponent::ExpireFuseFromAggregate()
{
	if (!bFuseArmed || bHasDetonated)
	{
		return;
	}

	FuseTimeRemaining = 0.f;
	bFuseArmed = false;
	CachedAggregateTicker.Reset();
	Detonate();
}

FDetonationResult UBallisticsDetonationComponent::Detonate()
{
	const FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	return DetonateAtLocation(Location);
}

FDetonationResult UBallisticsDetonationComponent::DetonateAtLocation(const FVector& Location)
{
	FDetonationResult Result;
	Result.DetonationLocation = Location;
	if (const UProjectileBallisticsComponent* Ballistics = CachedBallisticsComponent.Get())
	{
		Result.ProjectileDataAsset = Ballistics->GetSourceProjectileData();
	}

	if (bHasDetonated || !ExplosiveDataAsset)
	{
		return Result;
	}

	bHasDetonated = true;
	bFuseArmed = false;
	FuseTimeRemaining = 0.f;
	ClearFuseRegistration();

	// Exclude the explosive actor from its own traces.
	if (AActor* Owner = GetOwner())
	{
		IgnoredActors.AddUnique(Owner);
	}

	// 1. Blast damage (sphere overlap + LOS)
	if (ExplosiveDataAsset->HasBlastDamage())
	{
		PerformBlastDamage(Location, Result);
	}

	// 2. Shrapnel traces
	if (ExplosiveDataAsset->HasShrapnel())
	{
		PerformShrapnel(Location, Result);
	}

	// Count unique actors damaged
	TSet<AActor*> UniqueActors;
	for (const FShrapnelHit& Hit : Result.BlastHits)
	{
		if (Hit.HitActor.IsValid())
		{
			UniqueActors.Add(Hit.HitActor.Get());
		}
	}
	for (const FShrapnelHit& Hit : Result.ShrapnelHits)
	{
		if (Hit.HitActor.IsValid())
		{
			UniqueActors.Add(Hit.HitActor.Get());
		}
	}
	Result.TotalActorsDamaged = UniqueActors.Num();

	FBallisticsDebugDraw::DrawDetonation(
		GetWorld(), Result,
		ExplosiveDataAsset->BlastRadius,
		ExplosiveDataAsset->BlastInnerRadius
	);

	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		if (UProjectileLauncherComponent* Launcher = CachedSourceLauncher.Get())
		{
			Launcher->HandleDetonationCosmetic(Location, ExplosiveDataAsset);
		}
		else if (bCachedCosmeticRoutingEnabled)
		{
			if (UProjectileBallisticsSubsystem* Subsystem =
				World->GetSubsystem<UProjectileBallisticsSubsystem>())
			{
				if (UProjectileCosmeticsManager* Manager =
					Subsystem->GetCosmeticsManager())
				{
					Manager->RouteDetonation(Location, ExplosiveDataAsset);
				}
			}
		}
	}
	else if (World && bCachedCosmeticRoutingEnabled)
	{
		if (UProjectileBallisticsSubsystem* Subsystem =
			World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			if (UProjectileCosmeticsManager* Manager =
				Subsystem->GetCosmeticsManager())
			{
				Manager->RouteDetonation(Location, ExplosiveDataAsset);
			}
		}
	}

	OnDetonation.Broadcast(Result);

	return Result;
}

void UBallisticsDetonationComponent::ArmFuse()
{
	if (!ExplosiveDataAsset || bHasDetonated)
	{
		return;
	}

	bFuseArmed = true;
	FuseTimeRemaining = FMath::Max(ExplosiveDataAsset->FuseTime, 0.f);

	UWorld* World = GetWorld();
	UProjectileBallisticsSubsystem* Subsystem =
		World ? World->GetSubsystem<UProjectileBallisticsSubsystem>() : nullptr;
	UBallisticsAggregateTick* AggregateTicker =
		Subsystem ? Subsystem->GetAggregateTicker() : nullptr;
	if (!AggregateTicker)
	{
		bFuseArmed = false;
		FuseTimeRemaining = 0.f;
		return;
	}

	CachedAggregateTicker = AggregateTicker;
	AggregateTicker->RegisterArmedDetonation(this);
}

float UBallisticsDetonationComponent::GetRemainingFuseTime() const
{
	if (!bFuseArmed || bHasDetonated)
	{
		return 0.f;
	}
	return FuseTimeRemaining;
}
// Blast Damage

void UBallisticsDetonationComponent::PerformBlastDamage(const FVector& Location, FDetonationResult& OutResult)
{
	UWorld* World = GetWorld();
	if (!World || !ExplosiveDataAsset)
	{
		return;
	}

	// Sphere overlap to find actors in blast radius
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params;
	for (AActor* Ignored : IgnoredActors)
	{
		if (Ignored)
		{
			Params.AddIgnoredActor(Ignored);
		}
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectParams.AddObjectTypesToQuery(ECC_Destructible);
	ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);

	World->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity,
		ObjectParams, FCollisionShape::MakeSphere(ExplosiveDataAsset->BlastRadius), Params);

	// Process each overlapped actor
	TSet<AActor*> ProcessedActors;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || ProcessedActors.Contains(HitActor))
		{
			continue;
		}
		ProcessedActors.Add(HitActor);

		const FVector ActorLocation = HitActor->GetActorLocation();
		const float Distance = FVector::Dist(Location, ActorLocation);

		// Line of sight check
		if (ExplosiveDataAsset->bBlastRequiresLineOfSight)
		{
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(GetOwner());

			if (World->LineTraceSingleByChannel(LOSHit, Location, ActorLocation,
				ExplosiveDataAsset->BlastTraceChannel, LOSParams))
			{
				// Something is blocking LOS - check if it's the target actor itself
				if (LOSHit.GetActor() != HitActor)
				{
					continue; // Blocked by cover
				}
			}
		}

		const float Damage = ExplosiveDataAsset->CalculateBlastDamageAtDistance(Distance);
		if (Damage <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		FShrapnelHit BlastHit;
		BlastHit.HitActor = HitActor;
		BlastHit.FragmentDirection = (ActorLocation - Location).GetSafeNormal();
		BlastHit.Distance = Distance;
		BlastHit.Damage = Damage;
		OutResult.BlastHits.Add(BlastHit);
	}
}
// Shrapnel

void UBallisticsDetonationComponent::PerformShrapnel(const FVector& Location, FDetonationResult& OutResult)
{
	UWorld* World = GetWorld();
	if (!World || !ExplosiveDataAsset)
	{
		return;
	}

	const int32 Seed = GenerateDetonationSeed(Location);
	FHitscanConfig ShrapnelConfig = ExplosiveDataAsset->BuildShrapnelHitscanConfig(Seed, IgnoredActors);

	TArray<FHitscanResult> TraceResults = UBallisticsHitscanLibrary::PerformRadialHitscan(
		this, Location, ExplosiveDataAsset->ShrapnelCount, ShrapnelConfig
	);

	// Collect all hits from all shrapnel traces
	for (const FHitscanResult& TraceResult : TraceResults)
	{
		FBallisticsDebugDraw::DrawShrapnelTrace(World, Location, TraceResult);

		for (const FHitscanHit& Hit : TraceResult.Hits)
		{
			AActor* HitActor = Hit.HitResult.GetActor();
			if (!HitActor)
			{
				continue;
			}

			const float Distance = FVector::Dist(Location, Hit.HitResult.ImpactPoint);
			const float Damage = ExplosiveDataAsset->CalculateShrapnelDamageAtDistance(Distance);

			if (Damage <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FShrapnelHit ShrapnelHit;
			ShrapnelHit.HitActor = HitActor;
			ShrapnelHit.HitResult = Hit.HitResult;
			ShrapnelHit.FragmentDirection = (Hit.HitResult.ImpactPoint - Location).GetSafeNormal();
			ShrapnelHit.Distance = Distance;
			ShrapnelHit.Damage = Damage;
			OutResult.ShrapnelHits.Add(ShrapnelHit);
		}
	}
}
// Trigger Callbacks

void UBallisticsDetonationComponent::OnProjectileImpact(
	const FHitResult& HitResult,
	const FProjectileSnapshot& Snapshot,
	EImpactResponse Response)
{
	if (bHasDetonated || !ExplosiveDataAsset ||
		ExplosiveDataAsset->DetonationTrigger != EDetonationTrigger::OnImpact)
	{
		return;
	}

	if (ExplosiveDataAsset->ArmingDistance > 0.f &&
		FVector::DistSquared(ArmingOrigin, HitResult.ImpactPoint) <
			FMath::Square(ExplosiveDataAsset->ArmingDistance))
	{
		return;
	}

	DetonateAtLocation(HitResult.ImpactPoint);
}

void UBallisticsDetonationComponent::OnProjectileStateChanged(
	EProjectileState OldState,
	EProjectileState NewState)
{
	if (bHasDetonated || !ExplosiveDataAsset)
	{
		return;
	}

	if (ExplosiveDataAsset->DetonationTrigger == EDetonationTrigger::OnRest)
	{
		// Detonate when the projectile stops moving
		if (NewState == EProjectileState::Embedded || NewState == EProjectileState::Destroyed)
		{
			Detonate();
		}
	}
}
// Helpers

int32 UBallisticsDetonationComponent::GenerateDetonationSeed(const FVector& Location) const
{
	// Deterministic seed from location - same position always produces same shrapnel pattern
	return static_cast<int32>(
		FMath::FloorToInt(Location.X) ^
		FMath::FloorToInt(Location.Y) * 31 ^
		FMath::FloorToInt(Location.Z) * 97
	);
}
