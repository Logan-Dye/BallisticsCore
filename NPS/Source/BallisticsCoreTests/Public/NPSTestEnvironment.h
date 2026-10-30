// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsDamageInterface.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsLaunchValidation.h"
#include "ProjectileLauncherComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "NPSTestEnvironment.generated.h"

class UBoxComponent;
class UBallisticsProjectileDataAsset;
class UBallisticsDetonationComponent;
class UBallisticsLaunchCosmeticProfile;
class UProjectileBallisticsComponent;
class UProjectileLauncherComponent;
class UProjectileNetProxyComponent;
class UProjectileTracerComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestNetProxyActor : public AActor
{
	GENERATED_BODY()

public:
	ANPSTestNetProxyActor();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileNetProxyComponent> NetProxy;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileBallisticsComponent> Ballistics;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USceneComponent> SceneRoot;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestProjectile : public AActor
{
	GENERATED_BODY()

public:
	ANPSTestProjectile();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileBallisticsComponent> Ballistics;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileTracerComponent> Tracer;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileNetProxyComponent> NetProxy;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UBallisticsDetonationComponent> Detonation;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UStaticMeshComponent> Visual;

	UFUNCTION()
	void DestroyOnDetonation(const FDetonationResult& Result);
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestTarget : public AActor, public IBallisticsDamageInterface
{
	GENERATED_BODY()

public:
	ANPSTestTarget();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NPS|Test")
	int32 HitCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NPS|Test")
	float LastKineticEnergy = 0.f;

	UPROPERTY()
	TObjectPtr<UBallisticsProjectileDataAsset> LastProjectileDataAsset;

	virtual void OnBallisticDamage_Implementation(const FBallisticDamageContext& Context) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestFlyByListener : public APawn
{
	GENERATED_BODY()

public:
	ANPSTestFlyByListener();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	int32 FlyByCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	float LastMissDistance = 0.f;

	UFUNCTION()
	void RecordFlyBy(
		APawn* NearbyPawn,
		FVector ClosestPoint,
		float MissDistance,
		float ProjectileSpeed);
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestLauncher :
	public AActor,
	public IBallisticsLaunchValidator
{
	GENERATED_BODY()

public:
	ANPSTestLauncher();
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileLauncherComponent> Launcher;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileData;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UBallisticsLaunchCosmeticProfile> LaunchCosmeticProfile;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	int32 AuthoritativeImpactCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	int32 AuthoritativeFlyByCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	int32 LaunchValidationCount = 0;

	UPROPERTY(EditAnywhere, Category = "NPS|Test")
	bool bAllowValidatedLaunches = true;

	UFUNCTION()
	void RecordAuthoritativeImpact(const FBallisticsAuthoritativeImpactEvent& Event);

	UFUNCTION()
	void RecordAuthoritativeFlyBy(const FBallisticsAuthoritativeFlyByEvent& Event);

	virtual bool ValidateBallisticsLaunch_Implementation(
		const FBallisticsLaunchValidationContext& Context) override;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestCosmeticLauncher : public ANPSTestLauncher
{
	GENERATED_BODY()

public:
	ANPSTestCosmeticLauncher();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USkeletalMeshComponent> CosmeticSource;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestAuthoredLauncher : public AActor
{
	GENERATED_BODY()

public:
	ANPSTestAuthoredLauncher();

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "NPS|Test")
	TObjectPtr<UProjectileLauncherComponent> Launcher;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestOwnershipProbeProjectile : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<AActor> OwnerObservedAtBeginPlay;

	UPROPERTY()
	TObjectPtr<APawn> InstigatorObservedAtBeginPlay;
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestEnvironmentManager : public AActor
{
	GENERATED_BODY()

public:
	ANPSTestEnvironmentManager();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "NPS|Test")
	bool bAutoRunDemonstration = true;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	TArray<TObjectPtr<ANPSTestTarget>> Targets;

	UPROPERTY(BlueprintReadOnly, Category = "NPS|Test")
	TObjectPtr<ANPSTestLauncher> DemoLauncher;

private:
	float DemoTimer = 0.f;
	int32 DemoPhase = 0;
	void BuildEnvironment();
	void LaunchNextDemonstration();
};

UCLASS()
class BALLISTICSCORETESTS_API ANPSTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANPSTestGameMode();
	virtual void StartPlay() override;
};
