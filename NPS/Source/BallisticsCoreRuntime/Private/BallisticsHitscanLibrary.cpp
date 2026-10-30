// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsHitscanLibrary.h"
#include "GameFramework/Pawn.h"
#include "BallisticsDebugDraw.h"
#include "BallisticsDamageInterface.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "DrawDebugHelpers.h"

FHitscanResult UBallisticsHitscanLibrary::PerformHitscan(
	const UObject* WorldContext,
	const FVector& Origin,
	const FVector& Direction,
	const FHitscanConfig& Config)
{
	FHitscanResult Result;

	if (!WorldContext)
	{
		return Result;
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return Result;
	}

	const FVector NormalizedDir = Direction.GetSafeNormal();
	if (NormalizedDir.IsNearlyZero())
	{
		return Result;
	}

	FCollisionQueryParams Params;
	Params.bReturnPhysicalMaterial = true;
	for (AActor* Ignored : Config.IgnoredActors)
	{
		if (Ignored)
		{
			Params.AddIgnoredActor(Ignored);
		}
	}

	// Deterministic random for ricochet variance
	FRandomStream Random(Config.RandomSeed);

	FVector CurrentOrigin = Origin;
	FVector CurrentDirection = NormalizedDir;
	float RemainingRange = Config.MaxRange;
	float CurrentEnergy = 0.5f * Config.SimulatedMass * Config.SimulatedInitialSpeed * Config.SimulatedInitialSpeed * KE_RAW_TO_JOULES;
	const float InitialEnergy = CurrentEnergy;
	int32 RicochetCount = 0;
	int32 PenetrationCount = 0;
	int32 SegmentIndex = 0;

	Result.PathPoints.Add(CurrentOrigin);

	// Trace loop - each iteration is one segment (initial, post-ricochet, or post-penetration)
	while (RemainingRange > KINDA_SMALL_NUMBER && CurrentEnergy > KINDA_SMALL_NUMBER)
	{
		const FVector TraceEnd = CurrentOrigin + CurrentDirection * RemainingRange;

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, CurrentOrigin, TraceEnd, Config.TraceChannel, Params))
		{
			Result.PathPoints.Add(TraceEnd);
			Result.TotalDistance += RemainingRange;
			break;
		}

		const float SegmentDistance = FVector::Dist(CurrentOrigin, Hit.ImpactPoint);
		RemainingRange -= SegmentDistance;
		Result.TotalDistance += SegmentDistance;
		Result.PathPoints.Add(Hit.ImpactPoint);

		const FBallisticMaterialResponse Material = GetMaterialResponse(Hit, Config);
		const EImpactResponse Response = DecideImpactResponse(
			Hit, CurrentDirection, CurrentEnergy, Material,
			RicochetCount, Config.MaxRicochets
		);

		FHitscanHit HitEntry;
		HitEntry.HitResult = Hit;
		HitEntry.ImpactResponse = Response;
		HitEntry.MaterialResponse = Material;
		HitEntry.RemainingEnergyFraction = (InitialEnergy > KINDA_SMALL_NUMBER)
			? (CurrentEnergy / InitialEnergy) : 0.f;
		HitEntry.SegmentIndex = SegmentIndex;
		Result.Hits.Add(HitEntry);

		switch (Response)
		{
		case EImpactResponse::Ricochet:
		{
			// Reflect direction off surface
			CurrentDirection = FMath::GetReflectionVector(CurrentDirection, Hit.ImpactNormal);
			CurrentEnergy *= Material.CoefficientOfRestitution;

			// Deterministic variance
			if (Config.RandomSeed != 0)
			{
				const float VarianceAngle = Random.FRandRange(-5.f, 5.f);
				CurrentDirection = CurrentDirection.RotateAngleAxis(VarianceAngle, Hit.ImpactNormal);
			}
			CurrentDirection = CurrentDirection.GetSafeNormal();

			CurrentOrigin = Hit.ImpactPoint + Hit.ImpactNormal * 0.5f;
			RicochetCount++;
			Result.RicochetCount++;
			SegmentIndex++;
			break;
		}

		case EImpactResponse::Penetrate:
		{
			if (PenetrationCount >= Config.MaxPenetrations)
			{
				Result.bFullyStopped = true;
				break;
			}

			const FVector FarPoint = Hit.ImpactPoint + CurrentDirection * Config.MaxPenetrationDepth;
			FHitResult ExitHit;

			if (World->LineTraceSingleByChannel(ExitHit, FarPoint, Hit.ImpactPoint, Config.TraceChannel, Params))
			{
				const float PenDistance = FVector::Dist(Hit.ImpactPoint, ExitHit.ImpactPoint);
				// KE-conservation: subtract Toughness x (thickness / MaxPenDepth) joules.
				const float KELost = Material.Toughness * (PenDistance / FMath::Max(Config.MaxPenetrationDepth, 1.f));
				CurrentEnergy = FMath::Max(0.f, CurrentEnergy - KELost);
				CurrentOrigin = ExitHit.ImpactPoint + CurrentDirection * 0.5f;
				RemainingRange -= PenDistance;

				Result.PathPoints.Add(ExitHit.ImpactPoint);
			}
			else
			{
				// Object too thick - stopped inside
				Result.bFullyStopped = true;
				break;
			}

			PenetrationCount++;
			Result.PenetrationCount++;
			SegmentIndex++;
			break;
		}

		case EImpactResponse::Embed:
		case EImpactResponse::Shatter:
		case EImpactResponse::Slide: // Hitscans don't physically slide - treat as terminal.
		default:
			Result.bFullyStopped = true;
			break;
		}

		if (Result.bFullyStopped)
		{
			break;
		}
	}

	FBallisticsDebugDraw::DrawHitscanResult(World, Result);

	return Result;
}

TArray<FHitscanResult> UBallisticsHitscanLibrary::PerformRadialHitscan(
	const UObject* WorldContext,
	const FVector& Origin,
	int32 TraceCount,
	const FHitscanConfig& Config)
{
	TArray<FHitscanResult> Results;

	if (TraceCount <= 0)
	{
		return Results;
	}

	FRandomStream Random(Config.RandomSeed);
	TArray<FVector> Directions = GenerateSphericalDirections(TraceCount, Random);

	Results.Reserve(TraceCount);

	for (int32 i = 0; i < Directions.Num(); ++i)
	{
		// Each shrapnel trace gets its own deterministic seed derived from the base seed
		FHitscanConfig TraceConfig = Config;
		TraceConfig.RandomSeed = Config.RandomSeed + i + 1;

		Results.Add(PerformHitscan(WorldContext, Origin, Directions[i], TraceConfig));
	}

	return Results;
}

void UBallisticsHitscanLibrary::DispatchShatterFragments(
	const UObject* WorldContext,
	const FHitResult& Hit,
	const FVector& IncomingVelocity,
	const FProjectileInitialConditions& Conditions,
	const FBallisticMaterialResponse& Material,
	float KineticEnergyJ,
	AActor* InstigatorActor,
	AActor* ProjectileActor,
	UBallisticsProjectileDataAsset* ProjectileDataAsset,
	int32 RandomSeed,
	ECollisionChannel TraceChannel)
{
	if (!WorldContext)
	{
		return;
	}
	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return;
	}

	// Severity: how much the material "beat" the projectile. 0 = projectile nearly penetrated, 1 = vastly underpowered.
	const float Severity = (Material.Toughness > KINDA_SMALL_NUMBER)
		? FMath::Clamp((Material.Toughness - KineticEnergyJ) / Material.Toughness, 0.f, 1.f)
		: 0.f;

	const int32 FragmentCount = FMath::RoundToInt(FMath::Lerp<float>(
		static_cast<float>(Material.ShatterMinFragments),
		static_cast<float>(Material.ShatterMaxFragments),
		Severity));

	if (FragmentCount <= 0)
	{
		return;
	}

	// Cone base direction: blend between backsplash (toward shooter) and surface tangent (slide along the plate).
	const FVector VelDir = IncomingVelocity.GetSafeNormal();
	const FVector ImpactNormal = FVector(Hit.ImpactNormal);
	const FVector BackSplash = VelDir.IsNearlyZero() ? ImpactNormal : -VelDir;
	FVector SlideDir = FVector::VectorPlaneProject(VelDir, ImpactNormal).GetSafeNormal();
	if (SlideDir.IsNearlyZero())
	{
		// Pure head-on hit - no tangent direction exists; fall back to backsplash.
		SlideDir = BackSplash;
	}
	FVector ConeBase = FMath::Lerp(BackSplash, SlideDir, FMath::Clamp(Material.ShatterSlideBias, 0.f, 1.f));
	if (!ConeBase.Normalize())
	{
		ConeBase = ImpactNormal;
	}

	const float HalfConeRad = FMath::DegreesToRadians(Material.ShatterConeAngleDeg * 0.5f);
	const float FragmentSpeed = IncomingVelocity.Size() * Material.ShatterSpeedFraction;
	const float FragmentMass = Conditions.Mass * Material.ShatterFragmentMassFraction;
	const float FragmentKEJoules = 0.5f * FragmentMass * FragmentSpeed * FragmentSpeed * KE_RAW_TO_JOULES;

	FRandomStream Random(RandomSeed);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BallisticsShatterFragment), true);
	Params.AddIgnoredActor(ProjectileActor);
	if (InstigatorActor)
	{
		Params.AddIgnoredActor(InstigatorActor);
	}
	Params.bReturnPhysicalMaterial = true;

	for (int32 i = 0; i < FragmentCount; ++i)
	{
		const FVector FragDir = Random.VRandCone(ConeBase, HalfConeRad);
		const FVector FragStart = Hit.ImpactPoint + FragDir; // 1 cm offset so trace doesn't start inside the surface
		const FVector FragEnd = FragStart + FragDir * Material.ShatterMaxRange;

		FHitResult FragHit;
		const bool bHit = World->LineTraceSingleByChannel(FragHit, FragStart, FragEnd, TraceChannel, Params);

#if ENABLE_DRAW_DEBUG
		if (FBallisticsDebugDraw::IsProjectileDebugEnabled())
		{
			DrawDebugLine(
				World,
				FragStart,
				bHit ? FragHit.ImpactPoint : FragEnd,
				FColor(255, 80, 0),
				false,
				1.5f,
				0,
				0.5f);
		}
#endif

		if (!bHit)
		{
			continue;
		}

		AActor* FragTarget = FragHit.GetActor();
		if (!FragTarget || !FragTarget->GetClass()->ImplementsInterface(UBallisticsDamageInterface::StaticClass()))
		{
			continue;
		}

		FBallisticDamageContext FragContext;
		FragContext.HitResult = FragHit;
		FragContext.InitialConditions = Conditions;
		FragContext.ImpactResponse = EImpactResponse::Penetrate; // fragments perforate soft tissue
		FragContext.KineticEnergy = FragmentKEJoules;
		FragContext.DistanceTraveled = FVector::Dist(Conditions.Origin, FragHit.ImpactPoint);
		FragContext.Instigator = Cast<APawn>(InstigatorActor);
		FragContext.ProjectileActor = ProjectileActor;
		FragContext.ProjectileDataAsset = ProjectileDataAsset;
		FragContext.bIsFragment = true;

		IBallisticsDamageInterface::Execute_OnBallisticDamage(FragTarget, FragContext);
	}
}

FBallisticMaterialResponse UBallisticsHitscanLibrary::GetMaterialResponse(
	const FHitResult& Hit,
	const FHitscanConfig& Config)
{
	if (Config.MaterialDataAsset)
	{
		if (const UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get())
		{
			return Config.MaterialDataAsset->GetResponseForSurface(PhysMat->SurfaceType);
		}
		return Config.MaterialDataAsset->DefaultResponse;
	}

	return Config.DefaultMaterialResponse;
}

EImpactResponse UBallisticsHitscanLibrary::DecideImpactResponse(
	const FHitResult& Hit,
	const FVector& TraceDirection,
	float CurrentEnergy,
	const FBallisticMaterialResponse& Material,
	int32 RicochetCount,
	int32 MaxRicochets)
{
	// Impact angle: 0 = grazing, 90 = head-on.
	const float DotAbs = FMath::Abs(FVector::DotProduct(TraceDirection, Hit.ImpactNormal));
	const float ImpactAngleDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(DotAbs, 0.f, 1.f)));

	// Shallow angle -> ricochet
	if (ImpactAngleDeg < Material.RicochetAngleThreshold && RicochetCount < MaxRicochets)
	{
		return EImpactResponse::Ricochet;
	}

	// Enough energy to punch through?
	if (CurrentEnergy > Material.Toughness)
	{
		return EImpactResponse::Penetrate;
	}

	return Material.DefaultResponse;
}

TArray<FVector> UBallisticsHitscanLibrary::GenerateSphericalDirections(int32 Count, FRandomStream& Random)
{
	TArray<FVector> Directions;
	Directions.Reserve(Count);

	// Uniform distribution on a sphere using Marsaglia's method
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Dir;
		float LenSq;

		do
		{
			Dir.X = Random.FRandRange(-1.f, 1.f);
			Dir.Y = Random.FRandRange(-1.f, 1.f);
			Dir.Z = Random.FRandRange(-1.f, 1.f);
			LenSq = Dir.SizeSquared();
		} while (LenSq > 1.f || LenSq < KINDA_SMALL_NUMBER);

		Directions.Add(Dir.GetSafeNormal());
	}

	return Directions;
}
