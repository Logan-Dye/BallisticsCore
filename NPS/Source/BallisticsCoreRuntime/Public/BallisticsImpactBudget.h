// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BallisticsTypes.h"
#include "BallisticsImpactBudget.generated.h"

/** Impact priority used when the frame budget is exhausted. */
UENUM(BlueprintType)
enum class EImpactPriority : uint8
{
	/** Distant or environmental impact. */
	Low,
	/** Standard impact. */
	Normal,
	/** Nearby or player-facing impact. */
	High,
	/** Always admitted. */
	Critical
};

/** Input to the impact budget. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FImpactBudgetRequest
{
	GENERATED_BODY()

	/** World location of the impact */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	FVector ImpactLocation = FVector::ZeroVector;

	/** Surface normal at impact */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	FVector ImpactNormal = FVector::UpVector;

	/** Physical response produced by the impact. */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	EImpactResponse ImpactResponse = EImpactResponse::Embed;

	/** Physical surface type at impact */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	TEnumAsByte<EPhysicalSurface> SurfaceType = EPhysicalSurface::SurfaceType_Default;

	/** Priority of this impact */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	EImpactPriority Priority = EImpactPriority::Normal;

	/** The projectile that caused this impact (can be null for client-auth launches) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	TWeakObjectPtr<AActor> ProjectileActor;

	/** The actor that was hit (null for world geometry) */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	TWeakObjectPtr<AActor> HitActor;

	/** Projectile ID for per-projectile budget tracking */
	UPROPERTY(BlueprintReadWrite, Category = "Ballistics|VFX")
	int32 ProjectileID = -1;
};

/** Impact rendering level selected by the budget. */
UENUM(BlueprintType)
enum class EImpactBudgetResult : uint8
{
	/** Full effect. */
	Allowed,
	/** Reduced effect. */
	Reduced,
	/** Effect denied. */
	Denied
};

/** Broadcast after each impact budget decision. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnImpactBudgetResult,
	const FImpactBudgetRequest&, Request,
	EImpactBudgetResult, Result
);

/** Per-frame and per-projectile impact effect admission. */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsImpactBudget : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Submit an impact event for budget evaluation.
	 * Broadcasts OnImpactBudgetResult with the result.
	 *
	 * @param Request  The impact event to evaluate.
	 * @return The budget result (Allowed, Reduced, or Denied).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|VFX")
	EImpactBudgetResult SubmitImpact(const FImpactBudgetRequest& Request);

	/** Reset per-frame counters. Called by the subsystem at the start of each tick. */
	void ResetFrameBudget();

	/** Broadcast for every submitted impact. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|VFX")
	FOnImpactBudgetResult OnImpactBudgetResult;

	/** How many impacts have been allowed this frame */
	UFUNCTION(BlueprintPure, Category = "Ballistics|VFX")
	int32 GetFrameImpactsUsed() const { return FrameImpactsUsed; }

	/** How many impacts the current frame budget allows */
	UFUNCTION(BlueprintPure, Category = "Ballistics|VFX")
	int32 GetFrameBudgetMax() const;

private:
	/** Per-frame counter */
	int32 FrameImpactsUsed = 0;

	/** Per-projectile impact counters (ProjectileID -> count) */
	TMap<int32, int32> ProjectileImpactCounts;

	/** Evaluate distance-based priority adjustment */
	EImpactPriority AdjustPriorityByDistance(const FImpactBudgetRequest& Request) const;
};
