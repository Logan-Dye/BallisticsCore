// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

UProjectileBallisticsComponent::UProjectileBallisticsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);

	ElapsedTime = 0.f;
	CurrentState = EProjectileState::InFlight;
	CurrentPosition = FVector::ZeroVector;
	CurrentVelocity = FVector::ZeroVector;
	RicochetCount = 0;
}

void UProjectileBallisticsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProjectileBallisticsComponent, InitialConditions);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

void UProjectileBallisticsComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem = World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			Subsystem->RegisterProjectile(this);
		}
	}
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

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

void UProjectileBallisticsComponent::InitializeProjectile(const FProjectileInitialConditions& Conditions)
{
	InitialConditions = Conditions;
	InitialConditions.Direction = Conditions.Direction.GetSafeNormal();

	CurrentPosition = Conditions.Origin;
	CurrentVelocity = InitialConditions.Direction * Conditions.MuzzleVelocity;
	ElapsedTime = 0.f;
	RicochetCount = 0;

	DeterministicRandom.Initialize(Conditions.RandomSeed);

	SetState(EProjectileState::InFlight);

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(CurrentPosition);
	}
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
	// Pure math evaluation — no collision, no state changes
	// Steps through the trajectory using the same math as StepTrajectory
	// but without side effects

	FVector Pos = InitialConditions.Origin;
	FVector Vel = InitialConditions.Direction * InitialConditions.MuzzleVelocity;
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

// ─────────────────────────────────────────────
//  Tick
// ─────────────────────────────────────────────

void UProjectileBallisticsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsInFlight())
	{
		return;
	}

	StepTrajectory(DeltaTime);
}

// ─────────────────────────────────────────────
//  Trajectory Stepping
// ─────────────────────────────────────────────

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

		// Sweep for collisions between old and new position
		if (ProcessSweep(OldPosition, CurrentPosition))
		{
			// Hit something — ProcessSweep handled the response
			// Position and velocity are already updated by the handler
			break;
		}

		// Check if projectile has slowed below minimum flight speed
		if (CurrentVelocity.Size() < MinFlightSpeed)
		{
			SetState(EProjectileState::Destroyed);
			OnProjectileDestroyed.Broadcast(GetCurrentSnapshot());
			break;
		}

		Remaining -= dt;
	}

	// Update actor position
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
	// Gravity (hardcoded constant)
	FVector Accel(0.f, 0.f, GravityZ);

	// Drag: F = 0.5 * rho * Cd * A * v^2, direction opposing velocity
	const float Speed = Velocity.Size();
	if (Speed > KINDA_SMALL_NUMBER)
	{
		const float CrossSection = PI * FMath::Square(InitialConditions.Diameter * 0.5f);
		const float MassKg = InitialConditions.Mass * 0.001f; // grams to kg... but we're in cm so:
		// Actually: AirDensity is g/cm^3, CrossSection is cm^2, Speed is cm/s
		// DragForce (g*cm/s^2) = 0.5 * rho * Cd * A * v^2
		// DragAccel (cm/s^2) = DragForce / Mass(g)
		const float DragAccelMag = (0.5f * AirDensity * InitialConditions.DragCoefficient * CrossSection * Speed * Speed) / InitialConditions.Mass;

		Accel -= Velocity.GetSafeNormal() * DragAccelMag;
	}

	// Wind (treated as constant acceleration)
	Accel += InitialConditions.WindAcceleration;

	return Accel;
}

// ─────────────────────────────────────────────
//  Collision
// ─────────────────────────────────────────────

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

	// We hit something — figure out what to do
	const FBallisticMaterialResponse Material = GetMaterialResponse(Hit);
	const EImpactResponse Response = DecideImpactResponse(Hit, CurrentVelocity);

	// Move to the impact point
	CurrentPosition = Hit.ImpactPoint;

	// Build snapshot at impact for the delegate
	FProjectileSnapshot ImpactSnapshot = GetCurrentSnapshot();

	// Fire impact delegate
	OnImpact.Broadcast(Hit, ImpactSnapshot, Response);

	// Handle the response
	switch (Response)
	{
	case EImpactResponse::Ricochet:
		HandleRicochet(Hit, Material);
		break;

	case EImpactResponse::Penetrate:
		HandlePenetration(Hit, Material);
		break;

	case EImpactResponse::Embed:
		HandleEmbed(Hit);
		break;

	case EImpactResponse::Shatter:
		SetState(EProjectileState::Destroyed);
		OnProjectileDestroyed.Broadcast(ImpactSnapshot);
		break;
	}

	return true;
}

EImpactResponse UProjectileBallisticsComponent::DecideImpactResponse(const FHitResult& Hit, const FVector& Velocity) const
{
	const FBallisticMaterialResponse Material = GetMaterialResponse(Hit);

	// Compute impact angle (angle between velocity and surface)
	// 0 = parallel to surface (grazing), 90 = head-on
	const FVector VelNorm = Velocity.GetSafeNormal();
	const float DotProduct = FMath::Abs(FVector::DotProduct(VelNorm, Hit.ImpactNormal));
	const float ImpactAngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(1.f - DotProduct, 0.f, 1.f)));

	// Shallow angle → ricochet
	if (ImpactAngleDeg < Material.RicochetAngleThreshold && RicochetCount < MaxRicochets)
	{
		return EImpactResponse::Ricochet;
	}

	// Enough energy to punch through?
	// KE = 0.5 * m * v^2 (using grams and cm/s, gives g*cm^2/s^2)
	const float KineticEnergy = 0.5f * InitialConditions.Mass * Velocity.SizeSquared();
	if (KineticEnergy > Material.Toughness)
	{
		return EImpactResponse::Penetrate;
	}

	return Material.DefaultResponse;
}

FBallisticMaterialResponse UProjectileBallisticsComponent::GetMaterialResponse(const FHitResult& Hit) const
{
	// Always use the default material response
	return DefaultMaterialResponse;
}

// ─────────────────────────────────────────────
//  Impact Handlers
// ─────────────────────────────────────────────

void UProjectileBallisticsComponent::HandleRicochet(const FHitResult& Hit, const FBallisticMaterialResponse& Material)
{
	// Reflect velocity off surface normal
	CurrentVelocity = FMath::GetReflectionVector(CurrentVelocity, Hit.ImpactNormal);

	// Apply energy loss
	CurrentVelocity *= Material.CoefficientOfRestitution;

	// Add some deterministic variance so ricochets don't look robotic
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

	// Trace backward from far point to impact point
	if (World->LineTraceSingleByChannel(ExitHit, FarPoint, Hit.ImpactPoint, TraceChannel, Params))
	{
		// Found exit point
		const float PenetrationDistance = FVector::Dist(Hit.ImpactPoint, ExitHit.ImpactPoint);

		// Reduce velocity based on distance traveled through material and toughness
		// More toughness + more distance = more energy loss
		const float EnergyLossRatio = FMath::Clamp(
			(PenetrationDistance * Material.Toughness) / (MaxPenetrationDepth * 1000.f),
			0.f, 0.95f
		);

		CurrentVelocity *= (1.f - EnergyLossRatio);
		CurrentPosition = ExitHit.ImpactPoint + PenetrationDir * 0.5f;

		SetState(EProjectileState::Penetrating);

		// If too slow after penetration, embed instead
		if (CurrentVelocity.Size() < MinFlightSpeed)
		{
			HandleEmbed(Hit);
		}
	}
	else
	{
		// Couldn't find exit — object is thicker than max penetration depth
		HandleEmbed(Hit);
	}
}

void UProjectileBallisticsComponent::HandleEmbed(const FHitResult& Hit)
{
	CurrentVelocity = FVector::ZeroVector;
	SetState(EProjectileState::Embedded);

	// Attach to the hit component so we move with it (e.g. arrow stuck in a character)
	if (AActor* Owner = GetOwner())
	{
		if (Hit.GetComponent())
		{
			Owner->AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
		}
	}
}

// ─────────────────────────────────────────────
//  State Management
// ─────────────────────────────────────────────

void UProjectileBallisticsComponent::SetState(EProjectileState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const EProjectileState OldState = CurrentState;
	CurrentState = NewState;
	OnStateChanged.Broadcast(OldState, NewState);

	// Stop ticking if we're done
	if (NewState == EProjectileState::Embedded || NewState == EProjectileState::Destroyed)
	{
		SetComponentTickEnabled(false);
	}
}
