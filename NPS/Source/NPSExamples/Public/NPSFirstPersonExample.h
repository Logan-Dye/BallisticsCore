// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsCoreSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "NPSExampleGuide.h"
#include "NPSFirstPersonExample.generated.h"

class ANPSExampleTarget;
class UBallisticsAggregateTick;
class UBallisticsLaunchCosmeticProfile;
class UBallisticsProjectileDataAsset;
class UCameraComponent;
class UProjectileLauncherComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class ENPSPlaygroundGunType : uint8
{
	Rifle,
	Pistol,
	Launcher
};

/**
 * Base gun used by the playable first person example.
 *
 * Child classes configure presentation and launch cadence. NPS data assets
 * provide the projectile, authority, explosive, and wind behavior.
 */
UCLASS(Abstract, Blueprintable)
class NPSEXAMPLES_API ANPSPlaygroundGun : public AActor
{
	GENERATED_BODY()

public:
	ANPSPlaygroundGun();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void AttachToCharacter();
	void ApplyRuntimeConfiguration(
		ELaunchAuthorityMode AuthorityMode,
		bool bExplosive,
		bool bWindEnabled);
	void SetGunActive(bool bNewActive);
	void StartFiring();
	void StopFiring();
	bool FireOnce();

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	ENPSPlaygroundGunType GetGunType() const { return GunType; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	FText GetGunDisplayName() const { return DisplayName; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	UBallisticsProjectileDataAsset* GetActiveProjectileData() const;

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	UProjectileLauncherComponent* GetLauncher() const { return Launcher; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	USkeletalMeshComponent* GetFirstPersonMeshComponent() const
	{
		return FirstPersonMesh;
	}

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	USkeletalMeshComponent* GetWorldMeshComponent() const { return WorldMesh; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	bool IsAutomatic() const { return bAutomatic; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	bool IsGunActive() const { return bGunActive; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<USkeletalMeshComponent> WorldMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<UProjectileLauncherComponent> Launcher;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "NPS|First Person|Cosmetics",
		meta = (DisplayName = "Launch Cosmetic Profile"))
	TSoftObjectPtr<UBallisticsLaunchCosmeticProfile> LaunchCosmeticProfile;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	ENPSPlaygroundGunType GunType = ENPSPlaygroundGunType::Rifle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	TSoftObjectPtr<USkeletalMesh> GunMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	bool bAutomatic = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person",
		meta = (ClampMin = "0.01"))
	float RefireInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	FTransform FirstPersonMeshTransform;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPS|First Person")
	FTransform WorldMeshTransform;

	virtual void OnRep_Owner() override;

private:
	TWeakObjectPtr<UBallisticsAggregateTick> AggregateTicker;
	double NextAllowedShotTime = 0.0;
	bool bTriggerHeld = false;
	bool bGunActive = false;

	void ApplyVisualMesh();
	void RegisterAggregateUpdate();
	void UnregisterAggregateUpdate();
	void HandleAggregateUpdate(float DeltaTime);
	FString BuildProjectileAssetPath(
		ELaunchAuthorityMode AuthorityMode,
		bool bExplosive,
		bool bWindEnabled) const;
};

UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSPlaygroundRifleGun : public ANPSPlaygroundGun
{
	GENERATED_BODY()

public:
	ANPSPlaygroundRifleGun();
};

UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSPlaygroundPistolGun : public ANPSPlaygroundGun
{
	GENERATED_BODY()

public:
	ANPSPlaygroundPistolGun();
};

UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSPlaygroundLauncherGun : public ANPSPlaygroundGun
{
	GENERATED_BODY()

public:
	ANPSPlaygroundLauncherGun();
};

/** Playable first person character for standalone and multiplayer examples. */
UCLASS(Blueprintable)
class NPSEXAMPLES_API ANPSPlaygroundCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANPSPlaygroundCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	ANPSPlaygroundGun* GetCurrentGun() const;

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	int32 GetSelectedGunIndex() const { return SelectedGunIndex; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	ELaunchAuthorityMode GetSelectedAuthorityMode() const { return SelectedAuthorityMode; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	bool IsExplosiveEnabled() const { return bExplosiveEnabled; }

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	bool IsWindEnabled() const { return bWindEnabled; }

	UFUNCTION(BlueprintCallable, Category = "NPS|First Person")
	void SetPlaygroundConfiguration(
		int32 GunIndex,
		ELaunchAuthorityMode AuthorityMode,
		bool bExplosive,
		bool bEnableWind);

	UFUNCTION(BlueprintCallable, Category = "NPS|First Person")
	void FireCurrentGunOnce();

	FVector GetLaunchOrigin() const;
	FVector GetAimDirection() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPS|First Person")
	TObjectPtr<UStaticMeshComponent> WorldBody;

	UPROPERTY(ReplicatedUsing = OnRep_Guns, BlueprintReadOnly,
		Category = "NPS|First Person")
	TArray<TObjectPtr<ANPSPlaygroundGun>> Guns;

	UPROPERTY(ReplicatedUsing = OnRep_PlaygroundConfiguration, BlueprintReadOnly,
		Category = "NPS|First Person")
	int32 SelectedGunIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_PlaygroundConfiguration, BlueprintReadOnly,
		Category = "NPS|First Person")
	ELaunchAuthorityMode SelectedAuthorityMode =
		ELaunchAuthorityMode::ClientPredicted;

	UPROPERTY(ReplicatedUsing = OnRep_PlaygroundConfiguration, BlueprintReadOnly,
		Category = "NPS|First Person")
	bool bExplosiveEnabled = false;

	UPROPERTY(ReplicatedUsing = OnRep_PlaygroundConfiguration, BlueprintReadOnly,
		Category = "NPS|First Person")
	bool bWindEnabled = false;

	UFUNCTION()
	void OnRep_Guns();

	UFUNCTION()
	void OnRep_PlaygroundConfiguration();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetPlaygroundConfiguration(
		int32 GunIndex,
		ELaunchAuthorityMode AuthorityMode,
		bool bExplosive,
		bool bEnableWind);

private:
	void SpawnGuns();
	void ApplyPlaygroundConfiguration();
	void SubmitPlaygroundConfiguration();
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void StartJump();
	void StopJump();
	void StartFiring();
	void StopFiring();
	void SelectRifle();
	void SelectPistol();
	void SelectLauncher();
	void SelectServerAuthoritative();
	void SelectClientPredicted();
	void SelectClientAuthoritative();
	void ToggleExplosive();
	void ToggleWind();
	void ToggleDebugDraw();
	void ToggleGuide();
	void ToggleMapBrowser();
	void SelectPreviousMap();
	void SelectNextMap();
	void OpenSelectedMap();
	void CloseGuidePanels();
	ANPSExampleGuideHUD* GetGuideHUD() const;
};

/** Runtime-built target range used by the first person map. */
UCLASS()
class NPSEXAMPLES_API ANPSPlaygroundArena : public AActor
{
	GENERATED_BODY()

public:
	ANPSPlaygroundArena();
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "NPS|First Person")
	UStaticMeshComponent* GetFloorMeshComponent() const { return FloorMesh; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "NPS|First Person")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "NPS|First Person")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> VisualMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> Labels;

private:
	void BuildArena();
	void SpawnTargets();
	UStaticMeshComponent* AddBox(
		const FName& Name,
		const FVector& Location,
		const FVector& Scale,
		const FLinearColor& Color,
		bool bCollision);
	void AddLabel(
		const FString& Text,
		const FVector& Location,
		const FLinearColor& Color,
		float WorldSize);
};

UCLASS()
class NPSEXAMPLES_API ANPSPlaygroundHUD : public ANPSExampleGuideHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawStatusLine(
		const FString& Label,
		const FString& Value,
		float X,
		float& Y,
		const FLinearColor& ValueColor);
};

UCLASS()
class NPSEXAMPLES_API ANPSPlaygroundGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANPSPlaygroundGameMode();
	virtual void StartPlay() override;
};
