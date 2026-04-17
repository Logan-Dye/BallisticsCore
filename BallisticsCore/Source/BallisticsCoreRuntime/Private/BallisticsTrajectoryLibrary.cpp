// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "BallisticsTrajectoryLibrary.h"

FVector UBallisticsTrajectoryLibrary::ComputeAcceleration(const FProjectileInitialConditions& Conditions, const FVector& Velocity)
{
	FVector Accel(0.f, 0.f, GravityZ);

	const float Speed = Velocity.Size();
	if (Speed > KINDA_SMALL_NUMBER)
	{
		const float CrossSection = PI * FMath::Square(Conditions.Diameter * 0.5f);
		const float DragAccelMag = (0.5f * AirDensity * Conditions.DragCoefficient * CrossSection * Speed * Speed) / Conditions.Mass;
		Accel -= Velocity.GetSafeNormal() * DragAccelMag;
	}

	Accel += Conditions.WindAcceleration;
	return Accel;
}

FVector UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(const FProjectileInitialConditions& Conditions, float Time)
{
	FVector Pos = Conditions.Origin;
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.MuzzleVelocity;
	float Remaining = Time;

	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float dt = FMath::Min(StepSize, Remaining);
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
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.MuzzleVelocity;
	float Remaining = Time;

	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float dt = FMath::Min(StepSize, Remaining);
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
	FVector Vel = Conditions.Direction.GetSafeNormal() * Conditions.MuzzleVelocity;
	float Time = 0.f;
	float DistCovered = 0.f;

	while (Time < 30.f && DistCovered < Distance)
	{
		const float dt = StepSize;
		const FVector OldPos = Pos;
		const FVector Accel = ComputeAcceleration(Conditions, Vel);

		Pos += Vel * dt + 0.5f * Accel * dt * dt;
		Vel += Accel * dt;
		Time += dt;

		DistCovered += FVector::Dist(OldPos, Pos);
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
	const int32 MaxIterations = 10;
	const float AcceptableError = 5.f;

	FProjectileInitialConditions TestConditions = Conditions;

	for (int32 i = 0; i < MaxIterations; i++)
	{
		TestConditions.Direction = GuessDir;
		TestConditions.Origin = Origin;

		const float EstTime = EstimateTimeToDistance(TestConditions, Distance);
		const FVector LandingPos = EvaluatePositionAtTime(TestConditions, EstTime);
		const FVector Error = Target - LandingPos;

		if (Error.Size() < AcceptableError)
		{
			OutDirection = GuessDir;
			OutTimeOfFlight = EstTime;
			return true;
		}

		GuessDir = (ToTarget + Error).GetSafeNormal();
	}

	OutDirection = GuessDir;
	OutTimeOfFlight = EstimateTimeToDistance(TestConditions, Distance);
	return false;
}
