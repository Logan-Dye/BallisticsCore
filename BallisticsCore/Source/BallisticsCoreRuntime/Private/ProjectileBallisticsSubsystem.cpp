// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileBallisticsComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

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

	constexpr float Gravity = -980.f;
	constexpr float Air = 0.001225f;
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
