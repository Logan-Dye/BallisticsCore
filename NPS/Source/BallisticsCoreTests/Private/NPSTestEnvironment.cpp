// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "NPSTestEnvironment.h"

#include "BallisticsCoreSettings.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsDetonationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "Net/UnrealNetwork.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileNetProxyComponent.h"
#include "ProjectileTracerComponent.h"
#include "UObject/ConstructorHelpers.h"

ANPSTestNetProxyActor::ANPSTestNetProxyActor()
{
	bReplicates = true;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
	NetProxy = CreateDefaultSubobject<UProjectileNetProxyComponent>(TEXT("NetProxy"));
	Ballistics = CreateDefaultSubobject<UProjectileBallisticsComponent>(TEXT("Ballistics"));
	NetProxy->InterpolationSpeed = 120.f;
}

ANPSTestProjectile::ANPSTestProjectile()
{
	bReplicates = true;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
	Ballistics = CreateDefaultSubobject<UProjectileBallisticsComponent>(TEXT("Ballistics"));
	Tracer = CreateDefaultSubobject<UProjectileTracerComponent>(TEXT("Tracer"));
	NetProxy = CreateDefaultSubobject<UProjectileNetProxyComponent>(TEXT("NetProxy"));
	Detonation = CreateDefaultSubobject<UBallisticsDetonationComponent>(TEXT("Detonation"));
	// Keep network automation independent of wall-clock/editor frame throttling.
	NetProxy->InterpolationSpeed = 120.f;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(SceneRoot);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.08f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Visual->SetStaticMesh(Sphere.Object);
	}
}

void ANPSTestProjectile::DestroyOnDetonation(const FDetonationResult&)
{
	Destroy();
}

ANPSTestTarget::ANPSTestTarget()
{
	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(40.f, 120.f, 120.f));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	RootComponent = Collision;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.8f, 2.4f, 2.4f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Visual->SetStaticMesh(Cube.Object);
	}
}

void ANPSTestTarget::OnBallisticDamage_Implementation(const FBallisticDamageContext& Context)
{
	if (HasAuthority())
	{
		++HitCount;
		LastKineticEnergy = Context.KineticEnergy;
		LastProjectileDataAsset = Context.ProjectileDataAsset;
		ForceNetUpdate();
	}
}

void ANPSTestTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPSTestTarget, HitCount);
	DOREPLIFETIME(ANPSTestTarget, LastKineticEnergy);
}

ANPSTestFlyByListener::ANPSTestFlyByListener()
{
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
}

void ANPSTestFlyByListener::RecordFlyBy(
	APawn* NearbyPawn,
	FVector,
	float MissDistance,
	float)
{
	if (NearbyPawn == this)
	{
		++FlyByCount;
		LastMissDistance = MissDistance;
	}
}

ANPSTestLauncher::ANPSTestLauncher()
{
	bReplicates = true;
	SetReplicateMovement(true);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
	Launcher = CreateDefaultSubobject<UProjectileLauncherComponent>(TEXT("Launcher"));
	ProjectileData = CreateDefaultSubobject<UBallisticsProjectileDataAsset>(TEXT("ProjectileData"));
	LaunchCosmeticProfile =
		CreateDefaultSubobject<UBallisticsLaunchCosmeticProfile>(TEXT("LaunchCosmeticProfile"));
	ProjectileData->ProjectileClass = ANPSTestProjectile::StaticClass();
	ProjectileData->InitialSpeed = 10000.f;
	ProjectileData->Mass = 9.5f;
	ProjectileData->DragCoefficient = 0.f;
	ProjectileData->bOverrideAuthorityMode = true;
	Launcher->ProjectileDataAsset = ProjectileData;
	Launcher->LaunchCosmeticProfile = LaunchCosmeticProfile;
	Launcher->bRouteCosmeticEventsWithoutAssets = true;
}

void ANPSTestLauncher::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Launcher->OnAuthoritativeImpact.AddDynamic(
			this, &ANPSTestLauncher::RecordAuthoritativeImpact);
		Launcher->OnAuthoritativeFlyBy.AddDynamic(
			this, &ANPSTestLauncher::RecordAuthoritativeFlyBy);
	}
}

void ANPSTestLauncher::RecordAuthoritativeImpact(
	const FBallisticsAuthoritativeImpactEvent&)
{
	++AuthoritativeImpactCount;
}

void ANPSTestLauncher::RecordAuthoritativeFlyBy(
	const FBallisticsAuthoritativeFlyByEvent& Event)
{
	if (Event.Listener && Event.Listener->IsA<ANPSTestFlyByListener>())
	{
		++AuthoritativeFlyByCount;
	}
}

bool ANPSTestLauncher::ValidateBallisticsLaunch_Implementation(
	const FBallisticsLaunchValidationContext&)
{
	++LaunchValidationCount;
	return bAllowValidatedLaunches;
}

ANPSTestCosmeticLauncher::ANPSTestCosmeticLauncher()
{
	CosmeticSource = CreateDefaultSubobject<USkeletalMeshComponent>(
		TEXT("CosmeticSource"));
	CosmeticSource->SetupAttachment(SceneRoot);
	CosmeticSource->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CosmeticMesh(
		TEXT("/NPS/FirstPerson/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle"));
	if (CosmeticMesh.Succeeded())
	{
		CosmeticSource->SetSkeletalMeshAsset(CosmeticMesh.Object);
	}

	Launcher->SetLaunchEffectSourceComponent(CosmeticSource);
	Launcher->SetLaunchEffectSourceBinding(TEXT("Muzzle"), CosmeticSource);
	Launcher->SetLaunchEffectSourceBinding(TEXT("ShellEject"), CosmeticSource);
	Launcher->bRouteCosmeticEventsWithoutAssets = false;

	static ConstructorHelpers::FObjectFinder<UBallisticsLaunchCosmeticProfile>
		CosmeticProfile(TEXT(
			"/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile."
			"DA_NPS_LaunchProfile"));
	if (CosmeticProfile.Succeeded())
	{
		LaunchCosmeticProfile = CosmeticProfile.Object;
		Launcher->LaunchCosmeticProfile = CosmeticProfile.Object;
	}
}

ANPSTestAuthoredLauncher::ANPSTestAuthoredLauncher()
{
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
	Launcher = CreateDefaultSubobject<UProjectileLauncherComponent>(TEXT("Launcher"));
}

void ANPSTestOwnershipProbeProjectile::BeginPlay()
{
	OwnerObservedAtBeginPlay = GetOwner();
	InstigatorObservedAtBeginPlay = GetInstigator();
	Super::BeginPlay();
}

ANPSTestEnvironmentManager::ANPSTestEnvironmentManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
}

void ANPSTestEnvironmentManager::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		BuildEnvironment();
	}
}

void ANPSTestEnvironmentManager::BuildEnvironment()
{
	DemoLauncher = GetWorld()->SpawnActor<ANPSTestLauncher>(FVector::ZeroVector, FRotator::ZeroRotator);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector Location(3000.f, (Index - 1) * 500.f, 150.f);
		Targets.Add(GetWorld()->SpawnActor<ANPSTestTarget>(Location, FRotator::ZeroRotator));
	}
}

void ANPSTestEnvironmentManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bAutoRunDemonstration || !DemoLauncher || Targets.Num() != 3)
	{
		return;
	}

	DemoTimer += DeltaSeconds;
	if (DemoTimer >= 2.f)
	{
		DemoTimer = 0.f;
		LaunchNextDemonstration();
	}
}

void ANPSTestEnvironmentManager::LaunchNextDemonstration()
{
	static const ELaunchAuthorityMode Modes[] = {
		ELaunchAuthorityMode::ServerAuthoritative,
		ELaunchAuthorityMode::ClientPredicted,
		ELaunchAuthorityMode::ClientAuthoritative
	};

	const int32 Lane = DemoPhase % UE_ARRAY_COUNT(Modes);
	DemoLauncher->ProjectileData->AuthorityModeOverride = Modes[Lane];
	const FVector Origin(0.f, (Lane - 1) * 500.f, 150.f);
	DemoLauncher->Launcher->Launch(Origin, FVector::ForwardVector);
	++DemoPhase;
}

ANPSTestGameMode::ANPSTestGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
}

void ANPSTestGameMode::StartPlay()
{
	Super::StartPlay();
	if (HasAuthority())
	{
		GetWorld()->SpawnActor<ANPSTestEnvironmentManager>();
	}
}
