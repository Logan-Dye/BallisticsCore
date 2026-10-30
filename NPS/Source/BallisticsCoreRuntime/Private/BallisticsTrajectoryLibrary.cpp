// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsTrajectoryLibrary.h"
#include "BallisticsCoreSettings.h"

// Must match ProjectileBallisticsComponent's trajectory step.
static constexpr float TrajectoryStepSize = 0.005f;

FVector UBallisticsTrajectoryLibrary::ComputeAcceleration(const FProjectileInitialConditions& Conditions, const FVector& Velocity)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	FVector Accel(0.f, 0.f, Settings->GravityZ);

	const float Speed = Velocity.Size();
	if (Speed > KINDA_SMALL_NUMBER)
	{
		const float CrossSection = PI * FMath::Square(Conditions.Diameter * 0.5f);
		const float DragAccelMag = (0.5f * Settings->AirDensity * Conditions.DragCoefficient * CrossSection * Speed * Speed) / Conditions.Mass;
		Accel -= Velocity.GetSafeNormal() * DragAccelMag;
	}

	Accel += Conditions.WindAcceleration;
	return Accel;
}

FVector UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(const FProjectileInitialConditions& Conditions, float Time)
{
	FVector Pos = Conditions.Origin;
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.InitialSpeed;
	float Remaining = Time;

	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float dt = FMath::Min(TrajectoryStepSize, Remaining);
		const FVector Accel = ComputeAcceleration(Conditions, Vel);

		Pos += Vel * dt + 0.5f * Accel * dt * dt;
		Vel += Accel * dt;
		Remaining -= dt;
	}

	return Pos;
}

FProjectileSnapshot UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(const FProjectileInitialConditions& Conditions, float Time)
{
	FVector Pos = Conditions.Origin;
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.InitialSpeed;
	float Remaining = Time;

	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float dt = FMath::Min(TrajectoryStepSize, Remaining);
		const FVector Accel = ComputeAcceleration(Conditions, Vel);

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

float UBallisticsTrajectoryLibrary::EstimateTimeToDistance(const FProjectileInitialConditions& Conditions, float Distance)
{
	FVector Pos = Conditions.Origin;
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.InitialSpeed;
	float Time = 0.f;
	float DistCovered = 0.f;

	while (Time < 30.f && DistCovered < Distance)
	{
		const float dt = TrajectoryStepSize;
		const FVector OldPos = Pos;
		const FVector Accel = ComputeAcceleration(Conditions, Vel);

		const FVector NextPos = Pos + Vel * dt + 0.5f * Accel * dt * dt;
		const float StepDistance = FVector::Dist(OldPos, NextPos);
		if (DistCovered + StepDistance >= Distance)
		{
			const float Alpha = (Distance - DistCovered) / FMath::Max(StepDistance, KINDA_SMALL_NUMBER);
			return Time + dt * FMath::Clamp(Alpha, 0.f, 1.f);
		}

		Pos = NextPos;
		Vel += Accel * dt;
		Time += dt;
		DistCovered += StepDistance;
	}

	return Time;
}

bool UBallisticsTrajectoryLibrary::SolveLaunchDirection(
	const FVector& Origin,
	const FVector& Target,
	const FProjectileInitialConditions& Conditions,
	FVector& OutDirection,
	float& OutTimeOfFlight)
{
	const FVector ToTarget = Target - Origin;
	const float Distance = ToTarget.Size();

	if (Distance < KINDA_SMALL_NUMBER)
	{
		OutDirection = FVector::ForwardVector;
		OutTimeOfFlight = 0.f;
		return true;
	}

	FVector GuessDir = ToTarget.GetSafeNormal();
	const int32 MaxIterations = 32;
	const float AcceptableError = 5.f;

	FProjectileInitialConditions TestConditions = Conditions;
	float EstTime = FMath::Max(
		Distance / FMath::Max(Conditions.InitialSpeed, 1.f),
		TrajectoryStepSize);

	for (int32 i = 0; i < MaxIterations; ++i)
	{
		TestConditions.Direction = GuessDir;
		TestConditions.Origin = Origin;

		EstTime = EstimateTimeToDistance(TestConditions, Distance);
		const FVector LandingPos = EvaluatePositionAtTime(TestConditions, EstTime);
		const FVector Error = Target - LandingPos;

		if (Error.Size() < AcceptableError)
		{
			OutDirection = GuessDir;
			OutTimeOfFlight = EstTime;
			return true;
		}

		const FVector CorrectedVelocity =
			GuessDir * Conditions.InitialSpeed +
			Error / FMath::Max(EstTime, TrajectoryStepSize);
		GuessDir = CorrectedVelocity.GetSafeNormal();
	}

	// Failed to converge - return best guess
	OutDirection = GuessDir;
	TestConditions.Direction = GuessDir;
	OutTimeOfFlight = EstimateTimeToDistance(TestConditions, Distance);
	return false;
}
