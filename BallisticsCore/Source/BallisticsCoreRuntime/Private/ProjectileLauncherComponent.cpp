// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "ProjectileLauncherComponent.h"
#include "ProjectileBallisticsComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UProjectileLauncherComponent::UProjectileLauncherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProjectileLauncherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// ─────────────────────────────────────────────
//  Fire
// ─────────────────────────────────────────────

void UProjectileLauncherComponent::Fire(const FVector& Origin, const FVector& Direction)
{
	Fire_ClientPredicted(Origin, Direction);
}

// ─────────────────────────────────────────────
//  Client-Predicted Flow
// ─────────────────────────────────────────────

void UProjectileLauncherComponent::Fire_ClientPredicted(const FVector& Origin, const FVector& Direction)
{
	FProjectileInitialConditions Conditions = BaseConditions;
	Conditions.Origin = Origin;
	Conditions.Direction = Direction.GetSafeNormal();
	Conditions.RandomSeed = GenerateSeed();

	// Spawn local predicted projectile immediately (responsive feel)
	AActor* PredictedProjectile = SpawnProjectile(Conditions);
	if (PredictedProjectile)
	{
		PredictedProjectiles.Add(NextProjectileID, PredictedProjectile);
		NextProjectileID++;
	}

	// Get client timestamp for lag compensation
	float ClientTimestamp = 0.f;
	if (const UWorld* World = GetWorld())
	{
		ClientTimestamp = World->GetTimeSeconds();
	}

	// Tell the server
	ServerNotifyFire(Conditions, ClientTimestamp);
}

bool UProjectileLauncherComponent::ServerNotifyFire_Validate(FProjectileInitialConditions Conditions, float ClientTimestamp)
{
	if (Conditions.MuzzleVelocity < 0.f || Conditions.MuzzleVelocity > 200000.f)
	{
		return false;
	}
	return Conditions.Direction.IsNormalized();
}

void UProjectileLauncherComponent::ServerNotifyFire_Implementation(FProjectileInitialConditions Conditions, float ClientTimestamp)
{
	// Spawn authoritative projectile on server
	AActor* AuthProjectile = SpawnProjectile(Conditions);
	if (!AuthProjectile)
	{
		return;
	}

	// Fast-forward to compensate for client latency
	const float ServerTime = GetWorld()->GetTimeSeconds();
	const float Lag = FMath::Clamp(ServerTime - ClientTimestamp, 0.f, MaxLagCompensationWindow);

	if (Lag > KINDA_SMALL_NUMBER)
	{
		if (UProjectileBallisticsComponent* Comp = AuthProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->TickComponent(Lag, LEVELTICK_All, nullptr);
		}
	}
}

// ─────────────────────────────────────────────
//  Hit Validation (TODO: Consumer should implement)
// ─────────────────────────────────────────────

bool UProjectileLauncherComponent::ServerCheckClientHit_Validate(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	return HitActor != nullptr;
}

void UProjectileLauncherComponent::ServerCheckClientHit_Implementation(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	// Stub: consumer should override or bind validation logic
	// This is left as TODO for game-specific hit validation
}

// ─────────────────────────────────────────────
//  Shared Internals
// ─────────────────────────────────────────────

AActor* UProjectileLauncherComponent::SpawnProjectile(const FProjectileInitialConditions& Conditions)
{
	UWorld* World = GetWorld();
	if (!World || !ProjectileClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Projectile = World->SpawnActor<AActor>(ProjectileClass, Conditions.Origin, Conditions.Direction.Rotation(), SpawnParams);
	if (!Projectile)
	{
		return nullptr;
	}

	if (UProjectileBallisticsComponent* Comp = Projectile->FindComponentByClass<UProjectileBallisticsComponent>())
	{
		Comp->InitializeProjectile(Conditions);
	}

	return Projectile;
}

int32 UProjectileLauncherComponent::GenerateSeed()
{
	const int32 OwnerID = GetOwner() ? GetOwner()->GetUniqueID() : 0;
	return OwnerID ^ (++FireCounter * 2654435761);
}
