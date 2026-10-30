// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsTypes.h"
#include "UObject/Interface.h"
#include "BallisticsLaunchValidation.generated.h"

class UBallisticsProjectileDataAsset;

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchValidationContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	ELaunchAuthorityMode AuthorityMode = ELaunchAuthorityMode::ServerAuthoritative;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	int32 ProjectileID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	float ClientTimestamp = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Validation")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;
};

UINTERFACE(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsLaunchValidator : public UInterface
{
	GENERATED_BODY()
};

class BALLISTICSCORERUNTIME_API IBallisticsLaunchValidator
{
	GENERATED_BODY()

public:
	/** Return false to reject a remote launch before NPS spawns or snapshots it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Ballistics|Validation")
	bool ValidateBallisticsLaunch(const FBallisticsLaunchValidationContext& Context);
};
