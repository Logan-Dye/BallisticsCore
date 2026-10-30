// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "NPSExampleEnvironment.h"

#include "BallisticsAggregateTick.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsDetonationComponent.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsLaunchContributorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Level.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "NPSExampleGuide.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileNetProxyComponent.h"
#include "ProjectileTracerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace NPSExamples
{
	static constexpr float LaneStartX = 1500.f;
	static constexpr float LaneSpacing = 430.f;

	FString WrapWorldText(const FString& Text, int32 MaximumLineLength)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		FString Result;
		FString Line;
		for (const FString& Word : Words)
		{
			if (!Line.IsEmpty() && Line.Len() + Word.Len() + 1 > MaximumLineLength)
			{
				Result += Line + TEXT("\n");
				Line.Reset();
			}
			if (!Line.IsEmpty())
			{
				Line += TEXT(" ");
			}
			Line += Word;
		}
		return Result + Line;
	}

	template <typename T>
	T* FindOrSpawnEnvironmentActor(
		UWorld* World,
		ULevel* TargetLevel,
		const FTransform& Transform,
		bool& bOutCreated)
	{
		bOutCreated = false;
		if (!World || !TargetLevel)
		{
			return nullptr;
		}

		for (AActor* Actor : TargetLevel->Actors)
		{
			if (T* Existing = Cast<T>(Actor))
			{
				return Existing;
			}
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = TargetLevel;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		T* Spawned = World->SpawnActor<T>(
			T::StaticClass(),
			Transform,
			SpawnParameters);
		bOutCreated = Spawned != nullptr;
		return Spawned;
	}

	template <typename T>
	T* LoadExampleAsset(const TCHAR* Path)
	{
		return Path && *Path ? LoadObject<T>(nullptr, Path) : nullptr;
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
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
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

ANPSExampleProjectile::ANPSExampleProjectile()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(SceneRoot);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.12f, 0.04f, 0.04f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Visual->SetStaticMesh(Sphere.Object);
	}

	Ballistics = CreateDefaultSubobject<UProjectileBallisticsComponent>(TEXT("Ballistics"));
	Tracer = CreateDefaultSubobject<UProjectileTracerComponent>(TEXT("Tracer"));
	NetProxy = CreateDefaultSubobject<UProjectileNetProxyComponent>(TEXT("NetProxy"));
	Detonation = CreateDefaultSubobject<UBallisticsDetonationComponent>(TEXT("Detonation"));
	NetProxy->InterpolationSpeed = 120.f;
}

ANPSExampleTarget::ANPSExampleTarget()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(35.f, 135.f, 150.f));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	RootComponent = Collision;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.7f, 2.7f, 3.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Visual->SetStaticMesh(Cube.Object);
	}

	Readout = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Readout"));
	Readout->SetupAttachment(Collision);
	Readout->SetRelativeLocation(FVector(0.f, 0.f, 210.f));
	Readout->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Readout->SetHorizontalAlignment(EHTA_Center);
	Readout->SetWorldSize(28.f);
	Readout->SetTextRenderColor(FColor(230, 240, 255));
}

void ANPSExampleTarget::BeginPlay()
{
	Super::BeginPlay();

	NPSExamples::ApplyColor(Visual, FLinearColor(0.04f, 0.18f, 0.28f));
	RefreshReadout();
}

void ANPSExampleTarget::OnBallisticDamage_Implementation(
	const FBallisticDamageContext& Context)
{
	if (!HasAuthority())
	{
		return;
	}

	++HitCount;
	LastKineticEnergy = Context.KineticEnergy;
	LastImpactLocation = Context.HitResult.ImpactPoint;
	RefreshReadout();
	ForceNetUpdate();
}

void ANPSExampleTarget::OnRep_TargetState()
{
	RefreshReadout();
}

void ANPSExampleTarget::RefreshReadout()
{
	if (Collision)
	{
		if (!RuntimePhysicalMaterial)
		{
			RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
		}
		RuntimePhysicalMaterial->SurfaceType = SurfaceType;
		Collision->SetPhysMaterialOverride(RuntimePhysicalMaterial);
	}

	if (Readout)
	{
		Readout->SetText(FText::FromString(FString::Printf(
			TEXT("%s\nHits: %d  Energy: %.0f J"),
			*TargetLabel,
			HitCount,
			LastKineticEnergy)));
	}
}

void ANPSExampleTarget::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPSExampleTarget, TargetLabel);
	DOREPLIFETIME(ANPSExampleTarget, SurfaceType);
	DOREPLIFETIME(ANPSExampleTarget, HitCount);
	DOREPLIFETIME(ANPSExampleTarget, LastKineticEnergy);
	DOREPLIFETIME(ANPSExampleTarget, LastImpactLocation);
}

ANPSExampleLauncher::ANPSExampleLauncher()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	Pedestal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal"));
	Pedestal->SetupAttachment(SceneRoot);
	Pedestal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pedestal->SetRelativeScale3D(FVector(0.7f, 0.7f, 0.8f));

	DirectionMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DirectionMarker"));
	DirectionMarker->SetupAttachment(SceneRoot);
	DirectionMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DirectionMarker->SetRelativeLocation(FVector(85.f, 0.f, 70.f));
	DirectionMarker->SetRelativeScale3D(FVector(1.6f, 0.12f, 0.12f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Pedestal->SetStaticMesh(Cube.Object);
		DirectionMarker->SetStaticMesh(Cube.Object);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(30.f);
	Label->SetTextRenderColor(FColor(120, 220, 255));

	GuideText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("GuideText"));
	GuideText->SetupAttachment(SceneRoot);
	GuideText->SetRelativeLocation(FVector(350.f, -175.f, 140.f));
	GuideText->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	GuideText->SetHorizontalAlignment(EHTA_Left);
	GuideText->SetVerticalAlignment(EVRTA_TextTop);
	GuideText->SetWorldSize(12.f);
	GuideText->SetTextRenderColor(FColor(185, 220, 240));

	Launcher = CreateDefaultSubobject<UProjectileLauncherComponent>(TEXT("Launcher"));
	LaunchContributor = CreateDefaultSubobject<UBallisticsLaunchContributorComponent>(
		TEXT("LaunchContributor"));
	Launcher->bEnableCosmeticRouting = true;
	Launcher->bRouteCosmeticEventsWithoutAssets = true;
	Launcher->CustomCosmeticRoutingRadius = 200000.f;
}

void ANPSExampleLauncher::Configure(
	UBallisticsProjectileDataAsset* InProjectileData,
	UBallisticsLaunchCosmeticProfile* InProfile,
	UBallisticsLaunchCosmeticModifier* InModifier,
	const FString& InLabel)
{
	if (!HasAuthority())
	{
		return;
	}

	ProjectileData = InProjectileData;
	LaunchProfile = InProfile;
	StationLabel = InLabel;
	TArray<FNPSExampleMapEntry> FallbackMaps;
	TArray<FNPSExampleStationEntry> FallbackStations;
	const UNPSExampleCatalog* Catalog = UNPSExampleCatalog::LoadDefaultCatalog();
	if (!Catalog)
	{
		UNPSExampleCatalog::BuildDefaultContent(FallbackMaps, FallbackStations);
	}
	const FNPSExampleStationEntry* Entry = Catalog
		? Catalog->FindStationByTitle(InLabel)
		: FallbackStations.FindByPredicate(
			[&InLabel](const FNPSExampleStationEntry& Candidate)
			{
				return Candidate.Title.ToString().Equals(InLabel, ESearchCase::IgnoreCase);
			});
	ExampleId = Entry ? Entry->ExampleId : NAME_None;
	LaunchContributor->SetModifier(InModifier);
	ApplyConfiguration();
	ForceNetUpdate();
}

void ANPSExampleLauncher::OnRep_Configuration()
{
	ApplyConfiguration();
}

void ANPSExampleLauncher::OnRep_Telemetry()
{
	RefreshGuideText();
}

void ANPSExampleLauncher::ApplyConfiguration()
{
	CachedSummary.Reset();
	CachedExpectedResult.Reset();
	if (Launcher)
	{
		Launcher->SetProjectileDataAsset(ProjectileData);
		if (HasAuthority())
		{
			Launcher->SetLaunchCosmeticProfile(LaunchProfile);
		}
	}
	if (Label)
	{
		Label->SetText(FText::FromString(StationLabel));
	}
	RefreshGuideText();
	NPSExamples::ApplyColor(Pedestal, FLinearColor(0.025f, 0.08f, 0.14f));
	NPSExamples::ApplyColor(DirectionMarker, FLinearColor(0.05f, 0.65f, 0.95f));
}

FVector ANPSExampleLauncher::GetLaunchOrigin() const
{
	return GetActorLocation() + GetActorForwardVector() * 180.f + FVector(0.f, 0.f, 85.f);
}

FVector ANPSExampleLauncher::GetLaunchDirection() const
{
	return GetActorForwardVector();
}

void ANPSExampleLauncher::LaunchExample()
{
	if (!HasAuthority() || !Launcher || !ProjectileData)
	{
		return;
	}

	const FVector Origin = GetLaunchOrigin();
	const FVector Direction = GetLaunchDirection();
	for (int32 Index = 0; Index < FMath::Clamp(ProjectilesPerCycle, 1, 64); ++Index)
	{
		Launcher->Launch(Origin, Direction);
		++LaunchCount;
	}
	RefreshGuideText();
	ForceNetUpdate();
}

void ANPSExampleLauncher::UpdateTelemetry(
	const ANPSExampleTarget* Target,
	int32 ActiveCount,
	int32 PoolSize)
{
	if (!HasAuthority())
	{
		return;
	}
	const float LateralOffset = Target && Target->HitCount > 0
		? Target->LastImpactLocation.Y - Target->GetActorLocation().Y
		: 0.f;
	const FString NewTelemetry = FString::Printf(
		TEXT("Requests %d  Hits %d\nImpact Y %+.0f cm\nActive %d  Pool %d"),
		LaunchCount,
		Target ? Target->HitCount : 0,
		LateralOffset,
		ActiveCount,
		PoolSize);
	if (TelemetryText == NewTelemetry)
	{
		return;
	}
	TelemetryText = NewTelemetry;
	RefreshGuideText();
	ForceNetUpdate();
}

void ANPSExampleLauncher::RefreshGuideText()
{
	if (!GuideText)
	{
		return;
	}
	if (CachedSummary.IsEmpty() || CachedExpectedResult.IsEmpty())
	{
		TArray<FNPSExampleMapEntry> FallbackMaps;
		TArray<FNPSExampleStationEntry> FallbackStations;
		const UNPSExampleCatalog* Catalog = UNPSExampleCatalog::LoadDefaultCatalog();
		if (!Catalog)
		{
			UNPSExampleCatalog::BuildDefaultContent(FallbackMaps, FallbackStations);
		}
		const FNPSExampleStationEntry* Entry = Catalog
			? Catalog->FindStation(ExampleId)
			: FallbackStations.FindByPredicate(
				[this](const FNPSExampleStationEntry& Candidate)
				{
					return Candidate.ExampleId == ExampleId;
				});
		CachedSummary = Entry && !Entry->ExampleId.IsNone()
			? Entry->Summary.ToString()
			: TEXT("Data-asset-driven NPS station.");
		CachedExpectedResult = Entry && !Entry->ExampleId.IsNone()
			? Entry->ExpectedResult.ToString()
			: TEXT("Inspect the projectile path and target response.");
	}
	const FString WrappedSummary = NPSExamples::WrapWorldText(CachedSummary, 36);
	const FString WrappedExpected = NPSExamples::WrapWorldText(CachedExpectedResult, 36);
	FString Live = TelemetryText.IsEmpty()
		? TEXT("Waiting for first sample")
		: TelemetryText;
	GuideText->SetText(FText::FromString(FString::Printf(
		TEXT("WHY\n%s\nEXPECT\n%s\nLIVE\n%s"),
		*WrappedSummary,
		*WrappedExpected,
		*Live)));
}

void ANPSExampleLauncher::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPSExampleLauncher, ProjectileData);
	DOREPLIFETIME(ANPSExampleLauncher, LaunchProfile);
	DOREPLIFETIME(ANPSExampleLauncher, StationLabel);
	DOREPLIFETIME(ANPSExampleLauncher, ExampleId);
	DOREPLIFETIME(ANPSExampleLauncher, LaunchCount);
	DOREPLIFETIME(ANPSExampleLauncher, TelemetryText);
}

ANPSExampleShowcase::ANPSExampleShowcase()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANPSExampleShowcase::BeginPlay()
{
	Super::BeginPlay();
	BuildVisualEnvironment();

	if (HasAuthority())
	{
		BuildGameplayEnvironment();
		if (UProjectileBallisticsSubsystem* Subsystem =
			GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			AggregateTicker = Subsystem->GetAggregateTicker();
			if (AggregateTicker.IsValid())
			{
				AggregateTicker->OnAggregateUpdate().AddUObject(
					this, &ANPSExampleShowcase::HandleAggregateUpdate);
			}
		}
		if (bAutoRun)
		{
			TimeUntilNextLaunch = 1.f;
		}
	}
}

void ANPSExampleShowcase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AggregateTicker.IsValid())
	{
		AggregateTicker->OnAggregateUpdate().RemoveAll(this);
		AggregateTicker.Reset();
	}
	FinishActiveSequence();
	Super::EndPlay(EndPlayReason);
}

void ANPSExampleShowcase::OnRep_Scenario()
{
	BuildVisualEnvironment();
}

void ANPSExampleShowcase::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPSExampleShowcase, Scenario);
}

void ANPSExampleShowcase::BuildVisualEnvironment()
{
	if (bVisualEnvironmentBuilt || !GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	bVisualEnvironmentBuilt = true;

	EnsureVisualLighting(GetWorld());

	AddVisualSection(TEXT("NETWORKED PROJECTILE SYSTEM - EXAMPLE LAB"), 0.f,
		FLinearColor(0.02f, 0.45f, 0.8f));

	switch (Scenario)
	{
	case ENPSExampleScenario::Showcase:
		AddVisualSection(TEXT("AUTHORITY MODES"), -3300.f, FLinearColor(0.1f, 0.55f, 0.95f));
		AddVisualSection(TEXT("TRAJECTORIES"), -1500.f, FLinearColor(0.15f, 0.8f, 0.5f));
		AddVisualSection(TEXT("MATERIAL RESPONSES"), 700.f, FLinearColor(0.95f, 0.65f, 0.1f));
		AddVisualSection(TEXT("MULTI PROJECTILE"), 2500.f, FLinearColor(0.75f, 0.25f, 0.95f));
		AddVisualSection(TEXT("EXPLOSIVES"), 4300.f, FLinearColor(0.95f, 0.2f, 0.1f));
		AddVisualSection(TEXT("COSMETICS AND DISTANCE"), 6500.f, FLinearColor(0.1f, 0.75f, 0.95f));
		AddVisualSection(TEXT("POOLING AND LOAD"), 9000.f, FLinearColor(0.85f, 0.85f, 0.9f));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			AddVisualLane(-3200.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.02f, 0.07f, 0.12f));
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			AddVisualLane(-1400.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.02f, 0.09f, 0.07f));
			AddVisualLane(4400.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.11f, 0.035f, 0.025f));
			AddVisualLane(6600.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.02f, 0.08f, 0.12f));
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			AddVisualLane(800.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.11f, 0.075f, 0.02f));
			AddVisualLane(2600.f + NPSExamples::LaneSpacing * Index, 7000.f,
				FLinearColor(0.08f, 0.025f, 0.11f));
		}
		AddVisualLane(9200.f, 7000.f, FLinearColor(0.07f, 0.07f, 0.08f));
		break;
	default:
		AddVisualSection(UEnum::GetDisplayValueAsText(Scenario).ToString(), -1000.f,
			FLinearColor(0.1f, 0.65f, 0.95f));
		{
			int32 LaneCount = 1;
			switch (Scenario)
			{
			case ENPSExampleScenario::AuthorityModes:
			case ENPSExampleScenario::MaterialResponses:
			case ENPSExampleScenario::MultiProjectile:
				LaneCount = 3;
				break;
			case ENPSExampleScenario::Trajectories:
			case ENPSExampleScenario::Explosives:
			case ENPSExampleScenario::Cosmetics:
				LaneCount = 4;
				break;
			default:
				break;
			}
			for (int32 Index = 0; Index < LaneCount; ++Index)
			{
				AddVisualLane(-900.f + NPSExamples::LaneSpacing * Index, 7000.f,
					FLinearColor(0.02f, 0.08f, 0.12f));
			}
		}
		break;
	}
}

void ANPSExampleShowcase::EnsureVisualLighting(UWorld* World, ULevel* TargetLevel)
{
	if (!World)
	{
		return;
	}

	ULevel* EnvironmentLevel =
		TargetLevel ? TargetLevel : World->PersistentLevel.Get();
	if (!EnvironmentLevel)
	{
		return;
	}

	bool bCreated = false;
	ADirectionalLight* Sun = NPSExamples::FindOrSpawnEnvironmentActor<ADirectionalLight>(
		World,
		EnvironmentLevel,
		FTransform(FRotator(-45.f, -35.f, 0.f)),
		bCreated);
	if (Sun && bCreated)
	{
		Sun->SetReplicates(false);
#if WITH_EDITOR
		Sun->SetActorLabel(TEXT("NPS Sun"));
#endif
		if (UDirectionalLightComponent* Light =
			Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(7.f);
			Light->SetLightColor(FLinearColor(1.f, 0.94f, 0.84f));
			Light->SetAtmosphereSunLight(true);
		}
	}

	ASkyAtmosphere* Atmosphere =
		NPSExamples::FindOrSpawnEnvironmentActor<ASkyAtmosphere>(
			World,
			EnvironmentLevel,
			FTransform::Identity,
			bCreated);
	if (Atmosphere && bCreated)
	{
		Atmosphere->SetReplicates(false);
#if WITH_EDITOR
		Atmosphere->SetActorLabel(TEXT("NPS Sky Atmosphere"));
#endif
	}

	AExponentialHeightFog* HeightFog =
		NPSExamples::FindOrSpawnEnvironmentActor<AExponentialHeightFog>(
			World,
			EnvironmentLevel,
			FTransform::Identity,
			bCreated);
	if (HeightFog && bCreated)
	{
		HeightFog->SetReplicates(false);
#if WITH_EDITOR
		HeightFog->SetActorLabel(TEXT("NPS Height Fog"));
#endif
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			Fog->SetFogDensity(0.02f);
			Fog->SetFogHeightFalloff(0.2f);
		}
	}

	ASkyLight* SkyLight = NPSExamples::FindOrSpawnEnvironmentActor<ASkyLight>(
		World,
		EnvironmentLevel,
		FTransform::Identity,
		bCreated);
	if (SkyLight && bCreated)
	{
		SkyLight->SetReplicates(false);
#if WITH_EDITOR
		SkyLight->SetActorLabel(TEXT("NPS Sky Light"));
#endif
		if (USkyLightComponent* Light = SkyLight->GetLightComponent())
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(1.f);
			Light->SetRealTimeCapture(true);
		}
	}
}

void ANPSExampleShowcase::BuildGameplayEnvironment()
{
	switch (Scenario)
	{
	case ENPSExampleScenario::Showcase:
		BuildAuthorityExamples(-3200.f);
		BuildTrajectoryExamples(-1400.f);
		BuildMaterialExamples(800.f);
		BuildMultiProjectileExamples(2600.f);
		BuildExplosiveExamples(4400.f);
		BuildCosmeticExamples(6600.f);
		BuildPerformanceExamples(9200.f);
		break;
	case ENPSExampleScenario::AuthorityModes:
		BuildAuthorityExamples(-900.f);
		break;
	case ENPSExampleScenario::Trajectories:
		BuildTrajectoryExamples(-900.f);
		break;
	case ENPSExampleScenario::MaterialResponses:
		BuildMaterialExamples(-900.f);
		break;
	case ENPSExampleScenario::MultiProjectile:
		BuildMultiProjectileExamples(-900.f);
		break;
	case ENPSExampleScenario::Explosives:
		BuildExplosiveExamples(-900.f);
		break;
	case ENPSExampleScenario::Cosmetics:
		BuildCosmeticExamples(-900.f);
		break;
	case ENPSExampleScenario::NetworkScale:
		BuildNetworkScaleExamples(-900.f);
		break;
	case ENPSExampleScenario::Performance:
		BuildPerformanceExamples(-900.f);
		break;
	}
}

void ANPSExampleShowcase::BuildAuthorityExamples(float BaseY)
{
	AddStation(TEXT("SERVER AUTHORITATIVE"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ServerAuth.DA_NPS_ServerAuth"), BaseY);
	AddStation(TEXT("CLIENT PREDICTED"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientPredicted.DA_NPS_ClientPredicted"), BaseY + NPSExamples::LaneSpacing);
	AddStation(TEXT("CLIENT AUTHORITATIVE"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientAuth.DA_NPS_ClientAuth"), BaseY + NPSExamples::LaneSpacing * 2.f);
}

void ANPSExampleShowcase::BuildTrajectoryExamples(float BaseY)
{
	AddStation(TEXT("HIGH VELOCITY PROJECTILE"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"), BaseY, 5000.f);
	AddStation(TEXT("SUBSONIC HEAVY"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Subsonic.DA_NPS_Subsonic"), BaseY + NPSExamples::LaneSpacing, 3500.f);
	AddStation(TEXT("ARROW - GRAVITY ARC"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Arrow.DA_NPS_Arrow"), BaseY + NPSExamples::LaneSpacing * 2.f, 2500.f, 8.f);
	AddStation(TEXT("CROSSWIND DRIFT"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Crosswind.DA_NPS_Crosswind"), BaseY + NPSExamples::LaneSpacing * 3.f, 4500.f);
}

void ANPSExampleShowcase::BuildMaterialExamples(float BaseY)
{
	AddStation(TEXT("PENETRATION"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Penetrator.DA_NPS_Penetrator"), BaseY, 3000.f);
	AddStation(TEXT("RICOCHET"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Ricochet.DA_NPS_Ricochet"), BaseY + NPSExamples::LaneSpacing, 3000.f, -7.f);
	AddStation(TEXT("EMBED / SLIDE"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Arrow.DA_NPS_Arrow"), BaseY + NPSExamples::LaneSpacing * 2.f, 2600.f);
}

void ANPSExampleShowcase::BuildMultiProjectileExamples(float BaseY)
{
	AddStation(TEXT("HITSCAN"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Hitscan.DA_NPS_Hitscan"), BaseY, 5000.f);
	AddStation(TEXT("RANDOM CONE SPREAD"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadRandom.DA_NPS_SpreadRandom"), BaseY + NPSExamples::LaneSpacing, 2200.f);
	AddStation(TEXT("FIXED PATTERN SPREAD"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadPattern.DA_NPS_SpreadPattern"), BaseY + NPSExamples::LaneSpacing * 2.f, 2200.f);
}

void ANPSExampleShowcase::BuildExplosiveExamples(float BaseY)
{
	AddStation(TEXT("IMPACT DETONATION"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveImpact.DA_NPS_ExplosiveImpact"), BaseY, 2600.f);
	AddStation(TEXT("FUSE TIMER"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveFuse.DA_NPS_ExplosiveFuse"), BaseY + NPSExamples::LaneSpacing, 3000.f, 12.f);
	AddStation(TEXT("DETONATE ON REST"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveRest.DA_NPS_ExplosiveRest"), BaseY + NPSExamples::LaneSpacing * 2.f, 2300.f, 10.f);
	AddStation(TEXT("MANUAL DETONATION"), TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveManual.DA_NPS_ExplosiveManual"), BaseY + NPSExamples::LaneSpacing * 3.f, 2400.f, 8.f);
}

void ANPSExampleShowcase::BuildCosmeticExamples(float BaseY)
{
	AddStation(
		TEXT("BASE LAUNCH PROFILE"),
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
		BaseY,
		4500.f,
		0.f,
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"));
	AddStation(
		TEXT("MODIFIED LAUNCH PROFILE"),
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
		BaseY + NPSExamples::LaneSpacing,
		4500.f,
		0.f,
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"),
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchModifier.DA_NPS_LaunchModifier"));
	AddStation(
		TEXT("SUSTAINED SEQUENCE"),
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
		BaseY + NPSExamples::LaneSpacing * 2.f,
		4500.f,
		0.f,
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"),
		nullptr,
		4,
		true);
	BuildNetworkScaleExamples(BaseY + NPSExamples::LaneSpacing * 3.f);
}

void ANPSExampleShowcase::BuildNetworkScaleExamples(float BaseY)
{
	AddStation(
		TEXT("COSMETICS BEYOND 100 CM NET CULL"),
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
		BaseY,
		7000.f,
		0.f,
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"),
		nullptr,
		1,
		false,
		100.f);
}

void ANPSExampleShowcase::BuildPerformanceExamples(float BaseY)
{
	AddStation(
		TEXT("POOLED 32 PROJECTILE SPREAD"),
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Stress.DA_NPS_Stress"),
		BaseY,
		6000.f,
		4.f);
}

ANPSExampleLauncher* ANPSExampleShowcase::AddStation(
	const FString& Label,
	const TCHAR* ProjectileAssetPath,
	float Y,
	float TargetDistance,
	float PitchDegrees,
	const TCHAR* ProfileAssetPath,
	const TCHAR* ModifierAssetPath,
	int32 ProjectilesPerCycle,
	bool bSustained,
	float NetCullDistance)
{
	if (!HasAuthority() || !GetWorld())
	{
		return nullptr;
	}

	UBallisticsProjectileDataAsset* Projectile =
		NPSExamples::LoadExampleAsset<UBallisticsProjectileDataAsset>(ProjectileAssetPath);
	if (!Projectile)
	{
		Projectile = NewObject<UBallisticsProjectileDataAsset>(this);
		Projectile->DisplayName = FText::FromString(Label);
		Projectile->ProjectileID = FName(*Label.Replace(TEXT(" "), TEXT("_")));
		Projectile->ProjectileClass = ANPSExampleProjectile::StaticClass();
		Projectile->InitialSpeed = 30000.f;
		Projectile->DragCoefficient = 0.2f;
		Projectile->bOverrideAuthorityMode = true;
		Projectile->AuthorityModeOverride = ELaunchAuthorityMode::ServerAuthoritative;
		RuntimeFallbackAssets.Add(Projectile);
	}

	UBallisticsLaunchCosmeticProfile* Profile =
		NPSExamples::LoadExampleAsset<UBallisticsLaunchCosmeticProfile>(ProfileAssetPath);
	UBallisticsLaunchCosmeticModifier* Modifier =
		NPSExamples::LoadExampleAsset<UBallisticsLaunchCosmeticModifier>(ModifierAssetPath);

	const FVector LauncherLocation(NPSExamples::LaneStartX, Y, 70.f);
	ANPSExampleLauncher* ExampleLauncher =
		GetWorld()->SpawnActor<ANPSExampleLauncher>(
			LauncherLocation,
			FRotator(PitchDegrees, 0.f, 0.f));
	if (!ExampleLauncher)
	{
		return nullptr;
	}

	ExampleLauncher->SetNetCullDistanceSquared(
		FMath::Square(FMath::Max(NetCullDistance, 1.f)));
	ExampleLauncher->ProjectilesPerCycle = ProjectilesPerCycle;
	ExampleLauncher->bDemonstrateSustainedSequence = bSustained;
	ExampleLauncher->Configure(Projectile, Profile, Modifier, Label);
	Launchers.Add(ExampleLauncher);

	const FTransform TargetTransform(
		FRotator::ZeroRotator,
		FVector(NPSExamples::LaneStartX + TargetDistance, Y, 170.f));
	ANPSExampleTarget* Target =
		GetWorld()->SpawnActorDeferred<ANPSExampleTarget>(
			ANPSExampleTarget::StaticClass(),
			TargetTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Target)
	{
		Target->TargetLabel = Label;
		Target->SurfaceType = SurfaceType1;
		UGameplayStatics::FinishSpawningActor(Target, TargetTransform);
		Targets.Add(Target);
	}

	return ExampleLauncher;
}

void ANPSExampleShowcase::AddVisualSection(
	const FString& Label,
	float Y,
	const FLinearColor& Color)
{
	if (!GetWorld())
	{
		return;
	}

	AActor* TextActor = GetWorld()->SpawnActor<AActor>(
		FVector(NPSExamples::LaneStartX - 350.f, Y, 350.f),
		FRotator::ZeroRotator);
	if (!TextActor)
	{
		return;
	}
	TextActor->SetReplicates(false);

	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(TextActor, TEXT("SectionLabel"));
	TextActor->SetRootComponent(Text);
	TextActor->AddInstanceComponent(Text);
	Text->RegisterComponent();
	Text->SetText(FText::FromString(Label));
	Text->SetHorizontalAlignment(EHTA_Left);
	Text->SetWorldSize(55.f);
	Text->SetTextRenderColor(Color.ToFColor(true));
	Text->SetWorldRotation(FRotator(0.f, 180.f, 0.f));
}

void ANPSExampleShowcase::AddVisualLane(
	float Y,
	float Length,
	const FLinearColor& Color)
{
	if (!GetWorld())
	{
		return;
	}

	AStaticMeshActor* Lane = GetWorld()->SpawnActor<AStaticMeshActor>(
		FVector(NPSExamples::LaneStartX + Length * 0.5f, Y, 5.f),
		FRotator::ZeroRotator);
	if (!Lane)
	{
		return;
	}
	Lane->SetReplicates(false);
	Lane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Lane->GetStaticMeshComponent()->SetWorldScale3D(FVector(Length / 100.f, 3.2f, 0.08f));
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Lane->GetStaticMeshComponent()->SetStaticMesh(Cube);
	}
	NPSExamples::ApplyColor(Lane->GetStaticMeshComponent(), Color);
}

void ANPSExampleShowcase::FinishActiveSequence()
{
	if (ActiveSequenceLauncher && ActiveSequenceLauncher->Launcher)
	{
		ActiveSequenceLauncher->Launcher->EndLaunchSequence();
		ActiveSequenceLauncher = nullptr;
	}
}

void ANPSExampleShowcase::DetonatePendingManualExamples()
{
	if (!HasAuthority())
	{
		return;
	}

	for (TActorIterator<ANPSExampleProjectile> It(GetWorld()); It; ++It)
	{
		if (It->Detonation &&
			It->Detonation->ExplosiveDataAsset &&
			It->Detonation->ExplosiveDataAsset->DetonationTrigger == EDetonationTrigger::Manual &&
			!It->Detonation->HasDetonated())
		{
			It->Detonation->Detonate();
		}
	}
}

void ANPSExampleShowcase::HandleAggregateUpdate(float DeltaTime)
{
	if (!HasAuthority())
	{
		return;
	}

	TimeUntilTelemetryUpdate -= FMath::Max(DeltaTime, 0.f);
	if (TimeUntilTelemetryUpdate <= 0.f)
	{
		TimeUntilTelemetryUpdate = 0.25f;
		RefreshStationTelemetry();
	}

	if (TimeUntilManualDetonation >= 0.f)
	{
		TimeUntilManualDetonation -= FMath::Max(DeltaTime, 0.f);
		if (TimeUntilManualDetonation <= 0.f)
		{
			TimeUntilManualDetonation = -1.f;
			DetonatePendingManualExamples();
		}
	}

	if (!bAutoRun || TimeUntilNextLaunch < 0.f)
	{
		return;
	}

	TimeUntilNextLaunch -= FMath::Max(DeltaTime, 0.f);
	if (TimeUntilNextLaunch <= 0.f)
	{
		TimeUntilNextLaunch = FMath::Max(LaunchInterval, 0.1f);
		LaunchNextExample();
	}
}

void ANPSExampleShowcase::RefreshStationTelemetry()
{
	const UProjectileBallisticsSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>()
		: nullptr;
	const int32 ActiveCount = Subsystem ? Subsystem->GetActiveProjectileCount() : 0;
	const int32 PoolSize = Subsystem ? Subsystem->GetPoolSize() : 0;
	for (int32 Index = 0; Index < Launchers.Num(); ++Index)
	{
		if (ANPSExampleLauncher* ExampleLauncher = Launchers[Index])
		{
			ExampleLauncher->UpdateTelemetry(
				Targets.IsValidIndex(Index) ? Targets[Index] : nullptr,
				ActiveCount,
				PoolSize);
		}
	}
}

void ANPSExampleShowcase::LaunchNextExample()
{
	if (!HasAuthority() || Launchers.IsEmpty())
	{
		return;
	}

	FinishActiveSequence();
	DetonatePendingManualExamples();

	ANPSExampleLauncher* ExampleLauncher =
		Launchers[NextLauncherIndex % Launchers.Num()];
	++NextLauncherIndex;
	if (!ExampleLauncher)
	{
		return;
	}

	if (ExampleLauncher->bDemonstrateSustainedSequence &&
		ExampleLauncher->Launcher)
	{
		ExampleLauncher->Launcher->BeginLaunchSequence(
			ExampleLauncher->GetLaunchOrigin(),
			ExampleLauncher->GetLaunchDirection(),
			12.f);
		ActiveSequenceLauncher = ExampleLauncher;
	}
	ExampleLauncher->LaunchExample();

	if (ExampleLauncher->ProjectileData &&
		ExampleLauncher->ProjectileData->ExplosiveConfig &&
		ExampleLauncher->ProjectileData->ExplosiveConfig->DetonationTrigger ==
			EDetonationTrigger::Manual)
	{
		TimeUntilManualDetonation = 0.35f;
	}
}

void ANPSExampleShowcase::LaunchAllExamplesOnce()
{
	if (!HasAuthority())
	{
		return;
	}
	for (ANPSExampleLauncher* ExampleLauncher : Launchers)
	{
		if (ExampleLauncher)
		{
			ExampleLauncher->LaunchExample();
		}
	}
}

ANPSExampleGameMode::ANPSExampleGameMode()
{
	DefaultPawnClass = ANPSExamplePawn::StaticClass();
	HUDClass = ANPSExampleGuideHUD::StaticClass();
}

void ANPSExampleGameMode::StartPlay()
{
	Super::StartPlay();

	if (!HasAuthority())
	{
		return;
	}

	for (TActorIterator<ANPSExampleShowcase> It(GetWorld()); It; ++It)
	{
		return;
	}

	GetWorld()->SpawnActor<ANPSExampleShowcase>();
}
