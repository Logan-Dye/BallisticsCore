// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileBallisticsSubsystem.h"
#include "BallisticsLaunchContributorComponent.h"
#include "GameFramework/Pawn.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileTracerComponent.h"
#include "BallisticsDetonationComponent.h"
#include "BallisticsAggregateTick.h"
#include "BallisticsImpactBudget.h"
#include "ProjectileCosmeticsManager.h"
#include "BallisticsDebugDraw.h"
#include "BallisticsCoreSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
// Subsystem Lifecycle

void UProjectileBallisticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

	// The aggregate object is the single scheduler for all ballistics services.
	// still use this one world-level update.
	{
		TSubclassOf<UBallisticsAggregateTick> TickerClass =
			Settings ? Settings->AggregateTickClass : nullptr;
		if (!TickerClass)
		{
			TickerClass = UBallisticsAggregateTick::StaticClass();
		}

		AggregateTicker = NewObject<UBallisticsAggregateTick>(this, TickerClass);
	}

	if (Settings && Settings->bEnableImpactBudget)
	{
		ImpactBudget = NewObject<UBallisticsImpactBudget>(this);
	}

	CosmeticsManager = NewObject<UProjectileCosmeticsManager>(this);
	CosmeticsManager->Initialize(GetWorld());
	if (AggregateTicker)
	{
		AggregateTicker->OnAggregateUpdate().AddUObject(
			CosmeticsManager, &UProjectileCosmeticsManager::Tick);
	}

	if (UWorld* World = GetWorld())
	{
		ActorSpawnedDelegateHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this,
				&UProjectileBallisticsSubsystem::HandleActorSpawned));
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			TrackFlyByPawn(*It);
		}
	}
}

void UProjectileBallisticsSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); World && ActorSpawnedDelegateHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedDelegateHandle);
	}
	ActorSpawnedDelegateHandle.Reset();
	for (const TWeakObjectPtr<APawn>& WeakPawn : WatchedFlyByPawns)
	{
		if (APawn* Pawn = WeakPawn.Get())
		{
			Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(
				this,
				&UProjectileBallisticsSubsystem::HandleFlyByPawnControllerChanged);
			Pawn->OnDestroyed.RemoveDynamic(
				this,
				&UProjectileBallisticsSubsystem::HandleFlyByPawnDestroyed);
		}
	}
	WatchedFlyByPawns.Empty();
	AutomaticFlyByListeners.Empty();
	ManualFlyByListeners.Empty();
	FlyByListeners.Empty();
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
	PendingProjectileRegistrations.Empty();
	PendingProjectileUnregistrations.Empty();
	bBatchTickInProgress = false;
	bPruneAfterBatchTick = false;
	AggregateTicker = nullptr;
	ImpactBudget = nullptr;
	if (CosmeticsManager)
	{
		CosmeticsManager->Shutdown();
		CosmeticsManager = nullptr;
	}

	Super::Deinitialize();
}

bool UProjectileBallisticsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UProjectileBallisticsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ImpactBudget)
	{
		ImpactBudget->ResetFrameBudget();
	}

	if (AggregateTicker)
	{
		AggregateTicker->TickServices(DeltaTime);

		const UBallisticsCoreSettings* CurrentSettings = UBallisticsCoreSettings::Get();
		if (!CurrentSettings || CurrentSettings->bEnableTickBatching)
		{
			PruneStaleProjectiles();
			bBatchTickInProgress = true;
			AggregateTicker->BatchTick(ActiveProjectiles, DeltaTime);
			bBatchTickInProgress = false;
			FlushPendingProjectileRegistrations();
		}
	}

	RefreshLaunchContributors(DeltaTime);
	const UBallisticsCoreSettings* CurrentSettings = UBallisticsCoreSettings::Get();
	if (CurrentSettings && CurrentSettings->bAutoRegisterPlayerFlyByListeners)
	{
		FlyByListenerVerificationAccumulator += FMath::Max(0.f, DeltaTime);
		if (FlyByListenerVerificationAccumulator >= 0.5f)
		{
			FlyByListenerVerificationAccumulator = FMath::Fmod(
				FlyByListenerVerificationAccumulator,
				0.5f);
			VerifyWatchedFlyByListeners();
		}
	}

	if (ImpactBudget)
	{
		FBallisticsDebugDraw::DrawBudgetHUD(GetWorld(),
			ImpactBudget->GetFrameImpactsUsed(),
			ImpactBudget->GetFrameBudgetMax());
	}
}

void UProjectileBallisticsSubsystem::RegisterLaunchContributor(
	UBallisticsLaunchContributorComponent* Component)
{
	if (Component)
	{
		LaunchContributors.AddUnique(Component);
	}
}

void UProjectileBallisticsSubsystem::UnregisterLaunchContributor(
	UBallisticsLaunchContributorComponent* Component)
{
	LaunchContributors.RemoveAll(
		[Component](const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Component;
		});
}

void UProjectileBallisticsSubsystem::RefreshLaunchContributors(float DeltaTime)
{
	for (int32 Index = LaunchContributors.Num() - 1; Index >= 0; --Index)
	{
		if (UBallisticsLaunchContributorComponent* Component =
			LaunchContributors[Index].Get())
		{
			Component->PollLauncherBinding();
		}
		else
		{
			LaunchContributors.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	const int32 ContributorCount = LaunchContributors.Num();
	if (ContributorCount == 0)
	{
		LaunchContributorAuditCursor = 0;
		LaunchContributorAuditWork = 0.f;
		return;
	}

	constexpr float FullAuditIntervalSeconds = 0.5f;
	LaunchContributorAuditWork += FMath::Max(0.f, DeltaTime) *
		static_cast<float>(ContributorCount) / FullAuditIntervalSeconds;
	const int32 AuditCount = FMath::Min(
		FMath::FloorToInt(LaunchContributorAuditWork),
		ContributorCount);
	LaunchContributorAuditWork -= static_cast<float>(AuditCount);
	for (int32 AuditIndex = 0; AuditIndex < AuditCount; ++AuditIndex)
	{
		LaunchContributorAuditCursor %= ContributorCount;
		if (UBallisticsLaunchContributorComponent* Component =
			LaunchContributors[LaunchContributorAuditCursor].Get())
		{
			Component->RefreshLauncherBinding();
		}
		++LaunchContributorAuditCursor;
	}
}

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
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.InitialSpeed;
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

	while (Time + KINDA_SMALL_NUMBER < Duration)
	{
		const float dt = FMath::Min(StepSize, Duration - Time);
		const FVector OldPos = Pos;

		FVector Accel(0.f, 0.f, Gravity);
		const float Speed = Vel.Size();
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const float DragAccelMag = (0.5f * Air * Conditions.DragCoefficient * CrossSection * Speed * Speed) / Conditions.Mass;
			Accel -= Vel.GetSafeNormal() * DragAccelMag;
		}
		Accel += Conditions.WindAcceleration;

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

void UProjectileBallisticsSubsystem::RegisterProjectile(UProjectileBallisticsComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (bBatchTickInProgress)
	{
		PendingProjectileUnregistrations.Remove(Component);
		if (!ActiveProjectiles.Contains(Component))
		{
			PendingProjectileRegistrations.AddUnique(Component);
		}
		return;
	}

	if (!ActiveProjectiles.Contains(Component))
	{
		ActiveProjectiles.Add(Component);
		if (AggregateTicker)
		{
			AggregateTicker->OnProjectileRegistered(Component);
		}
	}
}

void UProjectileBallisticsSubsystem::RegisterFlyByListener(APawn* Listener)
{
	if (Listener)
	{
		ManualFlyByListeners.Add(Listener);
		FlyByListeners.Add(Listener);
	}
}

void UProjectileBallisticsSubsystem::UnregisterFlyByListener(APawn* Listener)
{
	if (Listener)
	{
		ManualFlyByListeners.Remove(Listener);
		if (!AutomaticFlyByListeners.Contains(Listener))
		{
			FlyByListeners.Remove(Listener);
		}
	}
}

void UProjectileBallisticsSubsystem::TrackFlyByPawn(APawn* Pawn)
{
	if (!Pawn || WatchedFlyByPawns.Contains(Pawn))
	{
		return;
	}
	WatchedFlyByPawns.Add(Pawn);
	Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(
		this,
		&UProjectileBallisticsSubsystem::HandleFlyByPawnControllerChanged);
	Pawn->OnDestroyed.AddUniqueDynamic(
		this,
		&UProjectileBallisticsSubsystem::HandleFlyByPawnDestroyed);
	UpdateAutomaticFlyByListener(Pawn);
}

void UProjectileBallisticsSubsystem::UpdateAutomaticFlyByListener(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const bool bShouldRegister = Settings &&
		Settings->bAutoRegisterPlayerFlyByListeners &&
		(Pawn->IsPlayerControlled() || Pawn->GetPlayerState() != nullptr);
	if (bShouldRegister == AutomaticFlyByListeners.Contains(Pawn))
	{
		return;
	}
	if (bShouldRegister)
	{
		AutomaticFlyByListeners.Add(Pawn);
		FlyByListeners.Add(Pawn);
	}
	else
	{
		AutomaticFlyByListeners.Remove(Pawn);
		if (!ManualFlyByListeners.Contains(Pawn))
		{
			FlyByListeners.Remove(Pawn);
		}
	}
}

void UProjectileBallisticsSubsystem::VerifyWatchedFlyByListeners()
{
	for (auto It = WatchedFlyByPawns.CreateIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get())
		{
			UpdateAutomaticFlyByListener(Pawn);
		}
		else
		{
			It.RemoveCurrent();
		}
	}
}

void UProjectileBallisticsSubsystem::HandleActorSpawned(AActor* Actor)
{
	TrackFlyByPawn(Cast<APawn>(Actor));
}

void UProjectileBallisticsSubsystem::HandleFlyByPawnControllerChanged(
	APawn* Pawn,
	AController*,
	AController*)
{
	UpdateAutomaticFlyByListener(Pawn);
}

void UProjectileBallisticsSubsystem::HandleFlyByPawnDestroyed(AActor* DestroyedActor)
{
	APawn* Pawn = Cast<APawn>(DestroyedActor);
	if (!Pawn)
	{
		return;
	}
	WatchedFlyByPawns.Remove(Pawn);
	AutomaticFlyByListeners.Remove(Pawn);
	ManualFlyByListeners.Remove(Pawn);
	FlyByListeners.Remove(Pawn);
}

void UProjectileBallisticsSubsystem::UnregisterProjectile(UProjectileBallisticsComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (bBatchTickInProgress)
	{
		PendingProjectileRegistrations.Remove(Component);
		PendingProjectileUnregistrations.AddUnique(Component);
		return;
	}

	const int32 Removed = ActiveProjectiles.RemoveAll(
		[Component](const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Component;
		});
	if (Removed > 0 && AggregateTicker)
	{
		AggregateTicker->OnProjectileUnregistered(Component);
	}
}

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

AActor* UProjectileBallisticsSubsystem::AcquireProjectile(
	TSubclassOf<AActor> ProjectileClass,
	const FVector& Location,
	const FRotator& Rotation,
	AActor* NewOwner,
	APawn* NewInstigator)
{
	UWorld* World = GetWorld();
	if (!World || !ProjectileClass)
	{
		return nullptr;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const bool bPoolingEnabled = Settings && Settings->bEnableProjectilePooling;

	// Reuse a matching pooled actor.
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
				Pooled->SetOwner(NewOwner);
				Pooled->SetInstigator(NewInstigator);
				ActivateActor(Pooled, Location, Rotation);
				return Pooled;
			}
		}
	}

	// Pool empty or wrong class - spawn fresh
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = NewOwner;
	SpawnParams.Instigator = NewInstigator;
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

	if (UProjectileBallisticsComponent* Comp = Actor->FindComponentByClass<UProjectileBallisticsComponent>())
	{
		Comp->SetComponentTickEnabled(false);
	}

	if (UProjectileTracerComponent* Tracer = Actor->FindComponentByClass<UProjectileTracerComponent>())
	{
		Tracer->SetComponentTickEnabled(false);
	}

	if (UBallisticsDetonationComponent* Detonation =
		Actor->FindComponentByClass<UBallisticsDetonationComponent>())
	{
		// A fuse or manual trigger must never keep running after a projectile has
		// returned to the pool. The next launcher configures a fresh flight.
		Detonation->ConfigureExplosive(nullptr);
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
		const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
		Comp->SetComponentTickEnabled(!(Settings && Settings->bEnableTickBatching));
	}

	if (UProjectileTracerComponent* Tracer = Actor->FindComponentByClass<UProjectileTracerComponent>())
	{
		Tracer->SetComponentTickEnabled(!IsRunningDedicatedServer());
	}
}

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
			DrawDebugSphere(World, Snap.Position, 10.f, 8, DrawColor, false, Duration);

			if (Snap.Speed > KINDA_SMALL_NUMBER)
			{
				DrawDebugDirectionalArrow(World, Snap.Position,
					Snap.Position + Snap.Velocity.GetSafeNormal() * 100.f,
					20.f, DrawColor, false, Duration);
			}
		}
	}
}

void UProjectileBallisticsSubsystem::PruneStaleProjectiles()
{
	if (bBatchTickInProgress)
	{
		bPruneAfterBatchTick = true;
		return;
	}

	ActiveProjectiles.RemoveAll([](const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak)
	{
		return !Weak.IsValid();
	});
}

void UProjectileBallisticsSubsystem::FlushPendingProjectileRegistrations()
{
	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Pending :
		PendingProjectileUnregistrations)
	{
		UProjectileBallisticsComponent* Component = Pending.Get();
		const int32 Removed = ActiveProjectiles.RemoveAll(
			[&Pending](const TWeakObjectPtr<UProjectileBallisticsComponent>& Weak)
			{
				return !Weak.IsValid() || Weak == Pending;
			});
		if (Removed > 0 && Component && AggregateTicker)
		{
			AggregateTicker->OnProjectileUnregistered(Component);
		}
	}

	for (const TWeakObjectPtr<UProjectileBallisticsComponent>& Pending :
		PendingProjectileRegistrations)
	{
		if (UProjectileBallisticsComponent* Component = Pending.Get())
		{
			if (!ActiveProjectiles.Contains(Pending))
			{
				ActiveProjectiles.Add(Pending);
				if (AggregateTicker)
				{
					AggregateTicker->OnProjectileRegistered(Component);
				}
			}
		}
	}

	PendingProjectileUnregistrations.Reset();
	PendingProjectileRegistrations.Reset();

	if (bPruneAfterBatchTick)
	{
		bPruneAfterBatchTick = false;
		PruneStaleProjectiles();
	}
}
