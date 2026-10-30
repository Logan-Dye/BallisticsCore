// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsDebugDraw.h"

DEFINE_LOG_CATEGORY(LogNPS);

#if !UE_BUILD_SHIPPING

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
// Console Variables

static TAutoConsoleVariable<int32> CVarNPSDebugProjectiles(
	TEXT("NPS.Debug.Projectiles"),
	0,
	TEXT("Draw projectile paths, velocity arrows, and impact markers.\n")
	TEXT("0 = off, 1 = on"),
	ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarNPSDebugHitscan(
	TEXT("NPS.Debug.Hitscan"),
	0,
	TEXT("Draw hitscan trace paths and hit markers.\n")
	TEXT("0 = off, 1 = on"),
	ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarNPSDebugDetonation(
	TEXT("NPS.Debug.Detonation"),
	0,
	TEXT("Draw blast radii, affected actors, and applied damage.\n")
	TEXT("0 = off, 1 = on"),
	ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarNPSDebugExplosionsAlias(
	TEXT("NPS.Debug.Explosions"),
	0,
	TEXT("Alias for NPS.Debug.Detonation."),
	ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarNPSDebugBudget(
	TEXT("NPS.Debug.Budget"),
	0,
	TEXT("Draw on-screen impact budget stats.\n")
	TEXT("0 = off, 1 = on"),
	ECVF_Cheat
);

// Duration for debug draw persistence
static constexpr float DebugDrawDuration = 3.f;
static constexpr float ImpactMarkerSize = 8.f;
static constexpr int32 SphereSegments = 16;
// Console Commands

static FAutoConsoleCommand CmdNPSDebugAll(
	TEXT("NPS.Debug.All"),
	TEXT("Toggle all NPS debug drawing on/off."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const bool bAnyOn =
			CVarNPSDebugProjectiles.GetValueOnGameThread() ||
			CVarNPSDebugHitscan.GetValueOnGameThread() ||
			CVarNPSDebugDetonation.GetValueOnGameThread() ||
			CVarNPSDebugExplosionsAlias.GetValueOnGameThread() ||
			CVarNPSDebugBudget.GetValueOnGameThread();

		const int32 NewValue = bAnyOn ? 0 : 1;
		CVarNPSDebugProjectiles->Set(NewValue);
		CVarNPSDebugHitscan->Set(NewValue);
		CVarNPSDebugDetonation->Set(NewValue);
		CVarNPSDebugExplosionsAlias->Set(NewValue);
		CVarNPSDebugBudget->Set(NewValue);

		UE_LOG(LogNPS, Log, TEXT("NPS Debug: All %s"), NewValue ? TEXT("ON") : TEXT("OFF"));
	}),
	ECVF_Cheat
);

void FBallisticsDebugDraw::RegisterConsoleCommands()
{
	// CVars and commands are auto-registered via static constructors above.
	// The module calls this entry point to retain the translation unit.
}

bool FBallisticsDebugDraw::IsProjectileDebugEnabled()
{
	return CVarNPSDebugProjectiles.GetValueOnGameThread() != 0;
}

bool FBallisticsDebugDraw::IsHitscanDebugEnabled()
{
	return CVarNPSDebugHitscan.GetValueOnGameThread() != 0;
}

bool FBallisticsDebugDraw::IsDetonationDebugEnabled()
{
	return CVarNPSDebugDetonation.GetValueOnGameThread() != 0 ||
		CVarNPSDebugExplosionsAlias.GetValueOnGameThread() != 0;
}

bool FBallisticsDebugDraw::IsBudgetDebugEnabled()
{
	return CVarNPSDebugBudget.GetValueOnGameThread() != 0;
}
// Color Helpers

static FColor GetStateColor(EProjectileState State)
{
	switch (State)
	{
	case EProjectileState::InFlight:    return FColor::Cyan;
	case EProjectileState::Ricocheted:  return FColor::Yellow;
	case EProjectileState::Penetrating: return FColor::Orange;
	case EProjectileState::Sliding:     return FColor::Blue;
	case EProjectileState::Embedded:    return FColor::Green;
	case EProjectileState::Destroyed:   return FColor::Red;
	default:                            return FColor::White;
	}
}

static FColor GetImpactColor(EImpactResponse Response)
{
	switch (Response)
	{
	case EImpactResponse::Ricochet:   return FColor::Yellow;
	case EImpactResponse::Penetrate:  return FColor::Orange;
	case EImpactResponse::Embed:      return FColor::Green;
	case EImpactResponse::Shatter:    return FColor::Red;
	case EImpactResponse::Slide:      return FColor::Cyan;
	default:                          return FColor::White;
	}
}

void FBallisticsDebugDraw::DrawProjectileState(
	const UWorld* World,
	const FVector& Position,
	const FVector& Velocity,
	EProjectileState State)
{
	if (!World || !IsProjectileDebugEnabled())
	{
		return;
	}

	const FColor Color = GetStateColor(State);

	// Position sphere
	DrawDebugSphere(World, Position, 5.f, 6, Color, false, DebugDrawDuration);

	// Velocity arrow
	if (!Velocity.IsNearlyZero())
	{
		const FVector ArrowEnd = Position + Velocity.GetSafeNormal() * 80.f;
		DrawDebugDirectionalArrow(World, Position, ArrowEnd, 15.f, Color, false, DebugDrawDuration);
	}
}

void FBallisticsDebugDraw::DrawImpactMarker(
	const UWorld* World,
	const FVector& Location,
	const FVector& Normal,
	EImpactResponse Response)
{
	if (!World || !IsProjectileDebugEnabled())
	{
		return;
	}

	const FColor Color = GetImpactColor(Response);

	// Cross at impact point
	DrawDebugSphere(World, Location, ImpactMarkerSize, 6, Color, false, DebugDrawDuration, 0, 2.f);

	// Normal arrow
	const FVector NormalEnd = Location + Normal * 30.f;
	DrawDebugDirectionalArrow(World, Location, NormalEnd, 10.f, FColor::White, false, DebugDrawDuration);

	// Label
	const TCHAR* ResponseLabel = TEXT("?");
	switch (Response)
	{
	case EImpactResponse::Ricochet:   ResponseLabel = TEXT("RIC"); break;
	case EImpactResponse::Penetrate:  ResponseLabel = TEXT("PEN"); break;
	case EImpactResponse::Embed:      ResponseLabel = TEXT("EMB"); break;
	case EImpactResponse::Shatter:    ResponseLabel = TEXT("SHT"); break;
	case EImpactResponse::Slide:      ResponseLabel = TEXT("SLD"); break;
	default: break;
	}
	DrawDebugString(World, Location + FVector(0, 0, 15.f), ResponseLabel, nullptr, Color, DebugDrawDuration);
}

void FBallisticsDebugDraw::DrawPathSegment(
	const UWorld* World,
	const FVector& Start,
	const FVector& End,
	EProjectileState State)
{
	if (!World || !IsProjectileDebugEnabled())
	{
		return;
	}

	DrawDebugLine(World, Start, End, GetStateColor(State), false, DebugDrawDuration, 0, 1.5f);
}

void FBallisticsDebugDraw::DrawHitscanResult(
	const UWorld* World,
	const FHitscanResult& Result)
{
	if (!World || !IsHitscanDebugEnabled())
	{
		return;
	}

	for (int32 i = 0; i < Result.PathPoints.Num() - 1; ++i)
	{
		const FColor SegColor = (i == 0) ? FColor::Red : FColor::Orange;
		DrawDebugLine(World, Result.PathPoints[i], Result.PathPoints[i + 1],
			SegColor, false, DebugDrawDuration, 0, 1.5f);
	}

	for (const FHitscanHit& Hit : Result.Hits)
	{
		const FColor Color = GetImpactColor(Hit.ImpactResponse);

		DrawDebugSphere(World, Hit.HitResult.ImpactPoint, ImpactMarkerSize, 6,
			Color, false, DebugDrawDuration, 0, 2.f);

		// Normal
		DrawDebugDirectionalArrow(World, Hit.HitResult.ImpactPoint,
			Hit.HitResult.ImpactPoint + Hit.HitResult.ImpactNormal * 25.f,
			8.f, FColor::White, false, DebugDrawDuration);

		// Energy fraction label
		const FString EnergyStr = FString::Printf(TEXT("%.0f%%"), Hit.RemainingEnergyFraction * 100.f);
		DrawDebugString(World, Hit.HitResult.ImpactPoint + FVector(0, 0, 20.f),
			EnergyStr, nullptr, Color, DebugDrawDuration);
	}

	// Start marker
	if (Result.PathPoints.Num() > 0)
	{
		DrawDebugSphere(World, Result.PathPoints[0], 4.f, 4, FColor::White, false, DebugDrawDuration);
	}
}
// Explosion Drawing

void FBallisticsDebugDraw::DrawDetonation(
	const UWorld* World,
	const FDetonationResult& Result,
	float BlastRadius,
	float BlastInnerRadius)
{
	if (!World || !IsDetonationDebugEnabled())
	{
		return;
	}

	const FVector& Center = Result.DetonationLocation;

	// Detonation point
	DrawDebugSphere(World, Center, 12.f, 8, FColor::White, false, DebugDrawDuration, 0, 3.f);

	// Inner blast radius (full damage)
	if (BlastInnerRadius > 0.f)
	{
		DrawDebugSphere(World, Center, BlastInnerRadius, SphereSegments,
			FColor::Red, false, DebugDrawDuration, 0, 2.f);
	}

	// Outer blast radius (falloff edge)
	if (BlastRadius > 0.f)
	{
		DrawDebugSphere(World, Center, BlastRadius, SphereSegments,
			FColor(255, 100, 0), false, DebugDrawDuration, 0, 1.f);
	}

	// Blast hit lines (to each actor hit by blast)
	for (const FShrapnelHit& Hit : Result.BlastHits)
	{
		if (Hit.HitActor.IsValid())
		{
			DrawDebugLine(World, Center, Hit.HitActor->GetActorLocation(),
				FColor::Red, false, DebugDrawDuration, 0, 0.5f);

			const FString DmgStr = FString::Printf(TEXT("B:%.0f"), Hit.Damage);
			DrawDebugString(World, Hit.HitActor->GetActorLocation() + FVector(0, 0, 30.f),
				DmgStr, nullptr, FColor::Red, DebugDrawDuration);
		}
	}

	// Summary at detonation point
	const FString Summary = FString::Printf(TEXT("Blast:%d Shrapnel:%d Total:%d"),
		Result.BlastHits.Num(), Result.ShrapnelHits.Num(), Result.TotalActorsDamaged);
	DrawDebugString(World, Center + FVector(0, 0, 40.f), Summary, nullptr, FColor::White, DebugDrawDuration);
}

void FBallisticsDebugDraw::DrawShrapnelTrace(
	const UWorld* World,
	const FVector& Origin,
	const FHitscanResult& TraceResult)
{
	if (!World || !IsProjectileDebugEnabled())
	{
		return;
	}

	for (int32 i = 0; i < TraceResult.PathPoints.Num() - 1; ++i)
	{
		DrawDebugLine(World, TraceResult.PathPoints[i], TraceResult.PathPoints[i + 1],
			FColor(255, 200, 0), false, DebugDrawDuration, 0, 0.f);
	}

	// Hit markers
	for (const FHitscanHit& Hit : TraceResult.Hits)
	{
		DrawDebugPoint(World, Hit.HitResult.ImpactPoint, 5.f,
			GetImpactColor(Hit.ImpactResponse), false, DebugDrawDuration);
	}
}
// Budget HUD

void FBallisticsDebugDraw::DrawBudgetHUD(
	const UWorld* World,
	int32 FrameImpactsUsed,
	int32 FrameBudgetMax)
{
	if (!World || !IsBudgetDebugEnabled())
	{
		return;
	}

	if (!GEngine)
	{
		return;
	}

	const float Ratio = (FrameBudgetMax > 0) ? (static_cast<float>(FrameImpactsUsed) / FrameBudgetMax) : 0.f;

	FColor TextColor = FColor::Green;
	if (Ratio > 0.8f)
	{
		TextColor = FColor::Red;
	}
	else if (Ratio > 0.5f)
	{
		TextColor = FColor::Yellow;
	}

	const FString BudgetStr = FString::Printf(
		TEXT("NPS Impact Budget: %d / %d (%.0f%%)"),
		FrameImpactsUsed, FrameBudgetMax, Ratio * 100.f
	);

	GEngine->AddOnScreenDebugMessage(-1, 0.f, TextColor, BudgetStr);
}

#endif // !UE_BUILD_SHIPPING
