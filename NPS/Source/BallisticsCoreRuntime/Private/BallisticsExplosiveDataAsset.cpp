// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsExplosiveDataAsset.h"

float UBallisticsExplosiveDataAsset::CalculateBlastDamageAtDistance(float Distance) const
{
	if (!bEnableBlastDamage || Distance > BlastRadius)
	{
		return 0.f;
	}

	if (Distance <= BlastInnerRadius)
	{
		return BlastDamage;
	}

	// Linear falloff between inner and outer radius
	const float FalloffRange = BlastRadius - BlastInnerRadius;
	if (FalloffRange <= KINDA_SMALL_NUMBER)
	{
		return BlastDamage;
	}

	const float Alpha = (Distance - BlastInnerRadius) / FalloffRange;
	return FMath::Lerp(BlastDamage, BlastMinDamage, Alpha);
}

float UBallisticsExplosiveDataAsset::CalculateShrapnelDamageAtDistance(float Distance) const
{
	if (!bEnableShrapnel || Distance > ShrapnelRange || ShrapnelRange <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	const float Alpha = Distance / ShrapnelRange;
	return FMath::Lerp(ShrapnelDamage, 0.f, Alpha);
}

FHitscanConfig UBallisticsExplosiveDataAsset::BuildShrapnelHitscanConfig(
	int32 Seed,
	const TArray<TObjectPtr<AActor>>& IgnoredActors) const
{
	FHitscanConfig Config;
	Config.MaxRange = ShrapnelRange;
	Config.SimulatedMass = ShrapnelMass;
	Config.SimulatedInitialSpeed = ShrapnelInitialSpeed;
	Config.MaxRicochets = ShrapnelMaxRicochets;
	Config.MaxPenetrations = ShrapnelMaxPenetrations;
	Config.MaxPenetrationDepth = 15.f; // Shrapnel is small - thin penetration
	Config.TraceChannel = ShrapnelTraceChannel;
	Config.MaterialDataAsset = ShrapnelMaterialDataAsset;
	Config.IgnoredActors = IgnoredActors;
	Config.RandomSeed = Seed;
	return Config;
}
