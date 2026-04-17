// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileBallisticsComponent.h"
#include "BallisticsCoreSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

// ─────────────────────────────────────────────
//  Subsystem Lifecycle
// ─────────────────────────────────────────────

void UProjectileBallisticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectileBallisticsSubsystem::Deinitialize()
{
	// Destroy any pooled actors
	for (const TWeakObjectPtr<AActor>& Weak : ProjectilePool)
	{
		if (AActor* Actor = Weak.Get())
		{
			Actor->Destroy();
		}
	}
	ProjectilePool.Empty();
	ActiveProjectiles.Empty();

	Super::Deinitialize();
}

bool UProjectileBallisticsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UProjectileBallisticsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (Settings && Settings->bEnableTickBatching)
	{
		BatchTickProjectiles(DeltaTime);
	}
}

// ─────────────────────────────────────────────
//  Trajectory Prediction
// ─────────────────────────────────────────────

FTrajectoryPredictionResult UProjectileBallisticsSubsystem::PredictTrajectory(
	const FProjectileInitialConditions& Conditions,
	float Duration,
	float StepSize)
{
	FTrajectoryPredictionResult Result;

	if (Duration <= 0.f || StepSize <= 0.f)
	{
		return Result;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float Gravity = Settings ? Settings->GravityZ : -980.f;
	const float Air = Settings ? Settings->AirDensity : 0.001225f;
	const float CrossSection = PI * FMath::Square(Conditions.Diameter * 0.5f);

	FVector Pos = Conditions.Origin;
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.MuzzleVelocity;
	float Time = 0.f;
	float TotalDist = 0.f;

	Result.Points.Add(Pos);

	FProjectileSnapshot InitSnap;
	InitSnap.Position = Pos;
	InitSnap.Velocity = Vel;
	InitSnap.ElapsedTime = 0.f;
	InitSnap.Speed = Vel.Size();
	InitSnap.State = EProjectileState::InFlight;
	Result.Snapshots.Add(InitSnap);

	while (Time < Duration)
	{
		const float dt = FMath::Min(StepSize, Duration - Time);
		const FVector OldPos = Pos;

		// Compute acceleration
		FVector Accel(0.f, 0.f, Gravity);
		const float Speed = Vel.Size();
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const float DragAccelMag = (0.5f * Air * Conditions.DragCoefficient * CrossSection * Speed * Speed) / Conditions.Mass;
			Accel -= Vel.GetSafeNormal() * DragAccelMag;
		}
		Accel += Conditions.WindAcceleration;

		// Step
		Pos += Vel * dt + 0.5f * Accel * dt * dt;
		Vel += Accel * dt;
		Time += dt;
		TotalDist += FVector::Dist(OldPos, Pos);

		Result.Points.Add(Pos);

		FProjectileSnapshot Snap;
		Snap.Position = Pos;
		Snap.Velocity = Vel;
		Snap.ElapsedTime = Time;
		Snap.Speed = Vel.Size();
		Snap.State = EProjectileState::InFlight;
		Result.Snapshots.Add(Snap);
	}

	Result.TotalDistance = TotalDist;
	Result.TotalTime = Time;
	return Result;
}

// ─────────────────────────────────────────────
//  Registration
// ─────────────────────────────────────────────

void UProjectileBallisticsSubsystem::RegisterProjectile(UProjectileBallisticsComponent* Component)
{
	if (Component)
	{
		ActiveProjectiles.AddUnique(Component);
	}
}

void UProjectileBallisticsSubsystem::UnregisterProjectile(UProjectileBallisticsComponent* Component)
{
	ActiveProjectiles.RemoveAll([Component](const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == Component;
	});
}

// ─────────────────────────────────────────────
//  Queries
// ─────────────────────────────────────────────

TArray<UProjectileBallisticsComponent*> UProjectileBallisticsSubsystem::GetProjectilesInRadius(FVector Center, float Radius)
{
	PruneStaleProjectiles();

	const float RadiusSq = Radius * Radius;
	TArray<UProjectileBallisticsComponent*> Results;

	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak : ActiveProjectiles)
	{
		if (UProjectileBallisticsComponent* Comp = Weak.Get())
		{
			const FProjectileSnapshot Snap = Comp->GetCurrentSnapshot();
			if (FVector::DistSquared(Snap.Position, Center) <= RadiusSq)
			{
				Results.Add(Comp);
			}
		}
	}

	return Results;
}

// ─────────────────────────────────────────────
//  Object Pooling
// ─────────────────────────────────────────────

AActor* UProjectileBallisticsSubsystem::AcquireProjectile(TSubclassOf<AActor> ProjectileClass, const FVector& Location, const FRotator& Rotation)
{
	UWorld* World = GetWorld();
	if (!World || !ProjectileClass)
	{
		return nullptr;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const bool bPoolingEnabled = Settings && Settings->bEnableProjectilePooling;

	// Try to pull from pool
	if (bPoolingEnabled)
	{
		for (int32 i = ProjectilePool.Num() - 1; i >= 0; --i)
		{
			AActor* Pooled = ProjectilePool[i].Get();
			if (!Pooled)
			{
				ProjectilePool.RemoveAt(i);
				continue;
			}

			if (Pooled->GetClass() == ProjectileClass)
			{
				ProjectilePool.RemoveAt(i);
				ActivateActor(Pooled, Location, Rotation);
				return Pooled;
			}
		}
	}

	// Pool empty or wrong class — spawn fresh
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	return World->SpawnActor<AActor>(ProjectileClass, Location, Rotation, SpawnParams);
}

void UProjectileBallisticsSubsystem::ReleaseProjectile(AActor* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const bool bPoolingEnabled = Settings && Settings->bEnableProjectilePooling;

	if (bPoolingEnabled)
	{
		DeactivateActor(Projectile);
		ProjectilePool.Add(Projectile);
	}
	else
	{
		Projectile->Destroy();
	}
}

void UProjectileBallisticsSubsystem::PreWarmPool(TSubclassOf<AActor> ProjectileClass, int32 Count)
{
	UWorld* World = GetWorld();
	if (!World || !ProjectileClass || Count <= 0)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* Actor = World->SpawnActor<AActor>(ProjectileClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (Actor)
		{
			DeactivateActor(Actor);
			ProjectilePool.Add(Actor);
		}
	}
}

void UProjectileBallisticsSubsystem::DeactivateActor(AActor* Actor)
{
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);

	// Disable the ballistics component tick too
	if (UProjectileBallisticsComponent* Comp = Actor->FindComponentByClass<UProjectileBallisticsComponent>())
	{
		Comp->SetComponentTickEnabled(false);
	}
}

void UProjectileBallisticsSubsystem::ActivateActor(AActor* Actor, const FVector& Location, const FRotator& Rotation)
{
	Actor->SetActorLocation(Location);
	Actor->SetActorRotation(Rotation);
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);

	if (UProjectileBallisticsComponent* Comp = Actor->FindComponentByClass<UProjectileBallisticsComponent>())
	{
		Comp->SetComponentTickEnabled(true);
	}
}

// ─────────────────────────────────────────────
//  Tick Batching
// ─────────────────────────────────────────────

void UProjectileBallisticsSubsystem::BatchTickProjectiles(float DeltaTime)
{
	PruneStaleProjectiles();

	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak : ActiveProjectiles)
	{
		if (UProjectileBallisticsComponent* Comp = Weak.Get())
		{
			if (Comp->IsInFlight())
			{
				// Tick through the component's public interface
				// When batch ticking, individual component ticks should be disabled
				Comp->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
			}
		}
	}
}

// ─────────────────────────────────────────────
//  Debug
// ─────────────────────────────────────────────

void UProjectileBallisticsSubsystem::DebugDrawActiveTrajectories(float Duration, FLinearColor Color)
{
	PruneStaleProjectiles();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FColor DrawColor = Color.ToFColor(true);

	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak : ActiveProjectiles)
	{
		if (UProjectileBallisticsComponent* Comp = Weak.Get())
		{
			const FProjectileSnapshot Snap = Comp->GetCurrentSnapshot();
			DrawDebugLine(World, Comp->EvaluateAtTime(0.f).Position, Snap.Position, DrawColor, false, Duration, 0, 1.f);
		}
	}
}

void UProjectileBallisticsSubsystem::PruneStaleProjectiles()
{
	ActiveProjectiles.RemoveAll([](const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak)
	{
		return !Weak.IsValid();
	});
}
