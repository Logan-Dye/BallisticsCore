// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPSExampleCatalog.generated.h"

class UBallisticsExplosiveDataAsset;
class UBallisticsLaunchCosmeticModifier;
class UBallisticsLaunchCosmeticProfile;
class UBallisticsProjectileDataAsset;
class UBallisticsSpreadDataAsset;
class UWorld;

UENUM(BlueprintType)
enum class ENPSExampleScenario : uint8
{
	Showcase,
	AuthorityModes,
	Trajectories,
	MaterialResponses,
	MultiProjectile,
	Explosives,
	Cosmetics,
	NetworkScale,
	Performance
};

USTRUCT(BlueprintType)
struct NPSEXAMPLES_API FNPSExampleFact
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Value;
};

USTRUCT(BlueprintType)
struct NPSEXAMPLES_API FNPSExampleMapEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FName MapId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Summary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UWorld> Map;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	ENPSExampleScenario Scenario = ENPSExampleScenario::Showcase;
};

USTRUCT(BlueprintType)
struct NPSEXAMPLES_API FNPSExampleStationEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FName ExampleId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText Summary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	FText ExpectedResult;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	ENPSExampleScenario Scenario = ENPSExampleScenario::Showcase;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UBallisticsProjectileDataAsset> ProjectileData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UBallisticsSpreadDataAsset> SpreadData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UBallisticsExplosiveDataAsset> ExplosiveData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UBallisticsLaunchCosmeticProfile> LaunchProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TSoftObjectPtr<UBallisticsLaunchCosmeticModifier> LaunchModifier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TArray<FNPSExampleFact> Facts;
};

UCLASS(BlueprintType)
class NPSEXAMPLES_API UNPSExampleCatalog : public UDataAsset
{
	GENERATED_BODY()

  public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TArray<FNPSExampleMapEntry> Maps;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPS|Examples")
	TArray<FNPSExampleStationEntry> Stations;

	const FNPSExampleStationEntry* FindStation(FName ExampleId) const;

	const FNPSExampleStationEntry* FindStationByTitle(const FString& Title) const;

	static UNPSExampleCatalog* LoadDefaultCatalog();
	static void BuildDefaultContent(TArray<FNPSExampleMapEntry>& OutMaps,
									TArray<FNPSExampleStationEntry>& OutStations);
};
