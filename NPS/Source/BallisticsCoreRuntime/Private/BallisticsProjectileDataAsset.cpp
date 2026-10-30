// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsProjectileDataAsset.h"

FProjectileInitialConditions UBallisticsProjectileDataAsset::ApplyToConditions(const FProjectileInitialConditions& BaseConditions) const
{
	FProjectileInitialConditions Result = BaseConditions;

	Result.InitialSpeed = InitialSpeed;
	Result.Mass = Mass;
	Result.Diameter = Diameter;
	Result.DragCoefficient = DragCoefficient;
	Result.WindAcceleration = WindAcceleration;

	return Result;
}

float UBallisticsProjectileDataAsset::CalculateDamageAtDistance(float Distance) const
{
	if (Distance <= DamageFalloffStartDistance)
	{
		return BaseDamage;
	}

	if (Distance >= DamageFalloffEndDistance)
	{
		return BaseDamage * DamageFalloffMultiplier;
	}

	const float FalloffRange = DamageFalloffEndDistance - DamageFalloffStartDistance;
	if (FalloffRange <= KINDA_SMALL_NUMBER)
	{
		return BaseDamage;
	}

	const float FalloffAlpha = (Distance - DamageFalloffStartDistance) / FalloffRange;
	return FMath::Lerp(BaseDamage, BaseDamage * DamageFalloffMultiplier, FalloffAlpha);
}

bool UBallisticsProjectileDataAsset::IsSurfaceBlacklisted(TEnumAsByte<EPhysicalSurface> Surface) const
{
	return EmbedBlacklist.Contains(Surface);
}

bool UBallisticsProjectileDataAsset::ShouldShowTracer(int32 LaunchCounter) const
{
	if (!bUseTracerInterval)
	{
		// Every launch uses a tracer.
		return true;
	}

	if (TracerInterval <= 0)
	{
		return true;
	}

	return (LaunchCounter % TracerInterval) == 0;
}
