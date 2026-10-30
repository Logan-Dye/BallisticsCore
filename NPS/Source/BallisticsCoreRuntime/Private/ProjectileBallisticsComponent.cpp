// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileBallisticsComponent.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileNetProxyComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "ProjectileBallisticsSubsystem.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsDamageInterface.h"
#include "BallisticsDebugDraw.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsProjectileDataAsset.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

UProjectileBallisticsComponent::UProjectileBallisticsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
#if UE_WITH_IRIS
	// Iris uses registered subobjects; legacy replication also supports this list.
	bReplicateUsingRegisteredSubObjectList = true;
#endif

	ElapsedTime = 0.f;
	CurrentState = EProjectileState::InFlight;
	CurrentPosition = FVector::ZeroVector;
	CurrentVelocity = FVector::ZeroVector;
	RicochetCount = 0;
	ProjectileID = -1;
	bIsLocallyPredicted = false;

	// Sliding defaults seeded from project settings. BP-level overrides on the
	// component still win (serialized BP values apply after the constructor).
	if (const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get())
	{
		bEnableSliding = Settings->bDefaultEnableSliding;
		MinSlidingSpeed = Settings->DefaultMinSlidingSpeed;
		SlidingFriction = Settings->DefaultSlidingFriction;
	}
}

void UProjectileBallisticsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProjectileBallisticsComponent, InitialConditions);
}

void UProjectileBallisticsComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority() &&
		Owner->FindComponentByClass<UProjectileNetProxyComponent>())
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem = World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			Subsystem->RegisterProjectile(this);
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
			bSimulationRegisteredForTests = true;
#endif
		}
	}

	// Aggregate ticking owns simulation when enabled. Keeping the component tick
	// active here would advance every projectile twice per frame.
	if (const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get())
	{
		if (Settings->bEnableTickBatching)
		{
			EnforceSimulationTickOwnership();
		}
		else
		{
			SetComponentTickEnabled(true);
		}
	}
}

#if WITH_EDITOR
void UProjectileBallisticsComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (Settings && Settings->bEnableTickBatching &&
		PrimaryComponentTick.bStartWithTickEnabled)
	{
		PrimaryComponentTick.bStartWithTickEnabled = false;
		SetComponentTickEnabled(false);
		NPS_LOG(Warning,
			TEXT("NPS corrected '%s': component Tick cannot be enabled while aggregate ticking is active. "
			     "Enabling both would advance the projectile twice."),
			*GetPathName());
	}
}
#endif

bool UProjectileBallisticsComponent::EnforceSimulationTickOwnership()
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (!Settings || !Settings->bEnableTickBatching)
	{
		return true;
	}

	const bool bInvalidTickWasEnabled = IsComponentTickEnabled();
	SetComponentTickEnabled(false);
	if (bInvalidTickWasEnabled && !bWarnedAboutExternalTick)
	{
		bWarnedAboutExternalTick = true;
		NPS_LOG(Warning,
			TEXT("NPS blocked and disabled individual Tick on %s. "
			     "Aggregate ticking is the sole simulation owner."),
			*GetPathName());
	}
	return false;
}

void UProjectileBallisticsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem = World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			Subsystem->UnregisterProjectile(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UProjectileBallisticsComponent::InitializeProjectile(const FProjectileInitialConditions& Conditions)
{
	InitialConditions = Conditions;
	InitialConditions.Direction = Conditions.Direction.GetSafeNormal();

	CurrentPosition = Conditions.Origin;
	CurrentVelocity = InitialConditions.Direction * Conditions.InitialSpeed;
	ElapsedTime = 0.f;
	RicochetCount = 0;

	FlyByTriggered.Reset();
	ActiveFlightLog = FProjectileFlightLog();
	ActiveFlightLog.InitialConditions = InitialConditions;
	if (const UWorld* World = GetWorld())
	{
		const AGameStateBase* GameState = World->GetGameState();
		ActiveFlightLog.ClientTimestamp = GameState
			? GameState->GetServerWorldTimeSeconds()
			: World->GetTimeSeconds();
	}
	NextFlightLogReportSequence = 0;
	LastSubmittedFlightLogSegmentCount = 0;
	bFinalFlightLogSubmitted = false;

	DeterministicRandom.Initialize(Conditions.RandomSeed);

	SetState(EProjectileState::InFlight);

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(CurrentPosition);

		if (UProjectileNetProxyComponent* NetProxy =
			Owner->FindComponentByClass<UProjectileNetProxyComponent>())
		{
			NetProxy->StartReplicatingTransform();
		}
	}
}

void UProjectileBallisticsComponent::SetSourceLauncher(
	UProjectileLauncherComponent* InLauncher,
	UBallisticsProjectileDataAsset* InProjectileData)
{
	SourceLauncher = InLauncher;
	SourceProjectileData = InProjectileData;
}

FProjectileSnapshot UProjectileBallisticsComponent::GetCurrentSnapshot() const
{
	FProjectileSnapshot Snapshot;
	Snapshot.Position = CurrentPosition;
	Snapshot.Velocity = CurrentVelocity;
	Snapshot.ElapsedTime = ElapsedTime;
	Snapshot.Speed = CurrentVelocity.Size();
	Snapshot.State = CurrentState;
	return Snapshot;
}

FProjectileSnapshot UProjectileBallisticsComponent::EvaluateAtTime(float Time) const
{
	// Pure math evaluation - no collision, no state changes
	// but without side effects

	FVector Pos = InitialConditions.Origin;
	FVector Vel = InitialConditions.Direction * InitialConditions.InitialSpeed;
	float Remaining = Time;

	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float dt = FMath::Min(TrajectoryStepSize, Remaining);

		const FVector Accel = ComputeAcceleration(Vel);
		Pos += Vel * dt + 0.5f * Accel * dt * dt;
		Vel += Accel * dt;

		Remaining -= dt;
	}

	FProjectileSnapshot Snapshot;
	Snapshot.Position = Pos;
	Snapshot.Velocity = Vel;
	Snapshot.ElapsedTime = Time;
	Snapshot.Speed = Vel.Size();
	Snapshot.State = EProjectileState::InFlight;
	return Snapshot;
}

void UProjectileBallisticsComponent::ForceCorrection(const FVector& NewPosition, const FVector& NewVelocity, float NewElapsedTime)
{
	CurrentPosition = NewPosition;
	CurrentVelocity = NewVelocity;
	ElapsedTime = NewElapsedTime;

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(CurrentPosition);
		if (!CurrentVelocity.IsNearlyZero())
		{
			Owner->SetActorRotation(CurrentVelocity.Rotation());
		}
	}

	// Stop when the correction falls below the flight threshold.
	if (CurrentVelocity.Size() < MinFlightSpeed && IsInFlight())
	{
		SetState(EProjectileState::Destroyed);
		SubmitAccumulatedFlightLog(true);
		OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
	}
}

void UProjectileBallisticsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Aggregate simulation calls this method deliberately with no tick function.
	// Any engine/component tick while batching is enabled is a conflicting owner:
	// disable it before it can advance the projectile a second time.
	if (ThisTickFunction != nullptr && !EnforceSimulationTickOwnership())
	{
		return;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CurrentState == EProjectileState::Sliding)
	{
		StepSliding(DeltaTime);
		return;
	}

	if (!IsInFlight())
	{
		return;
	}

	StepTrajectory(DeltaTime);
}

void UProjectileBallisticsComponent::StepTrajectory(float DeltaTime)
{
	float Remaining = DeltaTime;

	while (Remaining > KINDA_SMALL_NUMBER && IsInFlight())
	{
		const float dt = FMath::Min(TrajectoryStepSize, Remaining);

		const FVector OldPosition = CurrentPosition;
		const FVector Accel = ComputeAcceleration(CurrentVelocity);

		// Constant-acceleration analytic step
		CurrentPosition += CurrentVelocity * dt + 0.5f * Accel * dt * dt;
		CurrentVelocity += Accel * dt;
		ElapsedTime += dt;

		FBallisticsDebugDraw::DrawPathSegment(GetWorld(), OldPosition, CurrentPosition, CurrentState);

		// Sweep for collisions between old and new position
		if (ProcessSweep(OldPosition, CurrentPosition))
		{
			// Hit something - ProcessSweep handled the response
			// Position and velocity are already updated by the handler
			break;
		}

		CheckFlyByListeners(OldPosition, CurrentPosition);

		if (CurrentVelocity.Size() < MinFlightSpeed)
		{
			SetState(EProjectileState::Destroyed);
			SubmitAccumulatedFlightLog(true);
			OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
			break;
		}

		Remaining -= dt;
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(CurrentPosition);

		if (!CurrentVelocity.IsNearlyZero())
		{
			Owner->SetActorRotation(CurrentVelocity.Rotation());
		}
	}
}

FVector UProjectileBallisticsComponent::ComputeAcceleration(const FVector& Velocity) const
{
	// Gravity (from global settings)
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	FVector Accel(0.f, 0.f, Settings->GravityZ);

	// Drag: F = 0.5 * rho * Cd * A * v^2, direction opposing velocity
	const float Speed = Velocity.Size();
	if (Speed > KINDA_SMALL_NUMBER)
	{
		const float CrossSection = PI * FMath::Square(InitialConditions.Diameter * 0.5f);
		// AirDensity, cross section, velocity, and mass use g/cm^3, cm^2, cm/s, and g.
		// Their quotient produces acceleration in cm/s^2.
		const float DragAccelMag = (0.5f * Settings->AirDensity * InitialConditions.DragCoefficient * CrossSection * Speed * Speed) / InitialConditions.Mass;

		Accel -= Velocity.GetSafeNormal() * DragAccelMag;
	}

	// Wind (treated as constant acceleration)
	Accel += InitialConditions.WindAcceleration;

	return Accel;
}

bool UProjectileBallisticsComponent::ProcessSweep(const FVector& Start, const FVector& End)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = true;

	if (!World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params))
	{
		return false;
	}

	const FBallisticMaterialResponse Material = GetMaterialResponse(Hit);
	const EImpactResponse Response = DecideImpactResponse(Hit, CurrentVelocity);
	const float SpeedAtStart = CurrentVelocity.Size();

	// Rides the projectile debug-draw cvar: names what the drawn markers actually hit.
	if (FBallisticsDebugDraw::IsProjectileDebugEnabled())
	{
		NPS_LOG(Log, TEXT("[NPS] Impact %s: actor=%s comp=%s bone=%s phys=%s dist=%.0fcm speed=%.0f"),
			*UEnum::GetValueAsString(Response),
			*GetNameSafe(Hit.GetActor()),
			*GetNameSafe(Hit.GetComponent()),
			*Hit.BoneName.ToString(),
			Hit.PhysMaterial.IsValid() ? *Hit.PhysMaterial->GetName() : TEXT("<none>"),
			FVector::Dist(Start, Hit.ImpactPoint),
			SpeedAtStart);
	}

	CurrentPosition = Hit.ImpactPoint;
	CheckFlyByListeners(Start, CurrentPosition);

	FProjectileSnapshot ImpactSnapshot = GetCurrentSnapshot();

	RecordFlightSegment(Start, Hit, Response, SpeedAtStart);

	if (UProjectileLauncherComponent* Launcher = SourceLauncher.Get())
	{
		Launcher->HandleProjectileImpactCosmetic(
			Hit,
			Response,
			ProjectileID,
			SourceProjectileData);
	}

	// Launch impact delegate after internal bookkeeping so external listeners can
	// safely destroy or pool the projectile.
	OnImpact.Broadcast(Hit, ImpactSnapshot, Response);

	FBallisticsDebugDraw::DrawImpactMarker(GetWorld(), Hit.ImpactPoint, Hit.ImpactNormal, Response);

	// Dispatch damage to the hit actor if it implements the interface
	if (AActor* HitActor = Hit.GetActor())
	{
		if (HitActor->GetClass()->ImplementsInterface(UBallisticsDamageInterface::StaticClass()))
		{
			FBallisticDamageContext DamageContext;
			DamageContext.HitResult = Hit;
			DamageContext.ImpactSnapshot = ImpactSnapshot;
			DamageContext.InitialConditions = InitialConditions;
			DamageContext.ImpactResponse = Response;
			DamageContext.KineticEnergy = 0.5f * InitialConditions.Mass * CurrentVelocity.SizeSquared() * KE_RAW_TO_JOULES;
			DamageContext.DistanceTraveled = FVector::Dist(InitialConditions.Origin, Hit.ImpactPoint);
			DamageContext.PenetrationDepth = 0.f; // Updated by HandlePenetration if applicable
			DamageContext.Instigator = GetOwner() ? GetOwner()->GetInstigator() : nullptr;
			DamageContext.ProjectileActor = GetOwner();
			DamageContext.ProjectileDataAsset = SourceProjectileData;

			IBallisticsDamageInterface::Execute_OnBallisticDamage(HitActor, DamageContext);
		}
	}

	switch (Response)
	{
	case EImpactResponse::Ricochet:
		HandleRicochet(Hit, Material);
		break;

	case EImpactResponse::Penetrate:
		HandlePenetration(Hit, Material);
		if (CurrentState == EProjectileState::Penetrating)
		{
			SubmitAccumulatedFlightLog(false);
		}
		break;

	case EImpactResponse::Embed:
		HandleEmbed(Hit);
		break;

	case EImpactResponse::Slide:
		HandleSlide(Hit, Material);
		break;

	case EImpactResponse::Shatter:
		{
			// Seed from ProjectileID so client+server agree on fragment directions.
			const float ImpactKEJoules = 0.5f * InitialConditions.Mass * CurrentVelocity.SizeSquared() * KE_RAW_TO_JOULES;
			AActor* InstigatorActor = GetOwner() ? GetOwner()->GetInstigator() : nullptr;
			UBallisticsHitscanLibrary::DispatchShatterFragments(
				this, Hit, CurrentVelocity, InitialConditions, Material,
				ImpactKEJoules, InstigatorActor, GetOwner(),
				SourceProjectileData, ProjectileID, TraceChannel);

			SetState(EProjectileState::Destroyed);
			SubmitAccumulatedFlightLog(true);
			OnProjectileDestroyed.Broadcast(ImpactSnapshot);
		}
		break;
	}

	if (Response == EImpactResponse::Embed || Response == EImpactResponse::Shatter ||
		CurrentState == EProjectileState::Embedded || CurrentState == EProjectileState::Destroyed)
	{
		SubmitAccumulatedFlightLog(true);
	}

	return true;
}

void UProjectileBallisticsComponent::RecordFlightSegment(
	const FVector& Start,
	const FHitResult& Hit,
	EImpactResponse Response,
	float SpeedAtStart)
{
	FProjectileFlightSegment& Segment = ActiveFlightLog.Segments.AddDefaulted_GetRef();
	Segment.StartLocation = Start;
	Segment.EndLocation = Hit.ImpactPoint;
	Segment.ImpactResponse = Response;
	Segment.SurfaceType = Hit.PhysMaterial.IsValid()
		? Hit.PhysMaterial->SurfaceType
		: TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType_Default);
	Segment.ImpactNormal = Hit.ImpactNormal;
	Segment.HitActor = Hit.GetActor();
	Segment.HitBoneName = Hit.BoneName;
	Segment.SpeedAtStart = SpeedAtStart;
	Segment.TimeAtImpact = ElapsedTime;
}

void UProjectileBallisticsComponent::SubmitAccumulatedFlightLog(bool bFinalReport)
{
	if (bFinalFlightLogSubmitted || ActiveFlightLog.Segments.IsEmpty() || !bIsLocallyPredicted)
	{
		return;
	}
	if (!bFinalReport &&
		ActiveFlightLog.Segments.Num() <= LastSubmittedFlightLogSegmentCount)
	{
		return;
	}

	UProjectileLauncherComponent* Launcher = SourceLauncher.Get();
	if (!Launcher || Launcher->GetActiveAuthorityMode() != ELaunchAuthorityMode::ClientAuthoritative)
	{
		return;
	}

	FProjectileFlightLog Report = ActiveFlightLog;
	Report.LaunchID = ProjectileID;
	Report.ReportSequence = NextFlightLogReportSequence++;
	Report.bFinalReport = bFinalReport;
	Launcher->SubmitFlightLog(Report);
	LastSubmittedFlightLogSegmentCount = ActiveFlightLog.Segments.Num();
	bFinalFlightLogSubmitted = bFinalReport;
}

EImpactResponse UProjectileBallisticsComponent::DecideImpactResponse(const FHitResult& Hit, const FVector& Velocity) const
{
	const FBallisticMaterialResponse Material = GetMaterialResponse(Hit);

	// 0 = parallel to surface (grazing), 90 = head-on
	const FVector VelNorm = Velocity.GetSafeNormal();
	const float DotAbs = FMath::Abs(FVector::DotProduct(VelNorm, Hit.ImpactNormal));
	const float ImpactAngleDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(DotAbs, 0.f, 1.f)));

	// Shallow angle path: try Ricochet first, fall back to Slide ("failed ricochet").
	// Arrows with MaxRicochets=0 skip straight to Slide on grazing hits. Projectiles ricochet
	// until their budget is exhausted, then slide.
	if (ImpactAngleDeg < Material.RicochetAngleThreshold)
	{
		if (RicochetCount < MaxRicochets)
		{
			return EImpactResponse::Ricochet;
		}

		const FVector TangentVel = FVector::VectorPlaneProject(Velocity, Hit.ImpactNormal);
		if (bEnableSliding && TangentVel.Size() > MinSlidingSpeed)
		{
			return EImpactResponse::Slide;
		}
		// else fall through to the energy/default branch below.
	}

	// Enough energy to punch through?
	// KE in Joules. Raw formula is 0.5 * m(g) * v^2(cm^2/s^2); scaled to J by KE_RAW_TO_JOULES.
	const float KineticEnergy = 0.5f * InitialConditions.Mass * Velocity.SizeSquared() * KE_RAW_TO_JOULES;
	if (KineticEnergy > Material.Toughness)
	{
		return EImpactResponse::Penetrate;
	}

	return Material.DefaultResponse;
}

FBallisticMaterialResponse UProjectileBallisticsComponent::GetMaterialResponse(const FHitResult& Hit) const
{
	// Resolution order:
	//   1. Component's MaterialDataAsset (also the slot the launcher writes projectile MaterialOverride into).
	//   2. Global DefaultMaterialDataAsset from project settings.
	//   3. DefaultMaterialResponse (single-response fallback).

	UBallisticMaterialDataAsset* ResolvedAsset = MaterialDataAsset;

	if (!ResolvedAsset)
	{
		if (const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get())
		{
			if (!Settings->DefaultMaterialDataAsset.IsNull())
			{
				ResolvedAsset = Cast<UBallisticMaterialDataAsset>(Settings->DefaultMaterialDataAsset.TryLoad());
			}
		}
	}

	if (ResolvedAsset)
	{
		if (const UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get())
		{
			return ResolvedAsset->GetResponseForSurface(PhysMat->SurfaceType);
		}
		return ResolvedAsset->DefaultResponse;
	}

	return DefaultMaterialResponse;
}

void UProjectileBallisticsComponent::HandleRicochet(const FHitResult& Hit, const FBallisticMaterialResponse& Material)
{
	// Reflect velocity off surface normal
	CurrentVelocity = FMath::GetReflectionVector(CurrentVelocity, Hit.ImpactNormal);

	CurrentVelocity *= Material.CoefficientOfRestitution;

	const float VarianceAngle = DeterministicRandom.FRandRange(-5.f, 5.f);
	CurrentVelocity = CurrentVelocity.RotateAngleAxis(VarianceAngle, Hit.ImpactNormal);

	RicochetCount++;
	SetState(EProjectileState::Ricocheted);

	// Nudge position slightly off the surface to avoid re-hitting immediately
	CurrentPosition += Hit.ImpactNormal * 0.5f;
}

void UProjectileBallisticsComponent::HandlePenetration(const FHitResult& Hit, const FBallisticMaterialResponse& Material)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		HandleEmbed(Hit);
		return;
	}

	// Trace backward from a point past the surface to find the exit point
	const FVector PenetrationDir = CurrentVelocity.GetSafeNormal();
	const FVector FarPoint = Hit.ImpactPoint + PenetrationDir * MaxPenetrationDepth;

	FHitResult ExitHit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = true;

	const bool bFoundExit = World->LineTraceSingleByChannel(
		ExitHit,
		FarPoint,
		Hit.ImpactPoint,
		TraceChannel,
		Params);
	const float PenetrationDistance = bFoundExit
		? FVector::DotProduct(ExitHit.ImpactPoint - Hit.ImpactPoint, PenetrationDir)
		: 0.f;
	const bool bValidExit =
		bFoundExit &&
		!ExitHit.bStartPenetrating &&
		ExitHit.GetComponent() == Hit.GetComponent() &&
		PenetrationDistance > KINDA_SMALL_NUMBER &&
		PenetrationDistance <= MaxPenetrationDepth + KINDA_SMALL_NUMBER;

	if (bValidExit)
	{
		// KE-conservation model: subtract energy proportional to (Toughness x thickness).
		// At the threshold KE == Toughness, a projectile exits with zero KE if it traverses
		// the full MaxPenetrationDepth - i.e. material thickness up to MaxPen is the
		// "equivalent" of one Toughness-worth of resistance.
		const float OldSpeed = CurrentVelocity.Size();
		const float OldKEJoules = 0.5f * InitialConditions.Mass * OldSpeed * OldSpeed * KE_RAW_TO_JOULES;
		const float KELostJoules = Material.Toughness * (PenetrationDistance / FMath::Max(MaxPenetrationDepth, 1.f));
		const float NewKEJoules = FMath::Max(0.f, OldKEJoules - KELostJoules);

		// v = sqrt(2*KE / m). Convert back from Joules into raw (g*cm^2/s^2) units before sqrt.
		const float MassDenom = FMath::Max(InitialConditions.Mass * KE_RAW_TO_JOULES, KINDA_SMALL_NUMBER);
		const float NewSpeed = FMath::Sqrt(2.f * NewKEJoules / MassDenom);

		CurrentVelocity = (OldSpeed > KINDA_SMALL_NUMBER)
			? CurrentVelocity * (NewSpeed / OldSpeed)
			: FVector::ZeroVector;
		// Offset 2 cm past the exit to avoid immediate overlap recollision.
		CurrentPosition = ExitHit.ImpactPoint + PenetrationDir * 2.0f;

		SetState(EProjectileState::Penetrating);

		// If too slow after penetration, embed instead
		if (CurrentVelocity.Size() < MinFlightSpeed)
		{
			HandleEmbed(Hit);
		}
	}
	else
	{
		// No exit within the maximum penetration depth.
		HandleEmbed(Hit);
	}
}

void UProjectileBallisticsComponent::CheckFlyByListeners(const FVector& OldPosition, const FVector& NewPosition)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Speed = CurrentVelocity.Size();
	if (Speed < MinFlyBySpeedCmS || FlyByDetectionRadius <= 0.f)
	{
		return;
	}

	UProjectileBallisticsSubsystem* Subsystem = World->GetSubsystem<UProjectileBallisticsSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	const TSet<TWeakObjectPtr<APawn>>& Listeners = Subsystem->GetFlyByListeners();
	if (Listeners.Num() == 0)
	{
		return;
	}

	const FVector Segment = NewPosition - OldPosition;
	const float SegLenSq = Segment.SizeSquared();
	if (SegLenSq < KINDA_SMALL_NUMBER)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const APawn* InstigatorPawn = Owner ? Owner->GetInstigator() : nullptr;
	const float RadiusSq = FlyByDetectionRadius * FlyByDetectionRadius;

	for (const TWeakObjectPtr<APawn>& Weak : Listeners)
	{
		APawn* Pawn = Weak.Get();
		if (!Pawn || Pawn == InstigatorPawn)
		{
			continue;
		}

		// Emit at most one fly-by per listener and projectile.
		if (FlyByTriggered.Contains(Pawn))
		{
			continue;
		}

		// This segment must cross the listener's closest-approach plane.
		const FVector P = Pawn->GetActorLocation();
		const float ProjectedDistance = FVector::DotProduct(P - OldPosition, Segment);
		if (ProjectedDistance <= 0.f || ProjectedDistance > SegLenSq)
		{
			continue;
		}
		const float T = ProjectedDistance / SegLenSq;
		const FVector Closest = OldPosition + Segment * T;

		const float DistSq = (P - Closest).SizeSquared();
		if (DistSq <= RadiusSq)
		{
			FlyByTriggered.Add(Pawn);
			if (UProjectileLauncherComponent* Launcher = SourceLauncher.Get())
			{
				Launcher->HandleProjectileFlyByCosmetic(
					Pawn,
					Closest,
					FMath::Sqrt(DistSq),
					Speed,
					CurrentVelocity.GetSafeNormal(),
					SourceProjectileData);
			}
			OnProjectileFlyBy.Broadcast(Pawn, Closest, FMath::Sqrt(DistSq), Speed);
		}
	}
}

void UProjectileBallisticsComponent::HandleEmbed(const FHitResult& Hit)
{
	CurrentVelocity = FVector::ZeroVector;
	SetState(EProjectileState::Embedded);

	if (bRestOnSurface)
	{
		CurrentPosition += Hit.ImpactNormal * GetSurfaceRestOffset();
	}

	if (AActor* Owner = GetOwner())
	{
		if (Hit.GetComponent())
		{
			Owner->AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
		}
	}

	SubmitAccumulatedFlightLog(true);
	OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
}

void UProjectileBallisticsComponent::HandleSlide(const FHitResult& Hit, const FBallisticMaterialResponse& Material)
{
	SlidingSurfaceNormal = Hit.ImpactNormal;
	const FVector VelAlongNormal = FVector::DotProduct(CurrentVelocity, SlidingSurfaceNormal) * SlidingSurfaceNormal;
	CurrentVelocity -= VelAlongNormal;

	CurrentVelocity *= Material.CoefficientOfRestitution;

	CurrentPosition += SlidingSurfaceNormal * GetSurfaceRestOffset();

	SetState(EProjectileState::Sliding);
}

void UProjectileBallisticsComponent::StepSliding(float DeltaTime)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

	const float Speed = CurrentVelocity.Size();
	if (Speed < MinSlidingSpeed)
	{
		CurrentVelocity = FVector::ZeroVector;
		SetState(EProjectileState::Embedded);
		SubmitAccumulatedFlightLog(true);
		OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
		return;
	}

	// Friction force: F = mu * m * g (opposing velocity direction)
	const float FrictionDecel = SlidingFriction * FMath::Abs(Settings->GravityZ);
	const float SpeedReduction = FrictionDecel * DeltaTime;

	if (SpeedReduction >= Speed)
	{
		CurrentVelocity = FVector::ZeroVector;
		SetState(EProjectileState::Embedded);
		SubmitAccumulatedFlightLog(true);
		OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
		return;
	}

	CurrentVelocity -= CurrentVelocity.GetSafeNormal() * SpeedReduction;

	const FVector OldPosition = CurrentPosition;
	CurrentPosition += CurrentVelocity * DeltaTime;
	ElapsedTime += DeltaTime;

	UWorld* World = GetWorld();
	if (World)
	{
		FHitResult GroundHit;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(GetOwner());

		const FVector TraceStart = CurrentPosition + SlidingSurfaceNormal * 5.f;
		const FVector TraceEnd = CurrentPosition - SlidingSurfaceNormal * 10.f;

		if (World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, TraceChannel, Params))
		{
			CurrentPosition = GroundHit.ImpactPoint + SlidingSurfaceNormal * GetSurfaceRestOffset();
			SlidingSurfaceNormal = GroundHit.ImpactNormal; // Update if surface curves
		}
		else
		{
			SetState(EProjectileState::InFlight);
			return;
		}
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(CurrentPosition);
		if (!CurrentVelocity.IsNearlyZero())
		{
			Owner->SetActorRotation(CurrentVelocity.Rotation());
		}
	}
}

void UProjectileBallisticsComponent::SetState(EProjectileState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const EProjectileState OldState = CurrentState;
	CurrentState = NewState;

	if (NewState == EProjectileState::Embedded || NewState == EProjectileState::Destroyed)
	{
		if (AActor* Owner = GetOwner())
		{
			if (UProjectileNetProxyComponent* NetProxy =
				Owner->FindComponentByClass<UProjectileNetProxyComponent>())
			{
				NetProxy->StopReplicatingTransform();
			}
		}
	}

	OnStateChanged.Broadcast(OldState, NewState);
}
