// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsImpactBudget.h"
#include "BallisticsCoreSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

EImpactBudgetResult UBallisticsImpactBudget::SubmitImpact(const FImpactBudgetRequest& Request)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (!Settings || !Settings->bEnableImpactBudget)
	{
		// Budget disabled - allow everything
		OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Allowed);
		return EImpactBudgetResult::Allowed;
	}

	// Critical priority always passes
	if (Request.Priority == EImpactPriority::Critical)
	{
		FrameImpactsUsed++;
		OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Allowed);
		return EImpactBudgetResult::Allowed;
	}

	if (Request.ProjectileID >= 0 && Settings->MaxImpactsPerProjectile > 0)
	{
		int32& ProjectileCount = ProjectileImpactCounts.FindOrAdd(Request.ProjectileID, 0);
		if (ProjectileCount >= Settings->MaxImpactsPerProjectile)
		{
			OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Denied);
			return EImpactBudgetResult::Denied;
		}
		ProjectileCount++;
	}

	// Adjust priority by camera distance.
	const EImpactPriority AdjustedPriority = AdjustPriorityByDistance(Request);

	const int32 MaxPerFrame = Settings->MaxImpactsPerFrame;

	if (FrameImpactsUsed < MaxPerFrame)
	{
		// Within the hard budget.
		FrameImpactsUsed++;
		OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Allowed);
		return EImpactBudgetResult::Allowed;
	}

	// Reduce high-priority effects over budget and deny lower priorities.
	if (AdjustedPriority >= EImpactPriority::High)
	{
		FrameImpactsUsed++;
		OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Reduced);
		return EImpactBudgetResult::Reduced;
	}

	OnImpactBudgetResult.Broadcast(Request, EImpactBudgetResult::Denied);
	return EImpactBudgetResult::Denied;
}

void UBallisticsImpactBudget::ResetFrameBudget()
{
	FrameImpactsUsed = 0;
	// Per-projectile counts persist until projectile destruction.
}

int32 UBallisticsImpactBudget::GetFrameBudgetMax() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	return Settings ? Settings->MaxImpactsPerFrame : 32;
}

EImpactPriority UBallisticsImpactBudget::AdjustPriorityByDistance(const FImpactBudgetRequest& Request) const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (!Settings || Settings->ImpactBudgetFadeDistance <= 0.f)
	{
		return Request.Priority;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return Request.Priority;
	}

	float MinDistSq = MAX_FLT;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		FVector CameraLocation;
		FRotator CameraRotation;
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

		const float DistSq = FVector::DistSquared(Request.ImpactLocation, CameraLocation);
		MinDistSq = FMath::Min(MinDistSq, DistSq);
	}

	const float FadeDistSq = FMath::Square(Settings->ImpactBudgetFadeDistance);

	// Beyond fade distance - demote priority by one level
	if (MinDistSq > FadeDistSq)
	{
		switch (Request.Priority)
		{
		case EImpactPriority::Critical:
			return EImpactPriority::Critical; // Never demote critical
		case EImpactPriority::High:
			return EImpactPriority::Normal;
		case EImpactPriority::Normal:
			return EImpactPriority::Low;
		case EImpactPriority::Low:
			return EImpactPriority::Low; // Already lowest
		default:
			return Request.Priority;
		}
	}

	return Request.Priority;
}
