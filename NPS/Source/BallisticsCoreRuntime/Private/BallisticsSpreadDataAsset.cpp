// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsSpreadDataAsset.h"
#include "BallisticsTypes.h"

void UBallisticsSpreadDataAsset::GenerateProjectileDirections(
	const FVector& ForwardDirection,
	FRandomStream& RandomStream,
	TArray<FVector>& OutDirections) const
{
	OutDirections.Reset();
	const FVector Forward = ForwardDirection.GetSafeNormal();

	switch (SpreadMode)
	{
	case ESpreadMode::RandomCone:
	{
		for (int32 i = 0; i < ProjectileCount; ++i)
		{
			OutDirections.Add(RandomDirectionInCone(Forward, ConeHalfAngle, RandomStream));
		}
		break;
	}

	case ESpreadMode::FixedPattern:
	{
		for (const FSpreadPatternPoint& Point : Pattern)
		{
			OutDirections.Add(ApplyPatternOffset(Forward, Point));
		}
		break;
	}

	case ESpreadMode::HybridPattern:
	{
		for (const FSpreadPatternPoint& Point : Pattern)
		{
			// Start with the fixed offset
			FVector Dir = ApplyPatternOffset(Forward, Point);

			if (PatternVarianceDegrees > KINDA_SMALL_NUMBER)
			{
				Dir = RandomDirectionInCone(Dir, PatternVarianceDegrees, RandomStream);
			}

			OutDirections.Add(Dir);
		}
		break;
	}
	}
}

void UBallisticsSpreadDataAsset::GenerateProjectileConditions(
	const FProjectileInitialConditions& BaseConditions,
	const FVector& LaunchDirection,
	FRandomStream& RandomStream,
	TArray<FProjectileInitialConditions>& OutConditionsArray) const
{
	TArray<FVector> Directions;
	GenerateProjectileDirections(LaunchDirection, RandomStream, Directions);

	OutConditionsArray.Reset();
	OutConditionsArray.Reserve(Directions.Num());

	for (int32 i = 0; i < Directions.Num(); ++i)
	{
		FProjectileInitialConditions ProjectileConditions = BaseConditions;
		ProjectileConditions.Direction = Directions[i];
		ProjectileConditions.RandomSeed = BaseConditions.RandomSeed ^ (i * 2654435761u);
		FRandomStream VariationStream(ProjectileConditions.RandomSeed);

		if (bVaryInitialSpeed && InitialSpeedVariationPercent > KINDA_SMALL_NUMBER)
		{
			const float SpeedVariation = VariationStream.FRandRange(
				-InitialSpeedVariationPercent,
				InitialSpeedVariationPercent);
			ProjectileConditions.InitialSpeed *= (1.f + SpeedVariation);
		}

		if (bVaryProjectileMass && MassVariationPercent > KINDA_SMALL_NUMBER)
		{
			const float MassVariation = VariationStream.FRandRange(
				-MassVariationPercent,
				MassVariationPercent);
			ProjectileConditions.Mass *= (1.f + MassVariation);
		}

		OutConditionsArray.Add(ProjectileConditions);
	}
}

FVector UBallisticsSpreadDataAsset::RandomDirectionInCone(const FVector& Forward, float HalfAngleDeg, FRandomStream& Stream) const
{
	if (HalfAngleDeg <= KINDA_SMALL_NUMBER)
	{
		return Forward;
	}

	// Sample the cone's solid angle uniformly to avoid center clustering.
	const float HalfAngleRad = FMath::DegreesToRadians(HalfAngleDeg);

	const float CosHalfAngle = FMath::Cos(HalfAngleRad);
	const float Z = Stream.FRandRange(CosHalfAngle, 1.f);
	const float Phi = Stream.FRandRange(0.f, 2.f * PI);
	const float SinTheta = FMath::Sqrt(1.f - Z * Z);

	// Unreal's forward axis is X.
	const FVector LocalDir(Z, SinTheta * FMath::Cos(Phi), SinTheta * FMath::Sin(Phi));

	// Rotate from local space to world space aligned with Forward
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Forward);
	return Rotation.RotateVector(LocalDir).GetSafeNormal();
}

FVector UBallisticsSpreadDataAsset::ApplyPatternOffset(const FVector& Forward, const FSpreadPatternPoint& Point) const
{
	if (FMath::IsNearlyZero(Point.YawOffset) && FMath::IsNearlyZero(Point.PitchOffset))
	{
		return Forward;
	}

	// Convert forward direction to a rotator, apply offsets, convert back
	FRotator BaseRotation = Forward.Rotation();
	BaseRotation.Yaw += Point.YawOffset;
	BaseRotation.Pitch += Point.PitchOffset;

	return BaseRotation.Vector().GetSafeNormal();
}
