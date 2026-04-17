// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "ProjectileLauncherComponent.h"
#include "ProjectileBallisticsComponent.h"
#include "BallisticsCoreSettings.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UProjectileLauncherComponent::UProjectileLauncherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	FireCounter = 0;
	NextProjectileID = 0;
	bOverrideAuthorityMode = false;
	AuthorityModeOverride = EFireAuthorityMode::ClientPredicted;
	MaxLagCompensationWindow = 0.25f;
	HitValidationTolerance = 50.f;
}

void UProjectileLauncherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// ─────────────────────────────────────────────
//  Authority Mode
// ─────────────────────────────────────────────

EFireAuthorityMode UProjectileLauncherComponent::GetActiveAuthorityMode() const
{
	if (bOverrideAuthorityMode)
	{
		return AuthorityModeOverride;
	}
	return UBallisticsCoreSettings::Get()->FireAuthorityMode;
}

// ─────────────────────────────────────────────
//  Fire (routes to correct authority path)
// ─────────────────────────────────────────────

void UProjectileLauncherComponent::Fire(const FVector& Origin, const FVector& Direction)
{
	switch (GetActiveAuthorityMode())
	{
	case EFireAuthorityMode::ServerAuthoritative:
		Fire_ServerAuthoritative(Origin, Direction);
		break;

	case EFireAuthorityMode::ClientPredicted:
		Fire_ClientPredicted(Origin, Direction);
		break;
	}
}

// ─────────────────────────────────────────────
//  Server-Authoritative Flow
// ─────────────────────────────────────────────

void UProjectileLauncherComponent::Fire_ServerAuthoritative(const FVector& Origin, const FVector& Direction)
{
	FProjectileInitialConditions Conditions = BaseConditions;
	Conditions.Origin = Origin;
	Conditions.Direction = Direction.GetSafeNormal();
	Conditions.RandomSeed = GenerateSeed();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// We're on the server already — spawn directly
		SpawnProjectile(Conditions);
		MulticastOnProjectileFired(Conditions);
	}
	else
	{
		// We're on the client — ask the server to fire
		ServerRequestFire(Conditions);
	}
}

bool UProjectileLauncherComponent::ServerRequestFire_Validate(FProjectileInitialConditions Conditions)
{
	if (Conditions.MuzzleVelocity < 0.f || Conditions.MuzzleVelocity > 200000.f)
	{
		return false;
	}
	return Conditions.Direction.IsNormalized();
}

void UProjectileLauncherComponent::ServerRequestFire_Implementation(FProjectileInitialConditions Conditions)
{
	// Server spawns the authoritative projectile
	SpawnProjectile(Conditions);

	// Tell all clients so they can spawn visual-only representations
	MulticastOnProjectileFired(Conditions);
}

void UProjectileLauncherComponent::MulticastOnProjectileFired_Implementation(FProjectileInitialConditions Conditions)
{
	// On non-authority clients, spawn a visual-only projectile
	// The owning client already sees it from the server replication
	if (!GetOwner() || GetOwner()->HasAuthority())
	{
		return;
	}

	// Spawn a local cosmetic projectile — the server owns the real simulation
	AActor* CosmeticProjectile = SpawnProjectile(Conditions);
	if (CosmeticProjectile)
	{
		// Mark as cosmetic so the ballistics component knows not to do authority checks
		if (UProjectileBallisticsComponent* Comp = CosmeticProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetIsLocallyPredicted(true);
		}
	}
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

	const int32 ProjectileID = NextProjectileID++;

	// Spawn local predicted projectile immediately (responsive feel)
	AActor* PredictedProjectile = SpawnProjectile(Conditions);
	if (PredictedProjectile)
	{
		if (UProjectileBallisticsComponent* Comp = PredictedProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetIsLocallyPredicted(true);
			Comp->SetProjectileID(ProjectileID);
		}
		PredictedProjectiles.Add(ProjectileID, PredictedProjectile);
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

			// Check if we need to tell the client to reconcile
			// This would happen if the fast-forward result diverges significantly
			// from where the client expects the projectile to be
			const FProjectileSnapshot ServerSnap = Comp->GetCurrentSnapshot();
			const FProjectileSnapshot ClientExpected = Comp->EvaluateAtTime(Lag);

			const float Divergence = FVector::Dist(ServerSnap.Position, ClientExpected.Position);
			const float Tolerance = UBallisticsCoreSettings::Get()->PredictionReconciliationTolerance;

			if (Divergence > Tolerance)
			{
				ClientReconcileProjectile(
					Comp->GetProjectileID(),
					ServerSnap.Position,
					ServerSnap.Velocity,
					Comp->GetElapsedTime()
				);
			}
		}
	}
}

// ─────────────────────────────────────────────
//  Client Reconciliation
// ─────────────────────────────────────────────

void UProjectileLauncherComponent::ClientReconcileProjectile_Implementation(
	int32 ProjectileID,
	FVector CorrectedPosition,
	FVector CorrectedVelocity,
	float ServerElapsedTime)
{
	// Find our predicted projectile by ID
	TWeakObjectPtr<AActor>* Found = PredictedProjectiles.Find(ProjectileID);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	AActor* PredictedActor = Found->Get();
	UProjectileBallisticsComponent* Comp = PredictedActor->FindComponentByClass<UProjectileBallisticsComponent>();
	if (!Comp)
	{
		return;
	}

	// Snap the predicted projectile to the server's corrected state
	Comp->ForceCorrection(CorrectedPosition, CorrectedVelocity, ServerElapsedTime);
}

// ─────────────────────────────────────────────
//  Hit Validation
// ─────────────────────────────────────────────

bool UProjectileLauncherComponent::ServerCheckClientHit_Validate(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	return HitActor != nullptr;
}

void UProjectileLauncherComponent::ServerCheckClientHit_Implementation(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	if (!HitActor)
	{
		return;
	}

	bool bConfirmed = false;

	if (OnValidateHit.IsBound())
	{
		bConfirmed = OnValidateHit.Execute(HitActor, ClaimTimestamp, ClaimPosition);
	}
	else
	{
		// No validation bound — reject by default (safe fallback)
		UE_LOG(LogTemp, Warning,
			TEXT("BallisticsCore: ServerCheckClientHit called but no OnValidateHit delegate is bound. "
			     "Hit rejected. Bind your validation logic to OnValidateHit on the launcher component."));
	}

	OnHitValidationResult.Broadcast(HitActor, ClaimPosition, bConfirmed);
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
