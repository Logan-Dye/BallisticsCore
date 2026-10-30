// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsDamageInterface.h"
#include "BallisticsTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "NPSExampleCatalog.h"
#include "NPSExampleEnvironment.generated.h"

class UBallisticsProjectileDataAsset;
class UBallisticsAggregateTick;
class UBallisticsDetonationComponent;
class UBallisticsLaunchCosmeticModifier;
class UBallisticsLaunchCosmeticProfile;
class UBallisticsLaunchContributorComponent;
class UBoxComponent;
class UProjectileBallisticsComponent;
class UProjectileLauncherComponent;
class UProjectileNetProxyComponent;
class UProjectileTracerComponent;
class UPhysicalMaterial;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class ULevel;
class UWorld;

/**
 * Reusable projectile actor for the NPS examples.
 *
 * Includes deterministic simulation, interpolation, tracer presentation,
 * and detonation support.
 */
UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSExampleProjectile : public AActor
{
	GENERATED_BODY()

public:
	ANPSExampleProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UProjectileBallisticsComponent> Ballistics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UProjectileTracerComponent> Tracer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UProjectileNetProxyComponent> NetProxy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UBallisticsDetonationComponent> Detonation;
};

/** Replicated damage target with a live hit/energy readout. */
UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSExampleTarget : public AActor, public IBallisticsDamageInterface
{
	GENERATED_BODY()

public:
	ANPSExampleTarget();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UTextRenderComponent> Readout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_TargetState,
		Category = "NPS|Example")
	FString TargetLabel = TEXT("TARGET");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_TargetState,
		Category = "NPS|Example")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TargetState,
		Category = "NPS|Example")
	int32 HitCount = 0;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TargetState,
		Category = "NPS|Example")
	float LastKineticEnergy = 0.f;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TargetState,
		Category = "NPS|Example")
	FVector LastImpactLocation = FVector::ZeroVector;

	virtual void BeginPlay() override;
	virtual void OnBallisticDamage_Implementation(
		const FBallisticDamageContext& Context) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_TargetState();

private:
	UPROPERTY()
	TObjectPtr<UPhysicalMaterial> RuntimePhysicalMaterial;

	void RefreshReadout();
};

/** A labeled, data-asset-driven launcher used by every showcase station. */
UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSExampleLauncher : public AActor
{
	GENERATED_BODY()

public:
	ANPSExampleLauncher();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UStaticMeshComponent> Pedestal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UStaticMeshComponent> DirectionMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UTextRenderComponent> GuideText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UProjectileLauncherComponent> Launcher;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|Example")
	TObjectPtr<UBallisticsLaunchContributorComponent> LaunchContributor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Configuration,
		Category = "NPS|Example")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Configuration,
		Category = "NPS|Example")
	TObjectPtr<UBallisticsLaunchCosmeticProfile> LaunchProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Configuration,
		Category = "NPS|Example")
	FString StationLabel = TEXT("NPS EXAMPLE");

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Configuration,
		Category = "NPS|Example")
	FName ExampleId;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Telemetry,
		Category = "NPS|Example")
	int32 LaunchCount = 0;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Telemetry,
		Category = "NPS|Example")
	FString TelemetryText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPS|Example",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 ProjectilesPerCycle = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPS|Example")
	bool bDemonstrateSustainedSequence = false;

	void Configure(
		UBallisticsProjectileDataAsset* InProjectileData,
		UBallisticsLaunchCosmeticProfile* InProfile,
		UBallisticsLaunchCosmeticModifier* InModifier,
		const FString& InLabel);

	UFUNCTION(BlueprintCallable, Category = "NPS|Example")
	void LaunchExample();
	void UpdateTelemetry(const ANPSExampleTarget* Target, int32 ActiveCount, int32 PoolSize);

	FVector GetLaunchOrigin() const;
	FVector GetLaunchDirection() const;

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Configuration();

	UFUNCTION()
	void OnRep_Telemetry();

private:
	FString CachedSummary;
	FString CachedExpectedResult;
	void ApplyConfiguration();
	void RefreshGuideText();
};

/**
 * Builds the example environment without level scripting.
 *
 * The director registers sequencing with the NPS aggregate path.
 * It owns neither an actor Tick nor individual timers.
 */
UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSExampleShowcase : public AActor
{
	GENERATED_BODY()

public:
	ANPSExampleShowcase();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Scenario,
		Category = "NPS|Example")
	ENPSExampleScenario Scenario = ENPSExampleScenario::Showcase;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPS|Example")
	bool bAutoRun = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPS|Example",
		meta = (ClampMin = "0.1"))
	float LaunchInterval = 0.8f;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Example")
	TArray<TObjectPtr<ANPSExampleLauncher>> Launchers;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Example")
	TArray<TObjectPtr<ANPSExampleTarget>> Targets;

	UFUNCTION(BlueprintCallable, Category = "NPS|Example")
	void LaunchNextExample();

	UFUNCTION(BlueprintCallable, Category = "NPS|Example")
	void LaunchAllExamplesOnce();

	UFUNCTION()
	void OnRep_Scenario();

	static void EnsureVisualLighting(UWorld* World, ULevel* TargetLevel = nullptr);

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UObject>> RuntimeFallbackAssets;

	UPROPERTY()
	TObjectPtr<ANPSExampleLauncher> ActiveSequenceLauncher;

	TWeakObjectPtr<UBallisticsAggregateTick> AggregateTicker;
	float TimeUntilNextLaunch = -1.f;
	float TimeUntilManualDetonation = -1.f;
	float TimeUntilTelemetryUpdate = 0.f;
	int32 NextLauncherIndex = 0;
	bool bVisualEnvironmentBuilt = false;

	void BuildVisualEnvironment();
	void BuildGameplayEnvironment();
	void BuildAuthorityExamples(float BaseY);
	void BuildTrajectoryExamples(float BaseY);
	void BuildMaterialExamples(float BaseY);
	void BuildMultiProjectileExamples(float BaseY);
	void BuildExplosiveExamples(float BaseY);
	void BuildCosmeticExamples(float BaseY);
	void BuildNetworkScaleExamples(float BaseY);
	void BuildPerformanceExamples(float BaseY);
	void FinishActiveSequence();
	void DetonatePendingManualExamples();
	void HandleAggregateUpdate(float DeltaTime);
	void RefreshStationTelemetry();

	ANPSExampleLauncher* AddStation(
		const FString& Label,
		const TCHAR* ProjectileAssetPath,
		float Y,
		float TargetDistance = 3500.f,
		float PitchDegrees = 0.f,
		const TCHAR* ProfileAssetPath = nullptr,
		const TCHAR* ModifierAssetPath = nullptr,
		int32 ProjectilesPerCycle = 1,
		bool bSustained = false,
		float NetCullDistance = 15000.f);

	void AddVisualSection(const FString& Label, float Y, const FLinearColor& Color);
	void AddVisualLane(float Y, float Length, const FLinearColor& Color);
};

UCLASS()
class NPSEXAMPLES_API ANPSExampleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANPSExampleGameMode();
	virtual void StartPlay() override;
};
