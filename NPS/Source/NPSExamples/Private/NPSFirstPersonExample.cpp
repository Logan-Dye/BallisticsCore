// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "NPSFirstPersonExample.h"

#include "BallisticsAggregateTick.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsProjectileDataAsset.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NPSExampleEnvironment.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileLauncherComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace NPSFirstPerson
{
	void InitializeInputBindings()
	{
		static bool bBindingsAdded = false;
		if (bBindingsAdded)
		{
			return;
		}
		bBindingsAdded = true;

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveForward"), EKeys::W, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveForward"), EKeys::S, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveForward"), EKeys::Gamepad_LeftY, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveRight"), EKeys::D, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveRight"), EKeys::A, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_MoveRight"), EKeys::Gamepad_LeftX, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_Turn"), EKeys::MouseX, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_Turn"), EKeys::Gamepad_RightX, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_LookUp"), EKeys::MouseY, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping(
			TEXT("NPSPlayground_LookUp"), EKeys::Gamepad_RightY, 1.f));
	}

	const TCHAR* GunToken(ENPSPlaygroundGunType GunType)
	{
		switch (GunType)
		{
		case ENPSPlaygroundGunType::Rifle:
			return TEXT("Rifle");
		case ENPSPlaygroundGunType::Pistol:
			return TEXT("Pistol");
		case ENPSPlaygroundGunType::Launcher:
			return TEXT("Launcher");
		default:
			return TEXT("Rifle");
		}
	}

	const TCHAR* AuthorityToken(ELaunchAuthorityMode AuthorityMode)
	{
		switch (AuthorityMode)
		{
		case ELaunchAuthorityMode::ServerAuthoritative:
			return TEXT("Server");
		case ELaunchAuthorityMode::ClientPredicted:
			return TEXT("Predicted");
		case ELaunchAuthorityMode::ClientAuthoritative:
			return TEXT("Client");
		default:
			return TEXT("Predicted");
		}
	}

	FString AuthorityLabel(ELaunchAuthorityMode AuthorityMode)
	{
		switch (AuthorityMode)
		{
		case ELaunchAuthorityMode::ServerAuthoritative:
			return TEXT("SERVER AUTHORITATIVE");
		case ELaunchAuthorityMode::ClientPredicted:
			return TEXT("CLIENT PREDICTED");
		case ELaunchAuthorityMode::ClientAuthoritative:
			return TEXT("CLIENT AUTHORITATIVE");
		default:
			return TEXT("UNKNOWN");
		}
	}

	UMaterialInstanceDynamic* ApplyColor(
		UStaticMeshComponent* Component,
		const FLinearColor& Color)
	{
		if (!Component)
		{
			return nullptr;
		}

		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!BaseMaterial)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* Material =
			UMaterialInstanceDynamic::Create(BaseMaterial, Component);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Material);
		return Material;
	}
}

ANPSPlaygroundGun::ANPSPlaygroundGun()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(
		TEXT("First Person Gun"));
	FirstPersonMesh->SetupAttachment(SceneRoot);
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType =
		EFirstPersonPrimitiveType::FirstPerson;

	WorldMesh = CreateDefaultSubobject<USkeletalMeshComponent>(
		TEXT("World Gun"));
	WorldMesh->SetupAttachment(SceneRoot);
	WorldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldMesh->SetOwnerNoSee(true);
	WorldMesh->FirstPersonPrimitiveType =
		EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	Launcher = CreateDefaultSubobject<UProjectileLauncherComponent>(
		TEXT("NPS Launcher"));
	Launcher->bEnableCosmeticRouting = true;

	LaunchCosmeticProfile = TSoftObjectPtr<UBallisticsLaunchCosmeticProfile>(
		FSoftObjectPath(
			TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile."
				"DA_NPS_LaunchProfile")));

	FirstPersonMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(35.f, 18.f, -16.f));
	WorldMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(35.f, 16.f, 35.f));
}

void ANPSPlaygroundGun::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisualMesh();
	AttachToCharacter();
	Launcher->SetLaunchEffectSourceComponent(FirstPersonMesh);
	Launcher->SetLaunchEffectSourceBinding(TEXT("Muzzle"), FirstPersonMesh);
	Launcher->SetLaunchEffectSourceBinding(TEXT("ShellEject"), FirstPersonMesh);

	if (HasAuthority())
	{
		if (UBallisticsLaunchCosmeticProfile* Profile =
			LaunchCosmeticProfile.LoadSynchronous())
		{
			Launcher->SetLaunchCosmeticProfile(Profile);
		}
	}
}

void ANPSPlaygroundGun::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAggregateUpdate();
	Super::EndPlay(EndPlayReason);
}

void ANPSPlaygroundGun::OnRep_Owner()
{
	Super::OnRep_Owner();
	AttachToCharacter();
}

void ANPSPlaygroundGun::AttachToCharacter()
{
	ANPSPlaygroundCharacter* Character =
		Cast<ANPSPlaygroundCharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	AttachToComponent(
		Character->GetRootComponent(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	FirstPersonMesh->AttachToComponent(
		Character->GetFirstPersonCamera(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	FirstPersonMesh->SetRelativeTransform(FirstPersonMeshTransform);
	WorldMesh->AttachToComponent(
		Character->GetRootComponent(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	WorldMesh->SetRelativeTransform(WorldMeshTransform);
}

void ANPSPlaygroundGun::ApplyRuntimeConfiguration(
	ELaunchAuthorityMode AuthorityMode,
	bool bExplosive,
	bool bWindEnabled)
{
	const FString AssetPath =
		BuildProjectileAssetPath(AuthorityMode, bExplosive, bWindEnabled);
	UBallisticsProjectileDataAsset* ProjectileData =
		LoadObject<UBallisticsProjectileDataAsset>(nullptr, *AssetPath);
	Launcher->SetProjectileDataAsset(ProjectileData);

	if (!ProjectileData)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("NPS first person example could not load %s"),
			*AssetPath);
	}
}

void ANPSPlaygroundGun::SetGunActive(bool bNewActive)
{
	bGunActive = bNewActive;
	FirstPersonMesh->SetVisibility(bGunActive, true);
	WorldMesh->SetVisibility(bGunActive, true);
	if (!bGunActive)
	{
		StopFiring();
	}
}

void ANPSPlaygroundGun::StartFiring()
{
	if (!bGunActive)
	{
		return;
	}

	bTriggerHeld = true;
	FireOnce();
	if (bAutomatic)
	{
		RegisterAggregateUpdate();
	}
}

void ANPSPlaygroundGun::StopFiring()
{
	bTriggerHeld = false;
	UnregisterAggregateUpdate();
}

bool ANPSPlaygroundGun::FireOnce()
{
	ANPSPlaygroundCharacter* Character =
		Cast<ANPSPlaygroundCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!bGunActive || !Character || !World ||
		!Launcher->GetProjectileDataAsset())
	{
		return false;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime + UE_KINDA_SMALL_NUMBER < NextAllowedShotTime)
	{
		return false;
	}

	NextAllowedShotTime = CurrentTime + FMath::Max(RefireInterval, 0.01f);
	Launcher->Launch(
		Character->GetLaunchOrigin(),
		Character->GetAimDirection());
	return true;
}

UBallisticsProjectileDataAsset*
ANPSPlaygroundGun::GetActiveProjectileData() const
{
	return Launcher ? Launcher->GetProjectileDataAsset() : nullptr;
}

void ANPSPlaygroundGun::ApplyVisualMesh()
{
	if (USkeletalMesh* Mesh = GunMesh.LoadSynchronous())
	{
		FirstPersonMesh->SetSkeletalMeshAsset(Mesh);
		WorldMesh->SetSkeletalMeshAsset(Mesh);
	}
}

void ANPSPlaygroundGun::RegisterAggregateUpdate()
{
	if (AggregateTicker.IsValid())
	{
		return;
	}

	if (UProjectileBallisticsSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>()
			: nullptr)
	{
		AggregateTicker = Subsystem->GetAggregateTicker();
		if (AggregateTicker.IsValid())
		{
			AggregateTicker->OnAggregateUpdate().AddUObject(
				this,
				&ANPSPlaygroundGun::HandleAggregateUpdate);
		}
	}
}

void ANPSPlaygroundGun::UnregisterAggregateUpdate()
{
	if (AggregateTicker.IsValid())
	{
		AggregateTicker->OnAggregateUpdate().RemoveAll(this);
		AggregateTicker.Reset();
	}
}

void ANPSPlaygroundGun::HandleAggregateUpdate(float DeltaTime)
{
	if (!bTriggerHeld || !bGunActive)
	{
		UnregisterAggregateUpdate();
		return;
	}

	FireOnce();
}

FString ANPSPlaygroundGun::BuildProjectileAssetPath(
	ELaunchAuthorityMode AuthorityMode,
	bool bExplosive,
	bool bWindEnabled) const
{
	const FString AssetName = FString::Printf(
		TEXT("DA_NPS_Playground_%s_%s_%s_%s"),
		NPSFirstPerson::GunToken(GunType),
		NPSFirstPerson::AuthorityToken(AuthorityMode),
		bExplosive ? TEXT("Explosive") : TEXT("Standard"),
		bWindEnabled ? TEXT("Wind") : TEXT("Calm"));
	return FString::Printf(
		TEXT("/NPS/Examples/Data/Playground/%s/%s.%s"),
		NPSFirstPerson::GunToken(GunType),
		*AssetName,
		*AssetName);
}

ANPSPlaygroundRifleGun::ANPSPlaygroundRifleGun()
{
	GunType = ENPSPlaygroundGunType::Rifle;
	DisplayName = FText::FromString(TEXT("Automatic Rifle"));
	GunMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/NPS/FirstPerson/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle")));
	bAutomatic = true;
	RefireInterval = 0.09f;
	FirstPersonMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(38.f, 16.f, -15.f));
	WorldMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(34.f, 16.f, 36.f));
}

ANPSPlaygroundPistolGun::ANPSPlaygroundPistolGun()
{
	GunType = ENPSPlaygroundGunType::Pistol;
	DisplayName = FText::FromString(TEXT("Precision Pistol"));
	GunMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/NPS/FirstPerson/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol")));
	bAutomatic = false;
	RefireInterval = 0.24f;
	FirstPersonMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(36.f, 14.f, -16.f));
	WorldMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(30.f, 14.f, 38.f));
}

ANPSPlaygroundLauncherGun::ANPSPlaygroundLauncherGun()
{
	GunType = ENPSPlaygroundGunType::Launcher;
	DisplayName = FText::FromString(TEXT("Heavy Launcher"));
	GunMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/NPS/FirstPerson/Weapons/GrenadeLauncher/Meshes/"
			"SKM_GrenadeLauncher.SKM_GrenadeLauncher")));
	bAutomatic = false;
	RefireInterval = 0.8f;
	FirstPersonMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(40.f, 17.f, -17.f));
	WorldMeshTransform = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(34.f, 17.f, 36.f));
}

ANPSPlaygroundCharacter::ANPSPlaygroundCharacter()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = 600.f;
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.f;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(
		TEXT("First Person Camera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->FirstPersonFieldOfView = 80.f;

	WorldBody = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("World Body"));
	WorldBody->SetupAttachment(GetCapsuleComponent());
	WorldBody->SetOwnerNoSee(true);
	WorldBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldBody->SetRelativeLocation(FVector(0.f, 0.f, -15.f));
	WorldBody->SetRelativeScale3D(FVector(0.55f, 0.4f, 1.55f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (BodyMesh.Succeeded())
	{
		WorldBody->SetStaticMesh(BodyMesh.Object);
	}
}

void ANPSPlaygroundCharacter::BeginPlay()
{
	Super::BeginPlay();

	NPSFirstPerson::ApplyColor(
		WorldBody,
		FLinearColor(0.025f, 0.12f, 0.22f));

	if (UProjectileBallisticsSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>())
	{
		Subsystem->RegisterFlyByListener(this);
	}

	if (HasAuthority())
	{
		SpawnGuns();
	}
	ApplyPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UProjectileBallisticsSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>()
			: nullptr)
	{
		Subsystem->UnregisterFlyByListener(this);
	}

	for (ANPSPlaygroundGun* Gun : Guns)
	{
		if (Gun)
		{
			Gun->StopFiring();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ANPSPlaygroundCharacter::SetupPlayerInputComponent(
	UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (!PlayerInputComponent)
	{
		return;
	}

	NPSFirstPerson::InitializeInputBindings();
	PlayerInputComponent->BindAxis(TEXT("NPSPlayground_MoveForward"), this,
		&ANPSPlaygroundCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("NPSPlayground_MoveRight"), this,
		&ANPSPlaygroundCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("NPSPlayground_Turn"), this,
		&ANPSPlaygroundCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("NPSPlayground_LookUp"), this,
		&ANPSPlaygroundCharacter::LookUp);

	PlayerInputComponent->BindKey(
		EKeys::SpaceBar, IE_Pressed, this,
		&ANPSPlaygroundCharacter::StartJump);
	PlayerInputComponent->BindKey(
		EKeys::SpaceBar, IE_Released, this,
		&ANPSPlaygroundCharacter::StopJump);
	PlayerInputComponent->BindKey(
		EKeys::LeftMouseButton, IE_Pressed, this,
		&ANPSPlaygroundCharacter::StartFiring);
	PlayerInputComponent->BindKey(
		EKeys::LeftMouseButton, IE_Released, this,
		&ANPSPlaygroundCharacter::StopFiring);

	PlayerInputComponent->BindKey(
		EKeys::One, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectRifle);
	PlayerInputComponent->BindKey(
		EKeys::Two, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectPistol);
	PlayerInputComponent->BindKey(
		EKeys::Three, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectLauncher);
	PlayerInputComponent->BindKey(
		EKeys::F1, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectServerAuthoritative);
	PlayerInputComponent->BindKey(
		EKeys::F2, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectClientPredicted);
	PlayerInputComponent->BindKey(
		EKeys::F3, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectClientAuthoritative);
	PlayerInputComponent->BindKey(
		EKeys::E, IE_Pressed, this,
		&ANPSPlaygroundCharacter::ToggleExplosive);
	PlayerInputComponent->BindKey(
		EKeys::V, IE_Pressed, this,
		&ANPSPlaygroundCharacter::ToggleWind);
	PlayerInputComponent->BindKey(
		EKeys::F4, IE_Pressed, this,
		&ANPSPlaygroundCharacter::ToggleDebugDraw);
	PlayerInputComponent->BindKey(
		EKeys::H, IE_Pressed, this,
		&ANPSPlaygroundCharacter::ToggleGuide);
	PlayerInputComponent->BindKey(
		EKeys::M, IE_Pressed, this,
		&ANPSPlaygroundCharacter::ToggleMapBrowser);
	PlayerInputComponent->BindKey(
		EKeys::Up, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectPreviousMap);
	PlayerInputComponent->BindKey(
		EKeys::Down, IE_Pressed, this,
		&ANPSPlaygroundCharacter::SelectNextMap);
	PlayerInputComponent->BindKey(
		EKeys::Enter, IE_Pressed, this,
		&ANPSPlaygroundCharacter::OpenSelectedMap);
	PlayerInputComponent->BindKey(
		EKeys::Escape, IE_Pressed, this,
		&ANPSPlaygroundCharacter::CloseGuidePanels);
}

void ANPSPlaygroundCharacter::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPSPlaygroundCharacter, Guns);
	DOREPLIFETIME(ANPSPlaygroundCharacter, SelectedGunIndex);
	DOREPLIFETIME(ANPSPlaygroundCharacter, SelectedAuthorityMode);
	DOREPLIFETIME(ANPSPlaygroundCharacter, bExplosiveEnabled);
	DOREPLIFETIME(ANPSPlaygroundCharacter, bWindEnabled);
}

ANPSPlaygroundGun* ANPSPlaygroundCharacter::GetCurrentGun() const
{
	return Guns.IsValidIndex(SelectedGunIndex)
		? Guns[SelectedGunIndex]
		: nullptr;
}

void ANPSPlaygroundCharacter::SetPlaygroundConfiguration(
	int32 GunIndex,
	ELaunchAuthorityMode AuthorityMode,
	bool bExplosive,
	bool bEnableWind)
{
	if (GunIndex < 0 || GunIndex >= 3 ||
		static_cast<uint8>(AuthorityMode) >
			static_cast<uint8>(ELaunchAuthorityMode::ClientAuthoritative))
	{
		return;
	}

	SelectedGunIndex = GunIndex;
	SelectedAuthorityMode = AuthorityMode;
	bExplosiveEnabled = bExplosive;
	bWindEnabled = bEnableWind;
	ApplyPlaygroundConfiguration();

	if (!HasAuthority())
	{
		ServerSetPlaygroundConfiguration(
			SelectedGunIndex,
			SelectedAuthorityMode,
			bExplosiveEnabled,
			bWindEnabled);
	}
	else
	{
		ForceNetUpdate();
	}
}

bool ANPSPlaygroundCharacter::ServerSetPlaygroundConfiguration_Validate(
	int32 GunIndex,
	ELaunchAuthorityMode AuthorityMode,
	bool bExplosive,
	bool bEnableWind)
{
	return GunIndex >= 0 && GunIndex < 3 &&
		static_cast<uint8>(AuthorityMode) <=
			static_cast<uint8>(ELaunchAuthorityMode::ClientAuthoritative);
}

void ANPSPlaygroundCharacter::ServerSetPlaygroundConfiguration_Implementation(
	int32 GunIndex,
	ELaunchAuthorityMode AuthorityMode,
	bool bExplosive,
	bool bEnableWind)
{
	SelectedGunIndex = GunIndex;
	SelectedAuthorityMode = AuthorityMode;
	bExplosiveEnabled = bExplosive;
	bWindEnabled = bEnableWind;
	ApplyPlaygroundConfiguration();
	ForceNetUpdate();
}

void ANPSPlaygroundCharacter::FireCurrentGunOnce()
{
	if (ANPSPlaygroundGun* Gun = GetCurrentGun())
	{
		Gun->FireOnce();
	}
}

FVector ANPSPlaygroundCharacter::GetLaunchOrigin() const
{
	return FirstPersonCamera->GetComponentLocation() +
		FirstPersonCamera->GetForwardVector() * 90.f;
}

FVector ANPSPlaygroundCharacter::GetAimDirection() const
{
	return FirstPersonCamera->GetForwardVector().GetSafeNormal();
}

void ANPSPlaygroundCharacter::OnRep_Guns()
{
	for (ANPSPlaygroundGun* Gun : Guns)
	{
		if (Gun)
		{
			Gun->AttachToCharacter();
		}
	}
	ApplyPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::OnRep_PlaygroundConfiguration()
{
	ApplyPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SpawnGuns()
{
	if (!HasAuthority() || !Guns.IsEmpty())
	{
		return;
	}

	const TSubclassOf<ANPSPlaygroundGun> GunClasses[] = {
		ANPSPlaygroundRifleGun::StaticClass(),
		ANPSPlaygroundPistolGun::StaticClass(),
		ANPSPlaygroundLauncherGun::StaticClass()
	};
	for (const TSubclassOf<ANPSPlaygroundGun>& GunClass : GunClasses)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.Instigator = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANPSPlaygroundGun* Gun = GetWorld()->SpawnActor<ANPSPlaygroundGun>(
			GunClass,
			GetActorTransform(),
			SpawnParameters);
		if (Gun)
		{
			Guns.Add(Gun);
		}
	}
	ForceNetUpdate();
}

void ANPSPlaygroundCharacter::ApplyPlaygroundConfiguration()
{
	for (int32 Index = 0; Index < Guns.Num(); ++Index)
	{
		if (ANPSPlaygroundGun* Gun = Guns[Index])
		{
			Gun->AttachToCharacter();
			Gun->ApplyRuntimeConfiguration(
				SelectedAuthorityMode,
				bExplosiveEnabled,
				bWindEnabled);
			Gun->SetGunActive(Index == SelectedGunIndex);
		}
	}
}

void ANPSPlaygroundCharacter::SubmitPlaygroundConfiguration()
{
	SetPlaygroundConfiguration(
		SelectedGunIndex,
		SelectedAuthorityMode,
		bExplosiveEnabled,
		bWindEnabled);
}

void ANPSPlaygroundCharacter::MoveForward(float Value)
{
	AddMovementInput(GetActorForwardVector(), Value);
}

void ANPSPlaygroundCharacter::MoveRight(float Value)
{
	AddMovementInput(GetActorRightVector(), Value);
}

void ANPSPlaygroundCharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void ANPSPlaygroundCharacter::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void ANPSPlaygroundCharacter::StartJump()
{
	Jump();
}

void ANPSPlaygroundCharacter::StopJump()
{
	StopJumping();
}

void ANPSPlaygroundCharacter::StartFiring()
{
	if (ANPSPlaygroundGun* Gun = GetCurrentGun())
	{
		Gun->StartFiring();
	}
}

void ANPSPlaygroundCharacter::StopFiring()
{
	if (ANPSPlaygroundGun* Gun = GetCurrentGun())
	{
		Gun->StopFiring();
	}
}

void ANPSPlaygroundCharacter::SelectRifle()
{
	SelectedGunIndex = 0;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SelectPistol()
{
	SelectedGunIndex = 1;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SelectLauncher()
{
	SelectedGunIndex = 2;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SelectServerAuthoritative()
{
	SelectedAuthorityMode = ELaunchAuthorityMode::ServerAuthoritative;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SelectClientPredicted()
{
	SelectedAuthorityMode = ELaunchAuthorityMode::ClientPredicted;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::SelectClientAuthoritative()
{
	SelectedAuthorityMode = ELaunchAuthorityMode::ClientAuthoritative;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::ToggleExplosive()
{
	bExplosiveEnabled = !bExplosiveEnabled;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::ToggleWind()
{
	bWindEnabled = !bWindEnabled;
	SubmitPlaygroundConfiguration();
}

void ANPSPlaygroundCharacter::ToggleDebugDraw()
{
	if (APlayerController* PlayerController =
		Cast<APlayerController>(GetController()))
	{
		PlayerController->ConsoleCommand(TEXT("NPS.Debug.All"), true);
	}
}

ANPSExampleGuideHUD* ANPSPlaygroundCharacter::GetGuideHUD() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController ? Cast<ANPSExampleGuideHUD>(PlayerController->GetHUD()) : nullptr;
}

void ANPSPlaygroundCharacter::ToggleGuide() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->ToggleGuide(); }
void ANPSPlaygroundCharacter::ToggleMapBrowser() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->ToggleMapBrowser(); }
void ANPSPlaygroundCharacter::SelectPreviousMap() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->SelectPreviousMap(); }
void ANPSPlaygroundCharacter::SelectNextMap() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->SelectNextMap(); }
void ANPSPlaygroundCharacter::OpenSelectedMap() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->OpenSelectedMap(); }
void ANPSPlaygroundCharacter::CloseGuidePanels() { if (ANPSExampleGuideHUD* HUD = GetGuideHUD()) HUD->ClosePanels(); }

ANPSPlaygroundArena::ANPSPlaygroundArena()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Range Floor"));
	FloorMesh->SetupAttachment(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		FloorMesh->SetStaticMesh(Cube.Object);
	}
	FloorMesh->SetRelativeLocation(FVector(3000.f, 0.f, -60.f));
	FloorMesh->SetRelativeScale3D(FVector(80.f, 40.f, 1.f));
	FloorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ANPSPlaygroundArena::BeginPlay()
{
	Super::BeginPlay();
	ANPSExampleShowcase::EnsureVisualLighting(GetWorld());
	BuildArena();
	if (HasAuthority())
	{
		SpawnTargets();
	}
}

void ANPSPlaygroundArena::BuildArena()
{
	if (!VisualMeshes.IsEmpty())
	{
		return;
	}

	NPSFirstPerson::ApplyColor(
		FloorMesh,
		FLinearColor(0.025f, 0.055f, 0.08f));
	VisualMeshes.Add(FloorMesh);
	AddBox(
		TEXT("Back Wall"),
		FVector(7100.f, 0.f, 500.f),
		FVector(1.f, 40.f, 12.f),
		FLinearColor(0.035f, 0.12f, 0.18f),
		true);

	for (int32 Index = -2; Index <= 2; ++Index)
	{
		AddBox(
			FName(*FString::Printf(TEXT("Lane %d"), Index + 3)),
			FVector(3000.f, Index * 700.f, -5.f),
			FVector(75.f, 0.08f, 0.08f),
			FLinearColor(0.04f, 0.35f, 0.52f),
			false);
	}

	AddBox(
		TEXT("Cover Left"),
		FVector(1800.f, -1200.f, 140.f),
		FVector(2.f, 5.f, 3.f),
		FLinearColor(0.12f, 0.16f, 0.2f),
		true);
	AddBox(
		TEXT("Cover Right"),
		FVector(3000.f, 1200.f, 140.f),
		FVector(2.f, 5.f, 3.f),
		FLinearColor(0.12f, 0.16f, 0.2f),
		true);

	AddLabel(
		TEXT("NPS FIRST PERSON PLAYGROUND"),
		FVector(300.f, 0.f, 360.f),
		FLinearColor(0.1f, 0.8f, 1.f),
		46.f);
	AddLabel(
		TEXT("WIND FORCE: WORLD +Y"),
		FVector(1400.f, 1450.f, 40.f),
		FLinearColor(0.2f, 0.9f, 0.55f),
		32.f);
}

void ANPSPlaygroundArena::SpawnTargets()
{
	for (TActorIterator<ANPSExampleTarget> It(GetWorld()); It; ++It)
	{
		return;
	}

	const FVector TargetLocations[] = {
		FVector(2400.f, -700.f, 170.f),
		FVector(4200.f, 0.f, 170.f),
		FVector(6200.f, 700.f, 170.f),
		FVector(6200.f, -700.f, 170.f)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TargetLocations); ++Index)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANPSExampleTarget* Target =
			GetWorld()->SpawnActor<ANPSExampleTarget>(
				ANPSExampleTarget::StaticClass(),
				TargetLocations[Index],
				FRotator::ZeroRotator,
				SpawnParameters);
		if (Target)
		{
			Target->TargetLabel = FString::Printf(
				TEXT("RANGE TARGET %d"),
				Index + 1);
		}
	}
}

UStaticMeshComponent* ANPSPlaygroundArena::AddBox(
	const FName& Name,
	const FVector& Location,
	const FVector& Scale,
	const FLinearColor& Color,
	bool bCollision)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		return nullptr;
	}

	UStaticMeshComponent* Mesh =
		NewObject<UStaticMeshComponent>(this, Name);
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetStaticMesh(Cube);
	Mesh->SetRelativeLocation(Location);
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetCollisionEnabled(
		bCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	Mesh->RegisterComponent();
	NPSFirstPerson::ApplyColor(Mesh, Color);
	VisualMeshes.Add(Mesh);
	return Mesh;
}

void ANPSPlaygroundArena::AddLabel(
	const FString& Text,
	const FVector& Location,
	const FLinearColor& Color,
	float WorldSize)
{
	UTextRenderComponent* Label =
		NewObject<UTextRenderComponent>(
			this,
			*FString::Printf(TEXT("Label %d"), Labels.Num()));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(Location);
	Label->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetText(FText::FromString(Text));
	Label->SetTextRenderColor(Color.ToFColor(true));
	Label->SetWorldSize(WorldSize);
	Label->RegisterComponent();
	Labels.Add(Label);
}

void ANPSPlaygroundHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const FLinearColor CrosshairColor(0.2f, 0.9f, 1.f);
	DrawLine(CenterX - 8.f, CenterY, CenterX + 8.f, CenterY, CrosshairColor);
	DrawLine(CenterX, CenterY - 8.f, CenterX, CenterY + 8.f, CrosshairColor);

	const ANPSPlaygroundCharacter* Character =
		GetOwningPawn()
			? Cast<ANPSPlaygroundCharacter>(GetOwningPawn())
			: nullptr;
	if (!Character)
	{
		return;
	}

	const ANPSPlaygroundGun* Gun = Character->GetCurrentGun();
	const FString GunName = Gun
		? Gun->GetGunDisplayName().ToString()
		: TEXT("Waiting for replicated gun");

	FString NetModeLabel = TEXT("STANDALONE");
	switch (GetNetMode())
	{
	case NM_ListenServer:
		NetModeLabel = TEXT("LISTEN SERVER");
		break;
	case NM_Client:
		NetModeLabel = TEXT("CLIENT");
		break;
	case NM_DedicatedServer:
		NetModeLabel = TEXT("DEDICATED SERVER");
		break;
	default:
		break;
	}

	float Y = 38.f;
	const float X = 36.f;
	DrawText(
		TEXT("NPS FIRST PERSON PLAYGROUND"),
		FLinearColor(0.1f, 0.8f, 1.f),
		X,
		Y,
		GEngine->GetMediumFont(),
		1.f,
		false);
	Y += 34.f;
	DrawStatusLine(TEXT("NETWORK"), NetModeLabel, X, Y,
		FLinearColor(0.8f, 0.9f, 1.f));
	DrawStatusLine(TEXT("GUN"), GunName, X, Y,
		FLinearColor(1.f, 0.8f, 0.25f));
	DrawStatusLine(
		TEXT("AUTHORITY"),
		NPSFirstPerson::AuthorityLabel(
			Character->GetSelectedAuthorityMode()),
		X,
		Y,
		FLinearColor(0.25f, 0.9f, 0.55f));
	DrawStatusLine(
		TEXT("EXPLOSIVE"),
		Character->IsExplosiveEnabled() ? TEXT("ON") : TEXT("OFF"),
		X,
		Y,
		Character->IsExplosiveEnabled()
			? FLinearColor(1.f, 0.25f, 0.1f)
			: FLinearColor(0.6f, 0.65f, 0.7f));
	DrawStatusLine(
		TEXT("WIND +Y"),
		Character->IsWindEnabled() ? TEXT("ON") : TEXT("OFF"),
		X,
		Y,
		Character->IsWindEnabled()
			? FLinearColor(0.1f, 0.8f, 1.f)
			: FLinearColor(0.6f, 0.65f, 0.7f));

	Y += 12.f;
	const FString Controls[] = {
		TEXT("WASD + MOUSE    MOVE / AIM"),
		TEXT("LMB             FIRE"),
		TEXT("1 / 2 / 3       RIFLE / PISTOL / LAUNCHER"),
		TEXT("F1 / F2 / F3    SERVER / PREDICTED / CLIENT AUTH"),
		TEXT("E               TOGGLE EXPLOSIVE"),
		TEXT("V               TOGGLE WIND"),
		TEXT("F4              TOGGLE NPS DEBUG"),
		TEXT("H / M           GUIDE / MAP BROWSER")
	};
	for (const FString& Control : Controls)
	{
		DrawText(
			Control,
			FLinearColor(0.78f, 0.82f, 0.88f),
			X,
			Y,
			GEngine->GetSmallFont(),
			1.f,
			false);
		Y += 20.f;
	}
}

void ANPSPlaygroundHUD::DrawStatusLine(
	const FString& Label,
	const FString& Value,
	float X,
	float& Y,
	const FLinearColor& ValueColor)
{
	DrawText(
		Label,
		FLinearColor(0.58f, 0.65f, 0.72f),
		X,
		Y,
		GEngine->GetSmallFont(),
		1.f,
		false);
	DrawText(
		Value,
		ValueColor,
		X + 120.f,
		Y,
		GEngine->GetSmallFont(),
		1.f,
		false);
	Y += 22.f;
}

ANPSPlaygroundGameMode::ANPSPlaygroundGameMode()
{
	DefaultPawnClass = ANPSPlaygroundCharacter::StaticClass();
	HUDClass = ANPSPlaygroundHUD::StaticClass();
}

void ANPSPlaygroundGameMode::StartPlay()
{
	Super::StartPlay();

	for (TActorIterator<ANPSPlaygroundArena> It(GetWorld()); It; ++It)
	{
		return;
	}
	GetWorld()->SpawnActor<ANPSPlaygroundArena>();
}
