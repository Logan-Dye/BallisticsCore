// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "CQTest.h"

#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS

#include "BallisticsAggregateTick.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsDetonationComponent.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsImpactBudget.h"
#include "BallisticsLaunchContributorComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/MapTestSpawner.h"
#include "Components/BoxComponent.h"
#include "Components/AudioComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraVariant.h"
#include "NPSExampleEnvironment.h"
#include "NPSExampleCatalog.h"
#include "NPSExampleGuide.h"
#include "NPSFirstPersonExample.h"
#include "NPSTestEnvironment.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileNetProxyComponent.h"
#include "ProjectileTracerComponent.h"
#include "BallisticsSpreadDataAsset.h"
#include "BallisticsTrajectoryLibrary.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"

TEST_CLASS_WITH_FLAGS(
	FNPSFeatureTests,
	"NPS.Features",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TUniquePtr<FMapTestSpawner> Spawner;
	TWeakObjectPtr<UBallisticsDetonationComponent> PendingDetonation;
	TWeakObjectPtr<AActor> PendingTracerActor;
	TWeakObjectPtr<UWorld> PendingCosmeticWorld;

	BEFORE_EACH()
	{
		PendingDetonation.Reset();
		PendingTracerActor.Reset();
		PendingCosmeticWorld.Reset();
		Spawner = FMapTestSpawner::CreateFromTempLevel(TestCommandBuilder);
		ASSERT_THAT(IsNotNull(Spawner));
		Spawner->AddWaitUntilLoadedCommand(TestRunner);
	}

	TEST_METHOD(Hitscan_ThroughClearLane_HitsTarget)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(2000.f, 0.f, 0.f));

			FHitscanConfig Config;
			Config.MaxRange = 5000.f;
			Config.MaxPenetrations = 0;
			Config.MaxRicochets = 0;
			const FHitscanResult Result = UBallisticsHitscanLibrary::PerformHitscan(
				&Spawner->GetWorld(), FVector::ZeroVector, FVector::ForwardVector, Config);

			ASSERT_THAT(AreEqual(Result.Hits.Num(), 1));
			ASSERT_THAT(IsTrue(Result.Hits[0].HitResult.GetActor() == &Target));
		});
	}

	TEST_METHOD(Hitscan_Radial_ProducesRequestedTraceCount)
	{
		TestCommandBuilder.Do([this]()
		{
			FHitscanConfig Config;
			Config.MaxRange = 1000.f;
			Config.MaxPenetrations = 0;
			Config.MaxRicochets = 0;
			Config.RandomSeed = 42;
			const TArray<FHitscanResult> Results = UBallisticsHitscanLibrary::PerformRadialHitscan(
				&Spawner->GetWorld(), FVector::ZeroVector, 16, Config);
			ASSERT_THAT(AreEqual(Results.Num(), 16));
			for (const FHitscanResult& Result : Results)
			{
				ASSERT_THAT(IsTrue(Result.PathPoints.Num() >= 2));
			}
		});
	}

	TEST_METHOD(Pooling_PreWarmReleaseAcquire_ReusesActor)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			Subsystem->PreWarmPool(ANPSTestProjectile::StaticClass(), 3);
			ASSERT_THAT(AreEqual(Subsystem->GetPoolSize(), 3));

			AActor* First = Subsystem->AcquireProjectile(
				ANPSTestProjectile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			ASSERT_THAT(IsNotNull(First));
			ASSERT_THAT(AreEqual(Subsystem->GetPoolSize(), 2));

			Subsystem->ReleaseProjectile(First);
			ASSERT_THAT(AreEqual(Subsystem->GetPoolSize(), 3));
			AActor* Reused = Subsystem->AcquireProjectile(
				ANPSTestProjectile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			ASSERT_THAT(IsTrue(Reused == First));
		});
	}

	TEST_METHOD(Pooling_AcquireAssignsOwnershipBeforeFreshLifecycleAndReuse)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestTarget& FirstOwner = Spawner->SpawnActor<ANPSTestTarget>();
			ANPSTestFlyByListener& FirstInstigator =
				Spawner->SpawnActor<ANPSTestFlyByListener>();
			ANPSTestOwnershipProbeProjectile* Fresh =
				Cast<ANPSTestOwnershipProbeProjectile>(Subsystem->AcquireProjectile(
					ANPSTestOwnershipProbeProjectile::StaticClass(),
					FVector::ZeroVector,
					FRotator::ZeroRotator,
					&FirstOwner,
					&FirstInstigator));
			ASSERT_THAT(IsNotNull(Fresh));
			ASSERT_THAT(IsTrue(Fresh->OwnerObservedAtBeginPlay == &FirstOwner));
			ASSERT_THAT(IsTrue(Fresh->InstigatorObservedAtBeginPlay == &FirstInstigator));

			Subsystem->ReleaseProjectile(Fresh);
			ANPSTestTarget& SecondOwner = Spawner->SpawnActor<ANPSTestTarget>();
			ANPSTestFlyByListener& SecondInstigator =
				Spawner->SpawnActor<ANPSTestFlyByListener>();
			AActor* Reused = Subsystem->AcquireProjectile(
				ANPSTestOwnershipProbeProjectile::StaticClass(),
				FVector(100.f, 0.f, 0.f),
				FRotator::ZeroRotator,
				&SecondOwner,
				&SecondInstigator);
			ASSERT_THAT(IsTrue(Reused == Fresh));
			ASSERT_THAT(IsTrue(Reused->GetOwner() == &SecondOwner));
			ASSERT_THAT(IsTrue(Reused->GetInstigator() == &SecondInstigator));
		});
	}

	TEST_METHOD(ClientAuthoritative_DamageUsesLaunchTimeProjectileData)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			TestLauncher.SetActorLocation(FVector::ZeroVector);
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));

			UBallisticsProjectileDataAsset* LaunchData = TestLauncher.ProjectileData;
			LaunchData->AuthorityModeOverride = ELaunchAuthorityMode::ClientAuthoritative;
			FProjectileInitialConditions Canonical = LaunchData->BuildInitialConditions();
			Canonical.Origin = FVector(0.f, 0.f, 100.f);
			Canonical.Direction = FVector::ForwardVector;
			Canonical.RandomSeed = 42;
			const float ClientTimestamp = Spawner->GetWorld().GetTimeSeconds();
			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				0,
				Canonical,
				ClientTimestamp);

			UBallisticsProjectileDataAsset* Replacement =
				NewObject<UBallisticsProjectileDataAsset>(&TestLauncher);
			Replacement->Mass = 99.f;
			TestLauncher.Launcher->ProjectileDataAsset = Replacement;

			FProjectileFlightLog FlightLog;
			FlightLog.LaunchID = 0;
			FlightLog.ClientTimestamp = ClientTimestamp;
			FlightLog.InitialConditions = Canonical;

			FProjectileFlightSegment Segment;
			Segment.TimeAtImpact = 0.1f;
			const FProjectileSnapshot Expected =
				UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(
					Canonical,
					Segment.TimeAtImpact);
			Segment.StartLocation = Expected.Position -
				Expected.Velocity.GetSafeNormal() * 25.f;
			Segment.EndLocation = Expected.Position;
			Segment.ImpactResponse = EImpactResponse::Embed;
			Segment.ImpactNormal = -FVector::ForwardVector;
			Segment.HitActor = &Target;
			Segment.SpeedAtStart = Expected.Speed;
			FlightLog.Segments.Add(Segment);

			TestLauncher.Launcher->SubmitFlightLogForTests(FlightLog);
			ASSERT_THAT(AreEqual(Target.HitCount, 1));
			ASSERT_THAT(IsTrue(Target.LastProjectileDataAsset == LaunchData));
			ASSERT_THAT(IsTrue(
				Target.LastProjectileDataAsset != TestLauncher.Launcher->ProjectileDataAsset));
		});
	}

	TEST_METHOD(ClientAuthoritative_IncrementalReportsDispatchNewImpactsExactlyOnce)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			ANPSTestTarget& FirstTarget = Spawner->SpawnActor<ANPSTestTarget>();
			ANPSTestTarget& SecondTarget = Spawner->SpawnActor<ANPSTestTarget>();
			TestLauncher.SetActorLocation(FVector::ZeroVector);

			UBallisticsProjectileDataAsset* LaunchData = TestLauncher.ProjectileData;
			LaunchData->AuthorityModeOverride = ELaunchAuthorityMode::ClientAuthoritative;
			FProjectileInitialConditions Canonical = LaunchData->BuildInitialConditions();
			Canonical.Origin = FVector(0.f, 0.f, 100.f);
			Canonical.Direction = FVector::ForwardVector;
			Canonical.RandomSeed = 77;
			const float ClientTimestamp = Spawner->GetWorld().GetTimeSeconds();
			constexpr int32 LaunchID = 77;
			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				LaunchID,
				Canonical,
				ClientTimestamp);

			const FProjectileSnapshot FirstExpected =
				UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Canonical, 0.1f);
			const FProjectileSnapshot SecondExpected =
				UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Canonical, 0.2f);
			FirstTarget.SetActorLocation(FirstExpected.Position);
			SecondTarget.SetActorLocation(SecondExpected.Position);

			FProjectileFlightSegment FirstSegment;
			FirstSegment.StartLocation = FirstExpected.Position -
				FirstExpected.Velocity.GetSafeNormal() * 25.f;
			FirstSegment.EndLocation = FirstExpected.Position;
			FirstSegment.ImpactResponse = EImpactResponse::Penetrate;
			FirstSegment.ImpactNormal = -FVector::ForwardVector;
			FirstSegment.HitActor = &FirstTarget;
			FirstSegment.SpeedAtStart = FirstExpected.Speed;
			FirstSegment.TimeAtImpact = 0.1f;

			FProjectileFlightLog FirstReport;
			FirstReport.LaunchID = LaunchID;
			FirstReport.ClientTimestamp = ClientTimestamp;
			FirstReport.InitialConditions = Canonical;
			FirstReport.ReportSequence = 0;
			FirstReport.bFinalReport = false;
			FirstReport.Segments.Add(FirstSegment);

			TestLauncher.Launcher->SubmitFlightLogForTests(FirstReport);
			ASSERT_THAT(AreEqual(FirstTarget.HitCount, 1));
			ASSERT_THAT(AreEqual(SecondTarget.HitCount, 0));
			ASSERT_THAT(AreEqual(TestLauncher.AuthoritativeImpactCount, 1));

			TestLauncher.Launcher->SubmitFlightLogForTests(FirstReport);
			ASSERT_THAT(AreEqual(FirstTarget.HitCount, 1));
			ASSERT_THAT(AreEqual(TestLauncher.AuthoritativeImpactCount, 1));
			FirstTarget.SetActorLocation(FVector(100000.f, 0.f, 0.f));

			FProjectileFlightSegment SecondSegment;
			SecondSegment.StartLocation = FirstExpected.Position + FVector(100.f, 0.f, 0.f);
			SecondSegment.EndLocation = SecondExpected.Position;
			SecondSegment.ImpactResponse = EImpactResponse::Embed;
			SecondSegment.ImpactNormal = -FVector::ForwardVector;
			SecondSegment.HitActor = &SecondTarget;
			SecondSegment.SpeedAtStart = SecondExpected.Speed;
			SecondSegment.TimeAtImpact = 0.2f;

			FProjectileFlightLog FinalReport = FirstReport;
			FinalReport.ReportSequence = 1;
			FinalReport.bFinalReport = true;
			FinalReport.Segments.Add(SecondSegment);
			TestLauncher.Launcher->SubmitFlightLogForTests(FinalReport);

			ASSERT_THAT(AreEqual(FirstTarget.HitCount, 1));
			ASSERT_THAT(AreEqual(SecondTarget.HitCount, 1));
			ASSERT_THAT(AreEqual(TestLauncher.AuthoritativeImpactCount, 2));
		});
	}

	TEST_METHOD(LauncherAdmission_ServerLocalLaunchesBypassCreditsAndCadence)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			TestLauncher.ProjectileData->AuthorityModeOverride =
				ELaunchAuthorityMode::ServerAuthoritative;
			TestLauncher.Launcher->bRequireLaunchCredits = true;
			TestLauncher.Launcher->MinLaunchInterval = 10.f;

			int32 Before = 0;
			for (TActorIterator<ANPSTestProjectile> It(&Spawner->GetWorld()); It; ++It)
			{
				++Before;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				TestLauncher.Launcher->Launch(
					FVector(0.f, Index * 100.f, 100.f),
					FVector::UpVector);
			}

			int32 After = 0;
			for (TActorIterator<ANPSTestProjectile> It(&Spawner->GetWorld()); It; ++It)
			{
				++After;
			}
			ASSERT_THAT(AreEqual(After - Before, 4));
			ASSERT_THAT(AreEqual(
				TestLauncher.Launcher->GetAvailableLaunchCredits(),
				0));
		});
	}

	TEST_METHOD(LauncherAdmission_OwnerChangeAndExplicitClearRemoveCredits)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			AActor& FirstOwner = Spawner->SpawnActor<AActor>();
			AActor& SecondOwner = Spawner->SpawnActor<AActor>();
			TestLauncher.SetOwner(&FirstOwner);
			TestLauncher.Launcher->GrantLaunchCredits(2, 5.f);
			ASSERT_THAT(AreEqual(
				TestLauncher.Launcher->GetAvailableLaunchCredits(),
				2));

			TestLauncher.SetOwner(&SecondOwner);
			ASSERT_THAT(AreEqual(
				TestLauncher.Launcher->GetAvailableLaunchCredits(),
				0));

			TestLauncher.Launcher->GrantLaunchCredits(1, 5.f);
			TestLauncher.Launcher->ClearLaunchCredits();
			ASSERT_THAT(AreEqual(
				TestLauncher.Launcher->GetAvailableLaunchCredits(),
				0));
		});
	}

	TEST_METHOD(ClientAuthoritative_RejectsSpoofedPhysicalValues)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));
			TestLauncher.ProjectileData->AuthorityModeOverride =
				ELaunchAuthorityMode::ClientAuthoritative;

			FProjectileInitialConditions Canonical =
				TestLauncher.ProjectileData->BuildInitialConditions();
			Canonical.Origin = FVector(0.f, 0.f, 100.f);
			Canonical.Direction = FVector::ForwardVector;
			Canonical.RandomSeed = 71;
			const float Timestamp = Spawner->GetWorld().GetTimeSeconds();

			auto MakeLog = [&](int32 LaunchID)
			{
				FProjectileFlightLog Log;
				Log.LaunchID = LaunchID;
				Log.ClientTimestamp = Timestamp;
				Log.InitialConditions = Canonical;
				FProjectileFlightSegment Segment;
				Segment.TimeAtImpact = 0.1f;
				const FProjectileSnapshot Expected =
					UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(
						Canonical,
						Segment.TimeAtImpact);
				Segment.StartLocation = Expected.Position -
					Expected.Velocity.GetSafeNormal() * 25.f;
				Segment.EndLocation = Expected.Position;
				Segment.ImpactResponse = EImpactResponse::Embed;
				Segment.ImpactNormal = -FVector::ForwardVector;
				Segment.HitActor = &Target;
				Segment.SpeedAtStart = Expected.Speed;
				Log.Segments.Add(Segment);
				return Log;
			};

			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				10,
				Canonical,
				Timestamp);
			FProjectileFlightLog MassSpoof = MakeLog(10);
			MassSpoof.InitialConditions.Mass *= 100.f;
			TestLauncher.Launcher->SubmitFlightLogForTests(MassSpoof);
			ASSERT_THAT(AreEqual(Target.HitCount, 0));

			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				11,
				Canonical,
				Timestamp);
			FProjectileFlightLog SpeedSpoof = MakeLog(11);
			SpeedSpoof.Segments[0].SpeedAtStart *= 10.f;
			TestLauncher.Launcher->SubmitFlightLogForTests(SpeedSpoof);
			ASSERT_THAT(AreEqual(Target.HitCount, 0));
		});
	}

	TEST_METHOD(ClientAuthoritative_RejectsFutureTimestampAndDisconnectedPath)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));
			TestLauncher.ProjectileData->AuthorityModeOverride =
				ELaunchAuthorityMode::ClientAuthoritative;

			FProjectileInitialConditions Canonical =
				TestLauncher.ProjectileData->BuildInitialConditions();
			Canonical.Origin = FVector(0.f, 0.f, 100.f);
			Canonical.Direction = FVector::ForwardVector;
			Canonical.RandomSeed = 72;
			const float Now = Spawner->GetWorld().GetTimeSeconds();

			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				20,
				Canonical,
				Now);
			FProjectileFlightLog FutureLog;
			FutureLog.LaunchID = 20;
			FutureLog.ClientTimestamp = Now + 1.f;
			FutureLog.InitialConditions = Canonical;
			FProjectileFlightSegment& FutureSegment =
				FutureLog.Segments.AddDefaulted_GetRef();
			FutureSegment.StartLocation = FVector(950.f, 0.f, 100.f);
			FutureSegment.EndLocation = Target.GetActorLocation();
			FutureSegment.SpeedAtStart = Canonical.InitialSpeed;
			FutureSegment.TimeAtImpact = 0.1f;
			FutureSegment.ImpactResponse = EImpactResponse::Embed;
			FutureSegment.HitActor = &Target;
			TestLauncher.Launcher->SubmitFlightLogForTests(FutureLog);
			ASSERT_THAT(AreEqual(Target.HitCount, 0));

			TestLauncher.Launcher->RegisterClientAuthoritativeLaunchForTests(
				21,
				Canonical,
				Now);
			FProjectileFlightLog DisconnectedLog;
			DisconnectedLog.LaunchID = 21;
			DisconnectedLog.ClientTimestamp = Now;
			DisconnectedLog.InitialConditions = Canonical;
			const FProjectileSnapshot FirstExpected =
				UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Canonical, 0.1f);
			const FProjectileSnapshot SecondExpected =
				UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Canonical, 0.2f);
			FProjectileFlightSegment& FirstSegment =
				DisconnectedLog.Segments.AddDefaulted_GetRef();
			FirstSegment.StartLocation = FirstExpected.Position -
				FirstExpected.Velocity.GetSafeNormal() * 25.f;
			FirstSegment.EndLocation = FirstExpected.Position;
			FirstSegment.SpeedAtStart = FirstExpected.Speed;
			FirstSegment.TimeAtImpact = 0.1f;
			FirstSegment.ImpactResponse = EImpactResponse::None;

			Target.SetActorLocation(SecondExpected.Position);
			FProjectileFlightSegment& SecondSegment =
				DisconnectedLog.Segments.AddDefaulted_GetRef();
			SecondSegment.StartLocation = SecondExpected.Position +
				SecondExpected.Velocity.GetSafeNormal() * 5000.f;
			SecondSegment.EndLocation = SecondExpected.Position;
			SecondSegment.SpeedAtStart = SecondExpected.Speed;
			SecondSegment.TimeAtImpact = 0.2f;
			SecondSegment.ImpactResponse = EImpactResponse::Embed;
			SecondSegment.ImpactNormal = -FVector::ForwardVector;
			SecondSegment.HitActor = &Target;
			TestLauncher.Launcher->SubmitFlightLogForTests(DisconnectedLog);
			ASSERT_THAT(AreEqual(Target.HitCount, 0));
		});
	}

	TEST_METHOD(Pooling_Release_ClearsExplosiveFuseState)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestProjectile* Projectile = Cast<ANPSTestProjectile>(
				Subsystem->AcquireProjectile(
					ANPSTestProjectile::StaticClass(),
					FVector::ZeroVector,
					FRotator::ZeroRotator));
			ASSERT_THAT(IsNotNull(Projectile));
			ASSERT_THAT(IsNotNull(Projectile->Detonation));

			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(Projectile);
			Explosive->DetonationTrigger = EDetonationTrigger::FuseTimer;
			Explosive->FuseTime = 30.f;
			Projectile->Detonation->ConfigureExplosive(Explosive);
			ASSERT_THAT(IsTrue(Projectile->Detonation->IsFuseArmed()));

			Subsystem->ReleaseProjectile(Projectile);
			ASSERT_THAT(IsNull(Projectile->Detonation->ExplosiveDataAsset));
			ASSERT_THAT(IsFalse(Projectile->Detonation->IsFuseArmed()));
			ASSERT_THAT(IsFalse(Projectile->Detonation->IsComponentTickEnabled()));
		});
	}

	TEST_METHOD(Projectile_ConditionsFalloffTracerAndBlacklist_AreConsistent)
	{
		UBallisticsProjectileDataAsset* Projectile = NewObject<UBallisticsProjectileDataAsset>();
		Projectile->InitialSpeed = 72000.f;
		Projectile->Mass = 12.f;
		Projectile->Diameter = 1.1f;
		Projectile->DragCoefficient = 0.25f;
		Projectile->WindAcceleration = FVector(0.f, 350.f, 0.f);
		Projectile->BaseDamage = 100.f;
		Projectile->DamageFalloffStartDistance = 1000.f;
		Projectile->DamageFalloffEndDistance = 3000.f;
		Projectile->DamageFalloffMultiplier = 0.4f;
		Projectile->bUseTracerInterval = true;
		Projectile->TracerInterval = 3;
		Projectile->EmbedBlacklist.Add(EPhysicalSurface::SurfaceType1);

		FProjectileInitialConditions Base;
		Base.Origin = FVector(10.f, 20.f, 30.f);
		const FProjectileInitialConditions Applied = Projectile->ApplyToConditions(Base);
		ASSERT_THAT(IsTrue(Applied.Origin.Equals(Base.Origin)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Applied.InitialSpeed, 72000.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Applied.Mass, 12.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Applied.Diameter, 1.1f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Applied.DragCoefficient, 0.25f)));
		ASSERT_THAT(IsTrue(Applied.WindAcceleration.Equals(FVector(0.f, 350.f, 0.f))));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Projectile->CalculateDamageAtDistance(500.f), 100.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Projectile->CalculateDamageAtDistance(2000.f), 70.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Projectile->CalculateDamageAtDistance(4000.f), 40.f)));
		ASSERT_THAT(IsTrue(Projectile->ShouldShowTracer(3)));
		ASSERT_THAT(IsFalse(Projectile->ShouldShowTracer(4)));
		ASSERT_THAT(IsTrue(Projectile->IsSurfaceBlacklisted(EPhysicalSurface::SurfaceType1)));
	}

	TEST_METHOD(Projectile_ConfigurationFields_AreResolved)
	{
		UBallisticsProjectileDataAsset* Projectile = NewObject<UBallisticsProjectileDataAsset>();
		Projectile->ProjectileClass = ANPSTestProjectile::StaticClass();
		Projectile->InitialSpeed = 32000.f;
		Projectile->Mass = 18.f;
		Projectile->bUseHitscan = true;
		Projectile->HitscanConfig.MaxRange = 7654.f;
		Projectile->bOverrideAuthorityMode = true;
		Projectile->AuthorityModeOverride = ELaunchAuthorityMode::ServerAuthoritative;
		Projectile->SlidingOverride.bOverride = true;
		Projectile->SlidingOverride.bEnableSliding = false;

		const FProjectileInitialConditions Conditions = Projectile->BuildInitialConditions();
		ASSERT_THAT(IsTrue(Projectile->ResolveProjectileClass() == ANPSTestProjectile::StaticClass()));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Conditions.InitialSpeed, 32000.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Conditions.Mass, 18.f)));
		ASSERT_THAT(IsTrue(Projectile->bUseHitscan));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Projectile->HitscanConfig.MaxRange, 7654.f)));
		ASSERT_THAT(IsTrue(Projectile->AuthorityModeOverride == ELaunchAuthorityMode::ServerAuthoritative));
		ASSERT_THAT(IsFalse(Projectile->SlidingOverride.bEnableSliding));
	}

	TEST_METHOD(Cosmetics_LaunchProfileAndModifier_ResolveWithoutDuplicatingProjectile)
	{
		UBallisticsLaunchCosmeticProfile* Profile =
			NewObject<UBallisticsLaunchCosmeticProfile>();
		FBallisticsLaunchEffect& ProfileEffect =
			Profile->Effects.AddDefaulted_GetRef();
		ProfileEffect.EffectId = TEXT("MuzzleFlash");
		ProfileEffect.SourceBinding = TEXT("Launch");
		ProfileEffect.SocketName = TEXT("Muzzle");
		ProfileEffect.Scale = 2.f;
		ProfileEffect.LifeSpan = 0.f;
		FBallisticsNiagaraStaticMeshParameter& CasingMesh =
			ProfileEffect.StaticMeshParameters.AddDefaulted_GetRef();
		CasingMesh.Name = TEXT("User.Casing");
		CasingMesh.Value = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
			TEXT("/Engine/BasicShapes/Cube.Cube")));
		FBallisticsNiagaraVector3Parameter& VelocityDirection =
			ProfileEffect.Vector3Parameters.AddDefaulted_GetRef();
		VelocityDirection.Name = TEXT("User.VelocityDirection");
		VelocityDirection.Value = FVector(0.f, 1.f, 0.25f);
		FBallisticsNiagaraFloatParameter& VelocitySpeed =
			ProfileEffect.FloatParameters.AddDefaulted_GetRef();
		VelocitySpeed.Name = TEXT("User.VelocitySpeed");
		VelocitySpeed.Value = 850.f;
		Profile->VolumeMultiplier = 0.8f;
		Profile->PitchMultiplier = 1.1f;
		Profile->AudibleRadius = 200000.f;
		Profile->PropagationSpeed = 34300.f;
		FBallisticsCosmeticBoolParameter& Suppressor =
			Profile->BoolParameters.AddDefaulted_GetRef();
		Suppressor.Name = TEXT("Suppressor");
		Suppressor.bValue = false;
		FBallisticsCosmeticFloatParameter& FireRate =
			Profile->FloatParameters.AddDefaulted_GetRef();
		FireRate.Name = TEXT("FireRate");
		FireRate.Value = 8.f;
		FireRate.bApplyToNiagara = false;

		UBallisticsLaunchCosmeticModifier* Modifier =
			NewObject<UBallisticsLaunchCosmeticModifier>();
		Modifier->EffectScaleMultiplier = 0.25f;
		Modifier->VolumeMultiplier = 0.5f;
		Modifier->PitchMultiplier = 0.9f;
		Modifier->AudibleRadiusMultiplier = 0.4f;
		FBallisticsCosmeticBoolParameter& SuppressorOverride =
			Modifier->BoolParameterOverrides.AddDefaulted_GetRef();
		SuppressorOverride.Name = TEXT("Suppressor");
		SuppressorOverride.bValue = true;
		SuppressorOverride.bApplyToNiagara = false;
		FBallisticsCosmeticBoolParameter& UseFlash =
			Modifier->BoolParameterOverrides.AddDefaulted_GetRef();
		UseFlash.Name = TEXT("UseFlash");
		UseFlash.bValue = false;
		UseFlash.bApplyToAudio = false;
		Modifier->FloatParameterOverrides.AddDefaulted_GetRef().Name =
			NAME_None;

		TArray<TObjectPtr<UBallisticsLaunchCosmeticModifier>> Modifiers;
		Modifiers.Add(Modifier);
		const FResolvedLaunchCosmetics Resolved =
			UBallisticsCosmeticLibrary::ResolveLaunchCosmetics(Profile, Modifiers);
		ASSERT_THAT(AreEqual(Resolved.Effects.Num(), 1));
		ASSERT_THAT(IsTrue(
			FMath::IsNearlyEqual(Resolved.Effects[0].Scale, 0.5f)));
		ASSERT_THAT(IsTrue(
			FMath::IsNearlyEqual(Resolved.Effects[0].LifeSpan, 0.05f)));
		ASSERT_THAT(IsTrue(Resolved.Effects[0].SourceBinding == TEXT("Launch")));
		ASSERT_THAT(IsTrue(Resolved.Effects[0].SocketName == TEXT("Muzzle")));
		ASSERT_THAT(AreEqual(Resolved.Effects[0].StaticMeshParameters.Num(), 1));
		ASSERT_THAT(AreEqual(Resolved.Effects[0].Vector3Parameters.Num(), 1));
		ASSERT_THAT(AreEqual(Resolved.Effects[0].FloatParameters.Num(), 1));
		ASSERT_THAT(IsTrue(
			Resolved.Effects[0].StaticMeshParameters[0].Name == TEXT("User.Casing")));
		ASSERT_THAT(IsTrue(
			Resolved.Effects[0].StaticMeshParameters[0].Value.ToSoftObjectPath() ==
			FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
		ASSERT_THAT(IsTrue(Resolved.Effects[0].Vector3Parameters[0].Value.Equals(
			FVector(0.f, 1.f, 0.25f))));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
			Resolved.Effects[0].FloatParameters[0].Value,
			850.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Resolved.VolumeMultiplier, 0.4f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Resolved.PitchMultiplier, 0.99f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Resolved.AudibleRadius, 80000.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Resolved.PropagationSpeed, 34300.f)));
		ASSERT_THAT(AreEqual(Resolved.BoolParameters.Num(), 2));
		ASSERT_THAT(AreEqual(Resolved.FloatParameters.Num(), 1));
		ASSERT_THAT(IsTrue(Resolved.BoolParameters[0].Name == TEXT("Suppressor")));
		ASSERT_THAT(IsTrue(Resolved.BoolParameters[0].bValue));
		ASSERT_THAT(IsFalse(Resolved.BoolParameters[0].bApplyToNiagara));
		ASSERT_THAT(IsTrue(Resolved.BoolParameters[1].Name == TEXT("UseFlash")));
		ASSERT_THAT(IsFalse(Resolved.BoolParameters[1].bApplyToAudio));
		ASSERT_THAT(IsTrue(Resolved.FloatParameters[0].Name == TEXT("FireRate")));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
			Resolved.FloatParameters[0].Value,
			8.f)));
	}

	TEST_METHOD(Cosmetics_ParameterPayloadsStopAtTheHardLimit)
	{
		UBallisticsLaunchCosmeticProfile* Profile =
			NewObject<UBallisticsLaunchCosmeticProfile>();
		for (int32 Index = 0; Index < 32; ++Index)
		{
			FBallisticsCosmeticBoolParameter& Parameter =
				Profile->BoolParameters.AddDefaulted_GetRef();
			Parameter.Name = FName(*FString::Printf(TEXT("Parameter%d"), Index));
		}
		FBallisticsLaunchEffect& Effect = Profile->Effects.AddDefaulted_GetRef();
		for (int32 Index = 0; Index < 10; ++Index)
		{
			FBallisticsNiagaraFloatParameter& Parameter =
				Effect.FloatParameters.AddDefaulted_GetRef();
			Parameter.Name = FName(*FString::Printf(TEXT("User.Float%d"), Index));
			FBallisticsNiagaraVector3Parameter& VectorParameter =
				Effect.Vector3Parameters.AddDefaulted_GetRef();
			VectorParameter.Name = FName(*FString::Printf(TEXT("User.Vector%d"), Index));
		}

		const FResolvedLaunchCosmetics Resolved =
			UBallisticsCosmeticLibrary::ResolveLaunchCosmetics(Profile, {});
		ASSERT_THAT(AreEqual(Resolved.BoolParameters.Num(), 16));
		ASSERT_THAT(AreEqual(Resolved.Effects.Num(), 1));
		ASSERT_THAT(AreEqual(Resolved.Effects[0].FloatParameters.Num(), 10));
		ASSERT_THAT(AreEqual(Resolved.Effects[0].Vector3Parameters.Num(), 6));
	}

	TEST_METHOD(Cosmetics_StackedNamedEffectOperations_AreDeterministic)
	{
		UBallisticsLaunchCosmeticProfile* Profile =
			NewObject<UBallisticsLaunchCosmeticProfile>();
		FBallisticsLaunchEffect& Flash = Profile->Effects.AddDefaulted_GetRef();
		Flash.EffectId = TEXT("Flash");
		Flash.Scale = 1.f;
		FBallisticsLaunchEffect& Smoke = Profile->Effects.AddDefaulted_GetRef();
		Smoke.EffectId = TEXT("Smoke");

		UBallisticsLaunchCosmeticModifier* Low =
			NewObject<UBallisticsLaunchCosmeticModifier>(GetTransientPackage(), TEXT("LowModifier"));
		Low->Priority = 10;
		FBallisticsLaunchEffectEdit& RemoveSmoke =
			Low->EffectOperations.AddDefaulted_GetRef();
		RemoveSmoke.Operation = EBallisticsLaunchEffectOperation::Remove;
		RemoveSmoke.EffectId = TEXT("Smoke");
		FBallisticsLaunchEffectEdit& AddCasing =
			Low->EffectOperations.AddDefaulted_GetRef();
		AddCasing.Operation = EBallisticsLaunchEffectOperation::Add;
		AddCasing.EffectId = TEXT("Casing");
		AddCasing.Effect.SocketName = TEXT("ShellEject");

		UBallisticsLaunchCosmeticModifier* High =
			NewObject<UBallisticsLaunchCosmeticModifier>(GetTransientPackage(), TEXT("HighModifier"));
		High->Priority = 20;
		FBallisticsLaunchEffectEdit& ReplaceFlash =
			High->EffectOperations.AddDefaulted_GetRef();
		ReplaceFlash.Operation = EBallisticsLaunchEffectOperation::Replace;
		ReplaceFlash.EffectId = TEXT("Flash");
		ReplaceFlash.Effect.Scale = 3.f;

		TArray<TObjectPtr<UBallisticsLaunchCosmeticModifier>> Reversed;
		Reversed.Add(High);
		Reversed.Add(Low);
		const FResolvedLaunchCosmetics Resolved =
			UBallisticsCosmeticLibrary::ResolveLaunchCosmetics(Profile, Reversed);
		ASSERT_THAT(AreEqual(Resolved.Effects.Num(), 2));
		const FBallisticsLaunchEffect* ResolvedFlash = Resolved.Effects.FindByPredicate(
			[](const FBallisticsLaunchEffect& Effect) { return Effect.EffectId == TEXT("Flash"); });
		const FBallisticsLaunchEffect* ResolvedCasing = Resolved.Effects.FindByPredicate(
			[](const FBallisticsLaunchEffect& Effect) { return Effect.EffectId == TEXT("Casing"); });
		ASSERT_THAT(IsNotNull(ResolvedFlash));
		ASSERT_THAT(IsNotNull(ResolvedCasing));
		ASSERT_THAT(IsTrue(ResolvedFlash && FMath::IsNearlyEqual(ResolvedFlash->Scale, 3.f)));
		ASSERT_THAT(IsTrue(ResolvedCasing && ResolvedCasing->SocketName == TEXT("ShellEject")));
		ASSERT_THAT(IsNull(Resolved.Effects.FindByPredicate(
			[](const FBallisticsLaunchEffect& Effect) { return Effect.EffectId == TEXT("Smoke"); })));
	}

	TEST_METHOD(Cosmetics_RemoteLaunchConcurrency_IsPerSourceAndPreservesGlobalLimits)
	{
		UProjectileCosmeticsManager* Manager = NewObject<UProjectileCosmeticsManager>();
		USoundConcurrency* PerWeapon = NewObject<USoundConcurrency>();
		PerWeapon->Concurrency.bLimitToOwner = true;
		PerWeapon->Concurrency.MaxCount = 3;
		PerWeapon->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
		USoundConcurrency* Global = NewObject<USoundConcurrency>();
		Global->Concurrency.bLimitToOwner = false;
		USoundWave* Shot = NewObject<USoundWave>();
		USoundWave* AlternateShot = NewObject<USoundWave>();
		Shot->ConcurrencySet = { PerWeapon, Global };
		AlternateShot->ConcurrencySet = { PerWeapon, Global };
		const FGuid FirstWeapon = FGuid::NewGuid();
		const FGuid SecondWeapon = FGuid::NewGuid();

		const TSet<USoundConcurrency*> First =
			Manager->ResolveAudioConcurrencyForTesting(Shot, FirstWeapon);
		const TSet<USoundConcurrency*> Repeated =
			Manager->ResolveAudioConcurrencyForTesting(AlternateShot, FirstWeapon);
		const TSet<USoundConcurrency*> Other =
			Manager->ResolveAudioConcurrencyForTesting(Shot, SecondWeapon);
		ASSERT_THAT(AreEqual(First.Num(), 2));
		ASSERT_THAT(AreEqual(Other.Num(), 2));
		ASSERT_THAT(IsTrue(First.Contains(Global) && Other.Contains(Global)));
		ASSERT_THAT(IsFalse(First.Contains(PerWeapon)));
		for (USoundConcurrency* Group : First)
		{
			ASSERT_THAT(IsTrue(Repeated.Contains(Group)));
			if (Group != Global)
			{
				ASSERT_THAT(IsFalse(Other.Contains(Group)));
				ASSERT_THAT(IsFalse(Group->Concurrency.bLimitToOwner));
				ASSERT_THAT(AreEqual(Group->Concurrency.GetMaxCount(), 3));
				ASSERT_THAT(IsTrue(Group->Concurrency.ResolutionRule == EMaxConcurrentResolutionRule::StopOldest));
			}
		}
		// Authored assets remain shared and unchanged, and events without a source use engine defaults.
		ASSERT_THAT(IsTrue(PerWeapon->Concurrency.bLimitToOwner));
		ASSERT_THAT(IsTrue(Manager->ResolveAudioConcurrencyForTesting(Shot, FGuid()).IsEmpty()));

		Shot->bOverrideConcurrency = true;
		Shot->ConcurrencyOverrides.bLimitToOwner = true;
		Shot->ConcurrencyOverrides.MaxCount = 4;
		const TSet<USoundConcurrency*> Override =
			Manager->ResolveAudioConcurrencyForTesting(Shot, FirstWeapon);
		ASSERT_THAT(AreEqual(Override.Num(), 1));
		ASSERT_THAT(AreEqual((*Override.CreateConstIterator())->Concurrency.GetMaxCount(), 4));
		Shot->ConcurrencyOverrides.bLimitToOwner = false;
		ASSERT_THAT(IsTrue(Manager->ResolveAudioConcurrencyForTesting(Shot, FirstWeapon).IsEmpty()));
	}

	TEST_METHOD(Cosmetics_NamedParameters_ApplyToAudioAndNiagaraTargets)
	{
		UAudioComponent* Audio = NewObject<UAudioComponent>();
		UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>();
		ASSERT_THAT(IsNotNull(Audio));
		ASSERT_THAT(IsNotNull(Niagara));

		TArray<FBallisticsCosmeticBoolParameter> BoolParameters;
		FBallisticsCosmeticBoolParameter& Suppressor =
			BoolParameters.AddDefaulted_GetRef();
		Suppressor.Name = TEXT("Suppressor");
		Suppressor.bValue = true;
		Suppressor.bApplyToNiagara = false;
		FBallisticsCosmeticBoolParameter& UseFlash =
			BoolParameters.AddDefaulted_GetRef();
		UseFlash.Name = TEXT("UseFlash");
		UseFlash.bValue = false;
		UseFlash.bApplyToAudio = false;

		TArray<FBallisticsCosmeticFloatParameter> FloatParameters;
		FBallisticsCosmeticFloatParameter& FireRate =
			FloatParameters.AddDefaulted_GetRef();
		FireRate.Name = TEXT("FireRate");
		FireRate.Value = 11.5f;

		UBallisticsCosmeticLibrary::ApplyAudioParameters(
			Audio,
			BoolParameters,
			FloatParameters);
		UBallisticsCosmeticLibrary::ApplyNiagaraParameters(
			Niagara,
			BoolParameters,
			FloatParameters);

		const FAudioParameter* AudioSuppressor = FAudioParameter::FindParam(
			Audio->GetInstanceParameters(),
			TEXT("Suppressor"));
		const FAudioParameter* AudioUseFlash = FAudioParameter::FindParam(
			Audio->GetInstanceParameters(),
			TEXT("UseFlash"));
		const FAudioParameter* AudioFireRate = FAudioParameter::FindParam(
			Audio->GetInstanceParameters(),
			TEXT("FireRate"));
		ASSERT_THAT(IsNotNull(AudioSuppressor));
		ASSERT_THAT(IsNull(AudioUseFlash));
		ASSERT_THAT(IsNotNull(AudioFireRate));
		ASSERT_THAT(IsTrue(AudioSuppressor && AudioSuppressor->BoolParam));
		ASSERT_THAT(IsTrue(AudioFireRate &&
			FMath::IsNearlyEqual(AudioFireRate->FloatParam, 11.5f)));

		bool bUseFlashIsValid = false;
		const bool bUseFlash = Niagara->GetVariableBool(
			TEXT("UseFlash"),
			bUseFlashIsValid);
		bool bSuppressorIsValid = false;
		const bool bNiagaraSuppressor = Niagara->GetVariableBool(
			TEXT("Suppressor"),
			bSuppressorIsValid);
		bool bFireRateIsValid = false;
		const float NiagaraFireRate = Niagara->GetVariableFloat(
			TEXT("FireRate"),
			bFireRateIsValid);
		ASSERT_THAT(IsTrue(bUseFlashIsValid));
		ASSERT_THAT(IsFalse(bUseFlash));
		ASSERT_THAT(IsFalse(bSuppressorIsValid));
		ASSERT_THAT(IsFalse(bNiagaraSuppressor));
		ASSERT_THAT(IsTrue(bFireRateIsValid));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(NiagaraFireRate, 11.5f)));
	}

	TEST_METHOD(Cosmetics_LaunchEffectParameters_ApplySupportedNiagaraTypes)
	{
		UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>();
		ASSERT_THAT(IsNotNull(Niagara));

		FBallisticsLaunchEffect Effect;
		FBallisticsNiagaraBoolParameter& BoolParameter =
			Effect.BoolParameters.AddDefaulted_GetRef();
		BoolParameter.Name = TEXT("User.Enabled");
		BoolParameter.bValue = true;
		FBallisticsNiagaraIntParameter& IntParameter =
			Effect.IntParameters.AddDefaulted_GetRef();
		IntParameter.Name = TEXT("User.Count");
		IntParameter.Value = 4;
		FBallisticsNiagaraFloatParameter& FloatParameter =
			Effect.FloatParameters.AddDefaulted_GetRef();
		FloatParameter.Name = TEXT("User.Speed");
		FloatParameter.Value = 1250.f;
		FBallisticsNiagaraVector2Parameter& Vector2Parameter =
			Effect.Vector2Parameters.AddDefaulted_GetRef();
		Vector2Parameter.Name = TEXT("User.Range");
		Vector2Parameter.Value = FVector2D(2.f, 8.f);
		FBallisticsNiagaraVector3Parameter& Vector3Parameter =
			Effect.Vector3Parameters.AddDefaulted_GetRef();
		Vector3Parameter.Name = TEXT("User.Direction");
		Vector3Parameter.Value = FVector(0.f, 1.f, 0.5f);
		FBallisticsNiagaraColorParameter& ColorParameter =
			Effect.ColorParameters.AddDefaulted_GetRef();
		ColorParameter.Name = TEXT("User.Tint");
		ColorParameter.Value = FLinearColor(0.2f, 0.4f, 0.6f, 0.8f);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterial* Material = LoadObject<UMaterial>(
			nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		UTexture2D* Texture = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		ASSERT_THAT(IsNotNull(Mesh));
		ASSERT_THAT(IsNotNull(Material));
		ASSERT_THAT(IsNotNull(Texture));
		FBallisticsNiagaraStaticMeshParameter& MeshParameter =
			Effect.StaticMeshParameters.AddDefaulted_GetRef();
		MeshParameter.Name = TEXT("User.Mesh");
		MeshParameter.Value = Mesh;
		FBallisticsNiagaraMaterialParameter& MaterialParameter =
			Effect.MaterialParameters.AddDefaulted_GetRef();
		MaterialParameter.Name = TEXT("User.Material");
		MaterialParameter.Value = Material;
		FBallisticsNiagaraTextureParameter& TextureParameter =
			Effect.TextureParameters.AddDefaulted_GetRef();
		TextureParameter.Name = TEXT("User.Texture");
		TextureParameter.Value = Texture;
		Niagara->GetOverrideParameters().AddParameter(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUStaticMeshDef(),
			TEXT("User.Mesh")));
		Niagara->GetOverrideParameters().AddParameter(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUMaterialDef(),
			TEXT("User.Material")));
		Niagara->GetOverrideParameters().AddParameter(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUTextureDef(),
			TEXT("User.Texture")));

		UBallisticsCosmeticLibrary::ApplyLaunchEffectNiagaraParameters(
			Niagara,
			Effect);

		bool bValid = false;
		ASSERT_THAT(IsTrue(Niagara->GetVariableBool(TEXT("User.Enabled"), bValid)));
		ASSERT_THAT(IsTrue(bValid));
		ASSERT_THAT(AreEqual(Niagara->GetVariableInt(TEXT("User.Count"), bValid), 4));
		ASSERT_THAT(IsTrue(bValid));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
			Niagara->GetVariableFloat(TEXT("User.Speed"), bValid),
			1250.f)));
		ASSERT_THAT(IsTrue(bValid));
		ASSERT_THAT(IsTrue(Niagara->GetVariableVec2(TEXT("User.Range"), bValid).Equals(
			FVector2D(2.f, 8.f))));
		ASSERT_THAT(IsTrue(bValid));
		ASSERT_THAT(IsTrue(Niagara->GetVariableVec3(TEXT("User.Direction"), bValid).Equals(
			FVector(0.f, 1.f, 0.5f))));
		ASSERT_THAT(IsTrue(bValid));
		ASSERT_THAT(IsTrue(Niagara->GetVariableColor(TEXT("User.Tint"), bValid).Equals(
			FLinearColor(0.2f, 0.4f, 0.6f, 0.8f))));
		ASSERT_THAT(IsTrue(bValid));
		FNiagaraVariant MeshOverride;
		Niagara->GetVariable_InternalUseOnly(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUStaticMeshDef(),
			TEXT("User.Mesh")), MeshOverride);
		FNiagaraVariant MaterialOverride;
		Niagara->GetVariable_InternalUseOnly(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUMaterialDef(),
			TEXT("User.Material")), MaterialOverride);
		FNiagaraVariant TextureOverride;
		Niagara->GetVariable_InternalUseOnly(FNiagaraVariable(
			FNiagaraTypeDefinition::GetUTextureDef(),
			TEXT("User.Texture")), TextureOverride);
		ASSERT_THAT(IsTrue(MeshOverride.GetUObject() == Mesh));
		ASSERT_THAT(IsTrue(MaterialOverride.GetUObject() == Material));
		ASSERT_THAT(IsTrue(TextureOverride.GetUObject() == Texture));
	}

	TEST_METHOD(Cosmetics_ProjectileImpactMap_UsesSurfaceOverrideAndFallback)
	{
		UBallisticsCoreSettings* Settings = GetMutableDefault<UBallisticsCoreSettings>();
		const TMap<TEnumAsByte<EPhysicalSurface>, FBallisticsImpactCosmetic>
			PreviousProjectSurfaces = Settings->DefaultImpactCosmeticsBySurface;
		const FBallisticsImpactCosmetic PreviousProjectDefault = Settings->DefaultImpactCosmetic;
		ON_SCOPE_EXIT
		{
			Settings->DefaultImpactCosmeticsBySurface = PreviousProjectSurfaces;
			Settings->DefaultImpactCosmetic = PreviousProjectDefault;
		};

		Settings->DefaultImpactCosmeticsBySurface.Reset();
		Settings->DefaultImpactCosmetic = FBallisticsImpactCosmetic();
		Settings->DefaultImpactCosmetic.EffectScale = 5.f;
		Settings->DefaultImpactCosmetic.VolumeMultiplier = 0.5f;
		FBallisticsImpactCosmetic ProjectSurface;
		ProjectSurface.EffectScale = 4.f;
		ProjectSurface.VolumeMultiplier = 0.4f;
		Settings->DefaultImpactCosmeticsBySurface.Add(
			EPhysicalSurface::SurfaceType1, ProjectSurface);

		UBallisticsProjectileDataAsset* Projectile = NewObject<UBallisticsProjectileDataAsset>();
		const FBallisticsImpactCosmetic& ProjectSurfaceResult =
			Projectile->GetImpactCosmetic(EPhysicalSurface::SurfaceType1);
		const FBallisticsImpactCosmetic& ProjectResult =
			Projectile->GetImpactCosmetic(EPhysicalSurface::SurfaceType2);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(ProjectSurfaceResult.EffectScale, 4.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(ProjectSurfaceResult.VolumeMultiplier, 0.4f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(ProjectResult.EffectScale, 5.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(ProjectResult.VolumeMultiplier, 0.5f)));

		Projectile->DefaultImpactCosmetic.EffectScale = 3.f;
		Projectile->DefaultImpactCosmetic.VolumeMultiplier = 0.75f;

		FBallisticsImpactCosmetic Metal;
		Metal.EffectScale = 0.5f;
		Metal.VolumeMultiplier = 0.25f;
		Projectile->ImpactCosmetics.Add(EPhysicalSurface::SurfaceType1, Metal);

		const FBallisticsImpactCosmetic& SurfaceResult =
			Projectile->GetImpactCosmetic(EPhysicalSurface::SurfaceType1);
		const FBallisticsImpactCosmetic& FallbackResult =
			Projectile->GetImpactCosmetic(EPhysicalSurface::SurfaceType2);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(SurfaceResult.EffectScale, 0.5f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(SurfaceResult.VolumeMultiplier, 0.25f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(FallbackResult.EffectScale, 3.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(FallbackResult.VolumeMultiplier, 0.75f)));

		Projectile->DefaultImpactCosmetic = FBallisticsImpactCosmetic();
		Projectile->bOverrideProjectDefaultImpactCosmetic = true;
		const FBallisticsImpactCosmetic& EmptyOverride =
			Projectile->GetImpactCosmetic(EPhysicalSurface::SurfaceType2);
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(EmptyOverride.EffectScale, 1.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(EmptyOverride.VolumeMultiplier, 1.f)));
	}

	TEST_METHOD(Cosmetics_ExampleLaunchProfile_UsesSocketEffectArray)
	{
		UBallisticsLaunchCosmeticProfile* Profile =
			LoadObject<UBallisticsLaunchCosmeticProfile>(
				nullptr,
				TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile."
					"DA_NPS_LaunchProfile"));
		ASSERT_THAT(IsNotNull(Profile));
		ASSERT_THAT(AreEqual(Profile ? Profile->Effects.Num() : 0, 3));
		if (!Profile)
		{
			return;
		}

		int32 MuzzleEffectCount = 0;
		int32 EjectionEffectCount = 0;
		TSet<FName> EffectIds;
		for (const FBallisticsLaunchEffect& Effect : Profile->Effects)
		{
			ASSERT_THAT(IsFalse(Effect.Effect.IsNull()));
			ASSERT_THAT(IsFalse(Effect.EffectId.IsNone()));
			ASSERT_THAT(IsTrue(Effect.LifeSpan >= 0.05f));
			EffectIds.Add(Effect.EffectId);
			MuzzleEffectCount += Effect.SocketName == TEXT("Muzzle") ? 1 : 0;
			EjectionEffectCount +=
				Effect.SocketName == TEXT("ShellEject") ? 1 : 0;
		}
		ASSERT_THAT(AreEqual(MuzzleEffectCount, 2));
		ASSERT_THAT(AreEqual(EjectionEffectCount, 1));
		ASSERT_THAT(AreEqual(EffectIds.Num(), 3));
	}

	TEST_METHOD(Cosmetics_LaunchEffectSources_ResolveDefaultAndNamedComponents)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& Source = Spawner->SpawnActor<AActor>();
			USceneComponent* DefaultSource =
				NewObject<USceneComponent>(&Source, TEXT("DefaultEffectSource"));
			USceneComponent* EjectionSource =
				NewObject<USceneComponent>(&Source, TEXT("EjectionEffectSource"));
			UProjectileLauncherComponent* Launcher =
				NewObject<UProjectileLauncherComponent>(&Source);
			Source.AddInstanceComponent(DefaultSource);
			Source.AddInstanceComponent(EjectionSource);
			Source.AddInstanceComponent(Launcher);
			DefaultSource->RegisterComponent();
			EjectionSource->RegisterComponent();
			Launcher->RegisterComponent();

			Launcher->SetLaunchEffectSourceComponent(DefaultSource);
			ASSERT_THAT(IsTrue(Launcher->SetLaunchEffectSourceBinding(
				TEXT("Ejection"),
				EjectionSource)));
			ASSERT_THAT(IsTrue(
				Launcher->GetLaunchEffectSourceComponent(NAME_None) ==
					DefaultSource));
			ASSERT_THAT(IsTrue(
				Launcher->GetLaunchEffectSourceComponent(TEXT("Ejection")) ==
					EjectionSource));
			ASSERT_THAT(IsNull(
				Launcher->GetLaunchEffectSourceComponent(TEXT("Missing"))));
			ASSERT_THAT(IsFalse(
				Launcher->SetLaunchEffectSourceBinding(NAME_None, EjectionSource)));
			ASSERT_THAT(IsTrue(
				Launcher->SetLaunchEffectSourceBinding(TEXT("Ejection"), nullptr)));
			ASSERT_THAT(IsNull(
				Launcher->GetLaunchEffectSourceComponent(TEXT("Ejection"))));
		});
	}

	TEST_METHOD(Cosmetics_AttachedContributor_AutoBindsAndOverridesSource)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& LauncherActor = Spawner->SpawnActor<AActor>();
			USceneComponent* LauncherRoot =
				NewObject<USceneComponent>(&LauncherActor, TEXT("LauncherRoot"));
			UProjectileLauncherComponent* Launcher =
				NewObject<UProjectileLauncherComponent>(&LauncherActor, TEXT("Launcher"));
			LauncherActor.SetRootComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(Launcher);
			LauncherRoot->RegisterComponent();
			Launcher->RegisterComponent();

			AActor& Attachment = Spawner->SpawnActor<AActor>();
			USceneComponent* AttachmentRoot =
				NewObject<USceneComponent>(&Attachment, TEXT("AttachmentSource"));
			AttachmentRoot->ComponentTags.Add(TEXT("LaunchSource"));
			Attachment.SetRootComponent(AttachmentRoot);
			Attachment.AddInstanceComponent(AttachmentRoot);
			AttachmentRoot->RegisterComponent();

			UBallisticsLaunchCosmeticModifier* Modifier =
				NewObject<UBallisticsLaunchCosmeticModifier>(&Attachment);
			FBallisticsLaunchSourceOverride& Override =
				Modifier->SourceOverrides.AddDefaulted_GetRef();
			Override.BindingName = TEXT("Launch");
			Override.ComponentTag = TEXT("LaunchSource");

			UBallisticsLaunchContributorComponent* Contributor =
				NewObject<UBallisticsLaunchContributorComponent>(&Attachment, TEXT("Contributor"));
			Attachment.AddInstanceComponent(Contributor);
			Contributor->Modifier = Modifier;
			Contributor->RegisterComponent();
			Attachment.AttachToActor(&LauncherActor, FAttachmentTransformRules::KeepWorldTransform);

			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			Subsystem->Tick(0.016f);
			ASSERT_THAT(IsTrue(Contributor->GetBoundLauncher() == Launcher));
			const int32 SearchCount = Contributor->GetBindingSearchCountForTests();
			for (int32 TickIndex = 0; TickIndex < 10; ++TickIndex)
			{
				Subsystem->Tick(0.016f);
			}
			ASSERT_THAT(AreEqual(
				Contributor->GetBindingSearchCountForTests(),
				SearchCount));

			FTransform SourceTransform;
			ASSERT_THAT(IsTrue(Launcher->ResolveLaunchSourceTransform(
				TEXT("Launch"), NAME_None, SourceTransform)));
			ASSERT_THAT(IsTrue(SourceTransform.GetLocation().Equals(
				AttachmentRoot->GetComponentLocation())));

			Attachment.DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Subsystem->Tick(0.016f);
			ASSERT_THAT(IsNull(Contributor->GetBoundLauncher()));
		});
	}

	TEST_METHOD(Cosmetics_InlineSourceOverridesUseDepthAndOptionalPriority)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& LauncherActor = Spawner->SpawnActor<AActor>();
			USceneComponent* LauncherRoot =
				NewObject<USceneComponent>(&LauncherActor, TEXT("LauncherRoot"));
			UProjectileLauncherComponent* Launcher =
				NewObject<UProjectileLauncherComponent>(&LauncherActor, TEXT("Launcher"));
			LauncherActor.SetRootComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(Launcher);
			LauncherRoot->RegisterComponent();
			Launcher->RegisterComponent();

			AActor& Barrel = Spawner->SpawnActor<AActor>();
			USceneComponent* BarrelRoot =
				NewObject<USceneComponent>(&Barrel, TEXT("BarrelSource"));
			BarrelRoot->ComponentTags.Add(TEXT("LaunchSource"));
			Barrel.SetRootComponent(BarrelRoot);
			Barrel.AddInstanceComponent(BarrelRoot);
			BarrelRoot->RegisterComponent();
			Barrel.SetActorLocation(FVector(100.f, 0.f, 0.f));
			Barrel.AttachToActor(&LauncherActor, FAttachmentTransformRules::KeepWorldTransform);
			UBallisticsLaunchContributorComponent* BarrelContributor =
				NewObject<UBallisticsLaunchContributorComponent>(&Barrel, TEXT("Contributor"));
			Barrel.AddInstanceComponent(BarrelContributor);
			FBallisticsLaunchSourceOverride& BarrelOverride =
				BarrelContributor->SourceOverrides.AddDefaulted_GetRef();
			BarrelOverride.BindingName = TEXT("Muzzle");
			BarrelOverride.ComponentTag = TEXT("LaunchSource");
			BarrelContributor->RegisterComponent();

			AActor& Device = Spawner->SpawnActor<AActor>();
			USceneComponent* DeviceRoot =
				NewObject<USceneComponent>(&Device, TEXT("DeviceSource"));
			DeviceRoot->ComponentTags.Add(TEXT("LaunchSource"));
			Device.SetRootComponent(DeviceRoot);
			Device.AddInstanceComponent(DeviceRoot);
			DeviceRoot->RegisterComponent();
			Device.SetActorLocation(FVector(200.f, 0.f, 0.f));
			Device.AttachToActor(&Barrel, FAttachmentTransformRules::KeepWorldTransform);
			UBallisticsLaunchContributorComponent* DeviceContributor =
				NewObject<UBallisticsLaunchContributorComponent>(&Device, TEXT("Contributor"));
			Device.AddInstanceComponent(DeviceContributor);
			FBallisticsLaunchSourceOverride& DeviceOverride =
				DeviceContributor->SourceOverrides.AddDefaulted_GetRef();
			DeviceOverride.BindingName = TEXT("Muzzle");
			DeviceOverride.ComponentTag = TEXT("LaunchSource");
			DeviceContributor->RegisterComponent();

			FTransform SourceTransform;
			ASSERT_THAT(IsTrue(Launcher->ResolveLaunchSourceTransform(
				TEXT("Muzzle"), NAME_None, SourceTransform)));
			ASSERT_THAT(IsTrue(SourceTransform.GetLocation().Equals(
				DeviceRoot->GetComponentLocation())));

			BarrelOverride.bOverridePriority = true;
			BarrelOverride.Priority = 10;
			ASSERT_THAT(IsTrue(Launcher->ResolveLaunchSourceTransform(
				TEXT("Muzzle"), NAME_None, SourceTransform)));
			ASSERT_THAT(IsTrue(SourceTransform.GetLocation().Equals(
				BarrelRoot->GetComponentLocation())));
			ASSERT_THAT(IsNull(BarrelContributor->GetModifier()));
			ASSERT_THAT(IsNull(DeviceContributor->GetModifier()));
		});
	}

	TEST_METHOD(Cosmetics_ModifierStackUsesDepthPriorityAndStableTies)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& LauncherActor = Spawner->SpawnActor<AActor>();
			USceneComponent* LauncherRoot =
				NewObject<USceneComponent>(&LauncherActor, TEXT("LauncherRoot"));
			UProjectileLauncherComponent* Launcher =
				NewObject<UProjectileLauncherComponent>(&LauncherActor, TEXT("Launcher"));
			LauncherActor.SetRootComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(LauncherRoot);
			LauncherActor.AddInstanceComponent(Launcher);
			LauncherRoot->RegisterComponent();
			Launcher->RegisterComponent();

			UBallisticsLaunchCosmeticProfile* Profile =
				NewObject<UBallisticsLaunchCosmeticProfile>(&LauncherActor);
			FBallisticsLaunchEffect& BaseFlash = Profile->Effects.AddDefaulted_GetRef();
			BaseFlash.EffectId = TEXT("MuzzleFlash");
			BaseFlash.Scale = 1.f;
			FBallisticsCosmeticBoolParameter& BaseParameter =
				Profile->BoolParameters.AddDefaulted_GetRef();
			BaseParameter.Name = TEXT("Suppressed");
			BaseParameter.bValue = false;
			ASSERT_THAT(IsTrue(Launcher->SetLaunchCosmeticProfile(Profile)));

			AActor& Device = Spawner->SpawnActor<AActor>();
			USceneComponent* DeviceRoot =
				NewObject<USceneComponent>(&Device, TEXT("DeviceRoot"));
			Device.SetRootComponent(DeviceRoot);
			Device.AddInstanceComponent(DeviceRoot);
			DeviceRoot->RegisterComponent();
			Device.AttachToActor(&LauncherActor, FAttachmentTransformRules::KeepWorldTransform);

			UBallisticsLaunchCosmeticModifier* DeviceModifier =
				NewObject<UBallisticsLaunchCosmeticModifier>(&Device, TEXT("DeviceModifier"));
			FBallisticsLaunchEffectEdit& DeviceFlash =
				DeviceModifier->EffectOperations.AddDefaulted_GetRef();
			DeviceFlash.Operation = EBallisticsLaunchEffectOperation::Replace;
			DeviceFlash.EffectId = TEXT("MuzzleFlash");
			DeviceFlash.Effect.Scale = 2.f;
			FBallisticsCosmeticBoolParameter& DeviceParameter =
				DeviceModifier->BoolParameterOverrides.AddDefaulted_GetRef();
			DeviceParameter.Name = TEXT("Suppressed");
			DeviceParameter.bValue = false;
			UBallisticsLaunchContributorComponent* DeviceContributor =
				NewObject<UBallisticsLaunchContributorComponent>(&Device, TEXT("DeviceContributor"));
			DeviceContributor->Modifier = DeviceModifier;
			Device.AddInstanceComponent(DeviceContributor);
			DeviceContributor->RegisterComponent();

			AActor& Suppressor = Spawner->SpawnActor<AActor>();
			USceneComponent* SuppressorRoot =
				NewObject<USceneComponent>(&Suppressor, TEXT("SuppressorRoot"));
			Suppressor.SetRootComponent(SuppressorRoot);
			Suppressor.AddInstanceComponent(SuppressorRoot);
			SuppressorRoot->RegisterComponent();
			Suppressor.AttachToActor(&Device, FAttachmentTransformRules::KeepWorldTransform);

			UBallisticsLaunchCosmeticModifier* SuppressorModifier =
				NewObject<UBallisticsLaunchCosmeticModifier>(&Suppressor, TEXT("SuppressorModifier"));
			FBallisticsLaunchEffectEdit& SuppressorFlash =
				SuppressorModifier->EffectOperations.AddDefaulted_GetRef();
			SuppressorFlash.Operation = EBallisticsLaunchEffectOperation::Replace;
			SuppressorFlash.EffectId = TEXT("MuzzleFlash");
			SuppressorFlash.Effect.Scale = 3.f;
			FBallisticsCosmeticBoolParameter& SuppressorParameter =
				SuppressorModifier->BoolParameterOverrides.AddDefaulted_GetRef();
			SuppressorParameter.Name = TEXT("Suppressed");
			SuppressorParameter.bValue = true;
			UBallisticsLaunchContributorComponent* SuppressorContributor =
				NewObject<UBallisticsLaunchContributorComponent>(
					&Suppressor,
					TEXT("SuppressorContributor"));
			SuppressorContributor->Modifier = SuppressorModifier;
			Suppressor.AddInstanceComponent(SuppressorContributor);
			SuppressorContributor->RegisterComponent();

			auto GetFlashScale = [Launcher]()
			{
				const FBallisticsLaunchEffect* Flash =
					Launcher->GetResolvedLaunchCosmetics().Effects.FindByPredicate(
						[](const FBallisticsLaunchEffect& Effect)
						{
							return Effect.EffectId == TEXT("MuzzleFlash");
						});
				return Flash ? Flash->Scale : -1.f;
			};
			auto GetSuppressed = [Launcher]()
			{
				const FBallisticsCosmeticBoolParameter* Parameter =
					Launcher->GetResolvedLaunchCosmetics().BoolParameters.FindByPredicate(
						[](const FBallisticsCosmeticBoolParameter& Candidate)
						{
							return Candidate.Name == TEXT("Suppressed");
						});
				return Parameter && Parameter->bValue;
			};

			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(GetFlashScale(), 3.f)));
			ASSERT_THAT(IsTrue(GetSuppressed()));
			ASSERT_THAT(IsTrue(
				Launcher->GetActiveLaunchModifiers().Last() == SuppressorModifier));

			DeviceModifier->bOverridePriority = true;
			DeviceModifier->Priority = 10;
			Launcher->NotifyLaunchContributorChanged(DeviceContributor);
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(GetFlashScale(), 2.f)));
			ASSERT_THAT(IsFalse(GetSuppressed()));
			ASSERT_THAT(IsTrue(
				Launcher->GetActiveLaunchModifiers().Last() == DeviceModifier));

			AActor& TieActor = Spawner->SpawnActor<AActor>();
			USceneComponent* TieRoot =
				NewObject<USceneComponent>(&TieActor, TEXT("TieRoot"));
			TieActor.SetRootComponent(TieRoot);
			TieActor.AddInstanceComponent(TieRoot);
			TieRoot->RegisterComponent();
			TieActor.AttachToActor(&LauncherActor, FAttachmentTransformRules::KeepWorldTransform);

			UBallisticsLaunchCosmeticModifier* AModifier =
				NewObject<UBallisticsLaunchCosmeticModifier>(&TieActor, TEXT("AModifier"));
			AModifier->bOverridePriority = true;
			AModifier->Priority = 20;
			FBallisticsLaunchEffectEdit& AFlash =
				AModifier->EffectOperations.AddDefaulted_GetRef();
			AFlash.Operation = EBallisticsLaunchEffectOperation::Replace;
			AFlash.EffectId = TEXT("MuzzleFlash");
			AFlash.Effect.Scale = 4.f;
			UBallisticsLaunchContributorComponent* AContributor =
				NewObject<UBallisticsLaunchContributorComponent>(
					&TieActor,
					TEXT("AContributor"));
			AContributor->Modifier = AModifier;
			TieActor.AddInstanceComponent(AContributor);
			AContributor->RegisterComponent();

			UBallisticsLaunchCosmeticModifier* ZModifier =
				NewObject<UBallisticsLaunchCosmeticModifier>(&TieActor, TEXT("ZModifier"));
			ZModifier->bOverridePriority = true;
			ZModifier->Priority = 20;
			FBallisticsLaunchEffectEdit& ZFlash =
				ZModifier->EffectOperations.AddDefaulted_GetRef();
			ZFlash.Operation = EBallisticsLaunchEffectOperation::Replace;
			ZFlash.EffectId = TEXT("MuzzleFlash");
			ZFlash.Effect.Scale = 5.f;
			UBallisticsLaunchContributorComponent* ZContributor =
				NewObject<UBallisticsLaunchContributorComponent>(
					&TieActor,
					TEXT("ZContributor"));
			ZContributor->Modifier = ZModifier;
			TieActor.AddInstanceComponent(ZContributor);
			ZContributor->RegisterComponent();

			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(GetFlashScale(), 5.f)));
			ASSERT_THAT(IsTrue(
				Launcher->GetActiveLaunchModifiers().Last() == ZModifier));
		});
	}

	TEST_METHOD(Cosmetics_DetonationRoutesWithoutSourceLauncher)
	{
		TestCommandBuilder.Do([this]()
		{
			UWorld& World = Spawner->GetWorld();
			ASSERT_THAT(IsFalse(World.GetNetMode() == NM_Client));
			UProjectileBallisticsSubsystem* Subsystem =
				World.GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager =
				Subsystem ? Subsystem->GetCosmeticsManager() : nullptr;
			ASSERT_THAT(IsNotNull(Manager));

			AActor& Source = Spawner->SpawnActor<AActor>();
			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->BlastDamage = 0.f;
			Explosive->ShrapnelCount = 0;
			Explosive->DetonationCosmetic.Effect =
				TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
					TEXT("/Niagara/DefaultAssets/Templates/Systems/"
						"DirectionalBurst.DirectionalBurst")));
			Explosive->DetonationCosmetic.EffectLifeSpan = 0.05f;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Detonation->bAutoArm = false;
			Source.AddInstanceComponent(Detonation);
			Detonation->RegisterComponent();
			Detonation->ConfigureExplosive(Explosive);

			const uint64 Before = Manager->GetRoutedDetonationCount();
			Detonation->Detonate();
			ASSERT_THAT(IsTrue(Detonation->HasDetonated()));
			ASSERT_THAT(IsTrue(
				Manager->GetRoutedDetonationCount() == Before + 1));
		});
	}

	TEST_METHOD(Cosmetics_RemoteAudioPreservesBurstTimingAndArrivalOrder)
	{
		TestCommandBuilder.Do([this]()
		{
			// Isolate delivery timing from the user's PIE network mode and game state clock.
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
				.CreateFXSystem(false);
			UWorld* TestWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None,
				nullptr, true, ERHIFeatureLevel::Num, &Init);
			ASSERT_THAT(IsNotNull(TestWorld));
			ON_SCOPE_EXIT { TestWorld->DestroyWorld(false); };
			UWorld& World = *TestWorld;
			UProjectileCosmeticsManager* Manager = NewObject<UProjectileCosmeticsManager>(TestWorld);
			ASSERT_THAT(IsNotNull(Manager));
			Manager->Initialize(TestWorld);
			ON_SCOPE_EXIT { Manager->Shutdown(); };
			World.TimeSeconds = 10.0;
			APlayerController* Listener = World.SpawnActor<APlayerController>();
			ASSERT_THAT(IsNotNull(Listener));
			APlayerController& Controller = *Listener;
			Controller.Player = NewObject<ULocalPlayer>(GEngine);
			Controller.SetAsLocalPlayerController();
			World.AddController(&Controller); // Preview worlds do not run actor PostInitializeComponents.
			ASSERT_THAT(AreEqual(World.GetNumPlayerControllers(), 1));
			ASSERT_THAT(IsTrue(Controller.IsLocalController()));
			FVector ListenerLocation;
			FRotator ListenerRotation;
			Controller.GetPlayerViewPoint(ListenerLocation, ListenerRotation);

			USoundWave* Sound = NewObject<USoundWave>();
			const uint64 Before = Manager->GetProcessedEventCount();
			auto QueueShot = [&](int32 ID, float EmissionTime, float TravelSeconds)
			{
				FProjectileCosmeticEvent Event;
				Event.EventID = ID;
				Event.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
				Event.Sounds.Add(Sound);
				Event.bUsePropagationDelay = true;
				Event.PropagationSpeed = 1000.f;
				Event.AudibleRadius = 10000.f;
				Event.Location = ListenerLocation + FVector(TravelSeconds * Event.PropagationSpeed, 0.f, 0.f);
				Event.EmissionServerTime = EmissionTime;
				Manager->QueueBroadcastEvent(Event);
			};

			// A distant report must not hold up a closer 700-RPM burst.
			QueueShot(99, 10.f, 2.f);
			constexpr double Interval = 60.0 / 700.0;
			for (int32 Shot = 0; Shot < 6; ++Shot)
			{
				World.TimeSeconds = 10.0 + Shot * Interval;
				QueueShot(Shot, World.GetTimeSeconds(), 0.6f);
			}
			ASSERT_THAT(AreEqual(Manager->GetPendingRemoteAcousticDeliveryCount(), 7));
			World.TimeSeconds = 10.59;
			Manager->Tick(0.f);
			ASSERT_THAT(AreEqual(Manager->GetProcessedEventCount(), Before));
			for (int32 Shot = 0; Shot < 6; ++Shot)
			{
				World.TimeSeconds = 10.601 + Shot * Interval;
				Manager->Tick(0.f);
				ASSERT_THAT(AreEqual(Manager->GetProcessedEventCount(), Before + Shot + 1));
				ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().EventID, Shot));
				ASSERT_THAT(IsTrue(Manager->GetLastProcessedEvent().bServerDelayedAudio));
			}

			// Equal arrival times retain insertion order, even when a later report is queued.
			QueueShot(100, 11.f, 0.5f);
			QueueShot(101, 11.f, 0.5f);
			World.TimeSeconds = 11.501;
			Manager->Tick(0.f);
			ASSERT_THAT(AreEqual(Manager->GetProcessedEventCount(), Before + 8));
			ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().EventID, 101));
			ASSERT_THAT(AreEqual(Manager->GetPendingRemoteAcousticDeliveryCount(), 1));
			World.TimeSeconds = 12.001;
			Manager->Tick(0.f);
			ASSERT_THAT(AreEqual(Manager->GetProcessedEventCount(), Before + 9));
			ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().EventID, 99));
			ASSERT_THAT(AreEqual(Manager->GetPendingRemoteAcousticDeliveryCount(), 0));
		});
	}

	TEST_METHOD(Cosmetics_Manager_IsAutomaticAndOwnsSequenceLifecycle)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager = Subsystem->GetCosmeticsManager();
			ASSERT_THAT(IsNotNull(Manager));

			const uint64 Before = Manager->GetProcessedEventCount();
			FLaunchSequenceCommand Begin;
			Begin.Type = ELaunchSequenceCommandType::Begin;
			Begin.SequenceID = 44;
			Begin.LaunchesPerSecond = 10.f;
			Begin.LaunchEvent.Type = EProjectileCosmeticEventType::Launch;
			Manager->ReceiveSequenceBatch({ Begin });

			ASSERT_THAT(AreEqual(Manager->GetActiveSequenceCount(), 1));
			ASSERT_THAT(IsTrue(Manager->GetProcessedEventCount() == Before + 1));

			FLaunchSequenceCommand End;
			End.Type = ELaunchSequenceCommandType::End;
			End.SequenceID = 44;
			Manager->ReceiveSequenceBatch({ End });
			ASSERT_THAT(AreEqual(Manager->GetActiveSequenceCount(), 0));
		});
	}

	TEST_METHOD(Cosmetics_LoopingNiagaraIsStoppedByAggregateLifetime)
	{
		TestCommandBuilder.Do([this]()
		{
			UWorld& World = Spawner->GetWorld();
			if (World.GetNetMode() == NM_DedicatedServer)
			{
				return;
			}

			UProjectileBallisticsSubsystem* Subsystem =
				World.GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager = Subsystem->GetCosmeticsManager();
			ASSERT_THAT(IsNotNull(Manager));

			UBallisticsLaunchCosmeticProfile* Profile =
				NewObject<UBallisticsLaunchCosmeticProfile>();
			FBallisticsLaunchEffect& LaunchEffect =
				Profile->Effects.AddDefaulted_GetRef();
			LaunchEffect.Effect =
				TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
					TEXT("/Niagara/DefaultAssets/Templates/Systems/"
						"FountainLightweight.FountainLightweight")));
			ASSERT_THAT(IsNotNull(LaunchEffect.Effect.LoadSynchronous()));
			LaunchEffect.LifeSpan = 0.05f;

			FProjectileCosmeticEvent Event;
			Event.Type = EProjectileCosmeticEventType::Launch;
			Event.LaunchEffects = Profile->Effects;
			Event.PlaybackMask = static_cast<uint8>(
				EProjectileCosmeticPlayback::Visual);
			FBallisticsLaunchEffectTransform& Transform =
				Event.LaunchEffectTransforms.AddDefaulted_GetRef();
			Transform.bValid = true;
			const uint64 ProcessedBefore = Manager->GetProcessedEventCount();
			Manager->PlayLocalEvent(Event);
			ASSERT_THAT(IsTrue(
				Manager->GetProcessedEventCount() == ProcessedBefore + 1));
			if (Manager->GetActiveCosmeticEffectCount() == 0)
			{
				// NullRHI does not create Niagara render components. Inject a real
				UNiagaraComponent* HeadlessComponent =
					NewObject<UNiagaraComponent>(&World);
				HeadlessComponent->SetAsset(LaunchEffect.Effect.Get());
				HeadlessComponent->RegisterComponentWithWorld(&World);
				Manager->TrackCosmeticEffectForTesting(HeadlessComponent, 0.05f);
			}
			ASSERT_THAT(AreEqual(Manager->GetActiveCosmeticEffectCount(), 1));

			Manager->Tick(0.06f);
			ASSERT_THAT(AreEqual(Manager->GetActiveCosmeticEffectCount(), 0));
		});
	}

	TEST_METHOD(Performance_ProjectileTickStartsDisabledUntilOwnershipIsResolved)
	{
		const UProjectileBallisticsComponent* Component =
			GetDefault<UProjectileBallisticsComponent>();
		ASSERT_THAT(IsNotNull(Component));
		ASSERT_THAT(IsFalse(Component->PrimaryComponentTick.bStartWithTickEnabled));
	}

	TEST_METHOD(Performance_ExternalTickIsBlockedWhenAggregateTickOwnsSimulation)
	{
		TestRunner->SetSuppressLogWarnings();
		TestCommandBuilder.Do([this]()
		{
			const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
			ASSERT_THAT(IsNotNull(Settings));
			ASSERT_THAT(IsTrue(Settings->bEnableTickBatching));

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			ASSERT_THAT(IsNotNull(Projectile.Ballistics));

			Projectile.Ballistics->SetComponentTickEnabled(true);
			ASSERT_THAT(IsTrue(Projectile.Ballistics->IsComponentTickEnabled()));
			ASSERT_THAT(IsFalse(
				Projectile.Ballistics->EnforceSimulationTickOwnership()));

			ASSERT_THAT(IsFalse(Projectile.Ballistics->IsComponentTickEnabled()));
		});
	}

	TEST_METHOD(Performance_ImpactDetonationDefersAggregateUnregistration)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(500.f, 0.f, 100.f));

			ANPSTestProjectile& ExplosiveProjectile =
				Spawner->SpawnActor<ANPSTestProjectile>();
			ANPSTestProjectile& FollowingProjectile =
				Spawner->SpawnActor<ANPSTestProjectile>();

			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&ExplosiveProjectile);
			Explosive->DetonationTrigger = EDetonationTrigger::OnImpact;
			Explosive->bEnableBlastDamage = false;
			Explosive->bEnableShrapnel = false;
			ExplosiveProjectile.Detonation->ConfigureExplosive(Explosive);
			ExplosiveProjectile.Detonation->OnDetonation.AddDynamic(
				&ExplosiveProjectile,
				&ANPSTestProjectile::DestroyOnDetonation);

			FProjectileInitialConditions ExplosiveConditions;
			ExplosiveConditions.Origin = FVector(0.f, 0.f, 100.f);
			ExplosiveConditions.Direction = FVector::ForwardVector;
			ExplosiveConditions.InitialSpeed = 10000.f;
			ExplosiveConditions.Mass = 1.f;
			ExplosiveConditions.Diameter = 1.f;
			ExplosiveConditions.DragCoefficient = 0.f;
			ExplosiveProjectile.Ballistics->InitializeProjectile(
				ExplosiveConditions);

			FProjectileInitialConditions FollowingConditions =
				ExplosiveConditions;
			FollowingConditions.Origin = FVector(0.f, 1000.f, 100.f);
			FollowingProjectile.Ballistics->InitializeProjectile(
				FollowingConditions);

			ASSERT_THAT(AreEqual(Subsystem->GetActiveProjectileCount(), 2));
			Subsystem->Tick(0.1f);

			ASSERT_THAT(IsTrue(ExplosiveProjectile.IsActorBeingDestroyed()));
			ASSERT_THAT(IsTrue(
				FollowingProjectile.Ballistics->GetElapsedTime() > 0.f));
			ASSERT_THAT(AreEqual(Subsystem->GetActiveProjectileCount(), 1));
		});
	}

	TEST_METHOD(Performance_LocalCosmeticsAreHardBoundedPerFrame)
	{
		TestRunner->SetSuppressLogWarnings();
		TestCommandBuilder
			.StartWhen(TEXT("Wait for local cosmetic test world"), [this]()
			{
				PendingCosmeticWorld = &Spawner->GetWorld();
				if (PendingCosmeticWorld->GetNetMode() != NM_DedicatedServer)
				{
					return true;
				}
				if (!GEngine)
				{
					return false;
				}

				const FString MapName =
					UWorld::RemovePIEPrefix(PendingCosmeticWorld->GetMapName());
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* Candidate = Context.World();
					if (Candidate &&
						Candidate->GetNetMode() == NM_Client &&
						UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
					{
						PendingCosmeticWorld = Candidate;
						return true;
					}
				}
				return false;
			})
			.Then([this]()
			{
			UWorld* CosmeticWorld = PendingCosmeticWorld.Get();
			ASSERT_THAT(IsNotNull(CosmeticWorld));
			ASSERT_THAT(IsFalse(CosmeticWorld->GetNetMode() == NM_DedicatedServer));

			UProjectileBallisticsSubsystem* Subsystem =
				CosmeticWorld->GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager = Subsystem->GetCosmeticsManager();
			ASSERT_THAT(IsNotNull(Manager));

			const uint64 Before = Manager->GetProcessedEventCount();
			FProjectileCosmeticEvent Event;
			Event.Type = EProjectileCosmeticEventType::Launch;
			for (int32 Index = 0; Index < 1024; ++Index)
			{
				Manager->PlayLocalEvent(Event);
			}

			const uint64 Accepted = Manager->GetProcessedEventCount() - Before;
			const int32 Budget = FMath::Clamp(
				UBallisticsCoreSettings::Get()->MaxCosmeticEventsPerConnectionPerFrame,
				1,
				256);
			ASSERT_THAT(IsTrue(Accepted > 0));
			ASSERT_THAT(IsTrue(Accepted <= static_cast<uint64>(Budget)));
			});
	}

	TEST_METHOD(NetProxy_PublishingLifecycle_SupportsPooledReuse)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			ASSERT_THAT(IsNotNull(Projectile.NetProxy));
			ASSERT_THAT(IsTrue(Projectile.NetProxy->GetIsReplicated()));
			ASSERT_THAT(IsTrue(Projectile.NetProxy->IsPublishingTransform()));

			Projectile.NetProxy->StopReplicatingTransform();
			ASSERT_THAT(IsFalse(Projectile.NetProxy->IsPublishingTransform()));
			Projectile.NetProxy->StartReplicatingTransform();
			ASSERT_THAT(IsTrue(Projectile.NetProxy->IsPublishingTransform()));
		});
	}

	TEST_METHOD(Projectile_RestOnSurface_OffsetsVisibleBodyByRadius)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestTarget& Floor = Spawner->SpawnActor<ANPSTestTarget>();
			Floor.SetActorLocation(FVector::ZeroVector);

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->bRestOnSurface = true;
			Projectile.Ballistics->bEnableSliding = false;
			Projectile.Ballistics->DefaultMaterialResponse.Toughness = TNumericLimits<float>::Max();
			Projectile.Ballistics->DefaultMaterialResponse.RicochetAngleThreshold = 0.f;
			Projectile.Ballistics->DefaultMaterialResponse.DefaultResponse = EImpactResponse::Embed;

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 300.f);
			Conditions.Direction = FVector::DownVector;
			Conditions.InitialSpeed = 3000.f;
			Conditions.Mass = 1.f;
			Conditions.Diameter = 40.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.1f);

			ASSERT_THAT(IsTrue(
				Projectile.Ballistics->GetCurrentState() == EProjectileState::Embedded));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
				Projectile.Ballistics->GetCurrentSnapshot().Position.Z,
				140.f,
				2.f)));
		});
	}

	TEST_METHOD(MaterialResponse_MappedAndFallbackSurfaces_ReturnExpectedValues)
	{
		UBallisticMaterialDataAsset* Materials = NewObject<UBallisticMaterialDataAsset>();
		Materials->DefaultResponse.Toughness = 55.f;
		FBallisticMaterialResponse Steel;
		Steel.Toughness = 9000.f;
		Steel.CoefficientOfRestitution = 0.8f;
		Materials->SurfaceResponseMap.Add(EPhysicalSurface::SurfaceType1, Steel);

		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
			Materials->GetResponseForSurface(EPhysicalSurface::SurfaceType1).Toughness, 9000.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
			Materials->GetResponseForSurface(EPhysicalSurface::SurfaceType2).Toughness, 55.f)));
	}

	TEST_METHOD(Subsystem_PredictionRegistrationQueryAndBatchTicker_WorkTogether)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			ASSERT_THAT(IsNotNull(Subsystem->GetAggregateTicker()));

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(500.f, 0.f, 200.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 10000.f;
			Conditions.DragCoefficient = 0.f;
			const FTrajectoryPredictionResult Prediction =
				Subsystem->PredictTrajectory(Conditions, 0.1f, 0.02f);
			ASSERT_THAT(AreEqual(Prediction.Points.Num(), 6));
			ASSERT_THAT(AreEqual(Prediction.Points.Num(), Prediction.Snapshots.Num()));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Prediction.TotalTime, 0.1f)));

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->InitializeProjectile(Conditions);
			const TArray<UProjectileBallisticsComponent*> Nearby =
				Subsystem->GetProjectilesInRadius(Conditions.Origin, 10.f);
			ASSERT_THAT(IsTrue(Nearby.Contains(Projectile.Ballistics)));

			APawn& Listener = Spawner->SpawnActor<APawn>();
			Subsystem->RegisterFlyByListener(&Listener);
			ASSERT_THAT(IsTrue(Subsystem->GetFlyByListeners().Contains(&Listener)));
			Subsystem->UnregisterFlyByListener(&Listener);
			ASSERT_THAT(IsFalse(Subsystem->GetFlyByListeners().Contains(&Listener)));
		});
	}

	TEST_METHOD(Subsystem_AutomaticFlyByListenersTrackPossessionEvents)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			APawn& Listener = Spawner->SpawnActor<APawn>();
			APlayerController& Controller = Spawner->SpawnActor<APlayerController>();
			ASSERT_THAT(IsFalse(Subsystem->GetFlyByListeners().Contains(&Listener)));
			Controller.Possess(&Listener);
			ASSERT_THAT(IsTrue(Subsystem->GetFlyByListeners().Contains(&Listener)));
			Controller.UnPossess();
			ASSERT_THAT(IsFalse(Subsystem->GetFlyByListeners().Contains(&Listener)));

			Subsystem->RegisterFlyByListener(&Listener);
			Controller.Possess(&Listener);
			Controller.UnPossess();
			ASSERT_THAT(IsTrue(Subsystem->GetFlyByListeners().Contains(&Listener)));
			Subsystem->UnregisterFlyByListener(&Listener);
			ASSERT_THAT(IsFalse(Subsystem->GetFlyByListeners().Contains(&Listener)));
		});
	}

	TEST_METHOD(Projectile_FlyByDetection_TriggersEachListenerOnce)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestFlyByListener& Listener =
				Spawner->SpawnActor<ANPSTestFlyByListener>();
			Listener.SetActorLocation(FVector(500.f, 50.f, 10000.f));
			Subsystem->RegisterFlyByListener(&Listener);

			ANPSTestProjectile& Projectile =
				Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->FlyByDetectionRadius = 150.f;
			Projectile.Ballistics->MinFlyBySpeedCmS = 34000.f;
			Projectile.Ballistics->OnProjectileFlyBy.AddDynamic(
				&Listener,
				&ANPSTestFlyByListener::RecordFlyBy);

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 10000.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 50000.f;
			Conditions.Mass = 5.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.02f);

			ASSERT_THAT(AreEqual(Listener.FlyByCount, 1));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
				Listener.LastMissDistance, 50.f, 1.f)));

			Projectile.Ballistics->FastForward(0.02f);
			ASSERT_THAT(AreEqual(Listener.FlyByCount, 1));
			Subsystem->UnregisterFlyByListener(&Listener);
		});
	}

	TEST_METHOD(Projectile_FlyByDetection_DoesNotTriggerAfterRicochetMovesAway)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestTarget& RicochetSurface = Spawner->SpawnActor<ANPSTestTarget>();
			RicochetSurface.SetActorLocation(FVector(200.f, 0.f, 10000.f));

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->FlyByDetectionRadius = 150.f;
			Projectile.Ballistics->MinFlyBySpeedCmS = 34000.f;
			Projectile.Ballistics->DefaultMaterialResponse.Toughness = TNumericLimits<float>::Max();
			Projectile.Ballistics->DefaultMaterialResponse.RicochetAngleThreshold = 91.f;
			Projectile.Ballistics->DefaultMaterialResponse.CoefficientOfRestitution = 1.f;

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 10000.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 50000.f;
			Conditions.Mass = 5.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.005f);
			ASSERT_THAT(AreEqual(
				Projectile.Ballistics->GetCurrentState(),
				EProjectileState::Ricocheted));

			ANPSTestFlyByListener& Listener =
				Spawner->SpawnActor<ANPSTestFlyByListener>();
			Listener.SetActorLocation(FVector(200.f, 50.f, 10000.f));
			Subsystem->RegisterFlyByListener(&Listener);
			Projectile.Ballistics->OnProjectileFlyBy.AddDynamic(
				&Listener,
				&ANPSTestFlyByListener::RecordFlyBy);

			Projectile.Ballistics->FastForward(0.005f);

			ASSERT_THAT(AreEqual(Listener.FlyByCount, 0));
			Subsystem->UnregisterFlyByListener(&Listener);
		});
	}

	TEST_METHOD(Projectile_FlyByDetection_StopsAtBlockingImpact)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));

			ANPSTestTarget& BlockingSurface = Spawner->SpawnActor<ANPSTestTarget>();
			BlockingSurface.SetActorLocation(FVector(100.f, 0.f, 10000.f));

			ANPSTestFlyByListener& Listener =
				Spawner->SpawnActor<ANPSTestFlyByListener>();
			Listener.SetActorLocation(FVector(200.f, 50.f, 10000.f));
			Subsystem->RegisterFlyByListener(&Listener);

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->FlyByDetectionRadius = 150.f;
			Projectile.Ballistics->MinFlyBySpeedCmS = 34000.f;
			Projectile.Ballistics->DefaultMaterialResponse.Toughness = TNumericLimits<float>::Max();
			Projectile.Ballistics->DefaultMaterialResponse.RicochetAngleThreshold = 0.f;
			Projectile.Ballistics->DefaultMaterialResponse.DefaultResponse = EImpactResponse::Embed;
			Projectile.Ballistics->OnProjectileFlyBy.AddDynamic(
				&Listener,
				&ANPSTestFlyByListener::RecordFlyBy);

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 10000.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 50000.f;
			Conditions.Mass = 5.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.005f);

			ASSERT_THAT(AreEqual(Listener.FlyByCount, 0));
			ASSERT_THAT(AreEqual(
				Projectile.Ballistics->GetCurrentState(),
				EProjectileState::Embedded));
			Subsystem->UnregisterFlyByListener(&Listener);
		});
	}

	TEST_METHOD(Projectile_Penetration_DoesNotTreatStartInsideHitAsExit)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestTarget& ThickSurface = Spawner->SpawnActor<ANPSTestTarget>();
			ThickSurface.SetActorLocation(FVector(300.f, 0.f, 10000.f));
			ThickSurface.Collision->SetBoxExtent(FVector(200.f, 120.f, 120.f));

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->MaxPenetrationDepth = 100.f;
			UBallisticMaterialDataAsset* Materials =
				NewObject<UBallisticMaterialDataAsset>(&Projectile);
			Materials->DefaultResponse.Toughness = 1.f;
			Materials->DefaultResponse.RicochetAngleThreshold = 0.f;
			Projectile.Ballistics->MaterialDataAsset = Materials;

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 10000.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 50000.f;
			Conditions.Mass = 5.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.01f);

			ASSERT_THAT(AreEqual(ThickSurface.HitCount, 1));
			Projectile.Ballistics->FastForward(0.01f);
			ASSERT_THAT(AreEqual(ThickSurface.HitCount, 1));
			ASSERT_THAT(AreEqual(
				Projectile.Ballistics->GetCurrentState(),
				EProjectileState::Embedded));
		});
	}

	TEST_METHOD(Projectile_Penetration_UsesFarFaceOfEntryCollider)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestTarget& ThinSurface = Spawner->SpawnActor<ANPSTestTarget>();
			ThinSurface.SetActorLocation(FVector(100.f, 0.f, 10000.f));

			ANPSTestProjectile& Projectile = Spawner->SpawnActor<ANPSTestProjectile>();
			Projectile.Ballistics->MaxPenetrationDepth = 100.f;
			UBallisticMaterialDataAsset* Materials =
				NewObject<UBallisticMaterialDataAsset>(&Projectile);
			Materials->DefaultResponse.Toughness = 1.f;
			Materials->DefaultResponse.RicochetAngleThreshold = 0.f;
			Projectile.Ballistics->MaterialDataAsset = Materials;

			FProjectileInitialConditions Conditions;
			Conditions.Origin = FVector(0.f, 0.f, 10000.f);
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 50000.f;
			Conditions.Mass = 5.f;
			Conditions.DragCoefficient = 0.f;
			Projectile.Ballistics->InitializeProjectile(Conditions);
			Projectile.Ballistics->FastForward(0.005f);

			ASSERT_THAT(AreEqual(ThinSurface.HitCount, 1));
			ASSERT_THAT(AreEqual(
				Projectile.Ballistics->GetCurrentState(),
				EProjectileState::Penetrating));
			ASSERT_THAT(IsTrue(
				Projectile.Ballistics->GetCurrentSnapshot().Position.X > 140.f));
		});
	}

	TEST_METHOD(Launcher_ProjectileAndAuthorityOverrides_AreApplied)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			UBallisticsProjectileDataAsset* Projectile = NewObject<UBallisticsProjectileDataAsset>(&TestLauncher);
			TestLauncher.Launcher->SetProjectileDataAsset(Projectile);
			ASSERT_THAT(IsTrue(TestLauncher.Launcher->GetProjectileDataAsset() == Projectile));

			Projectile->bOverrideAuthorityMode = true;
			Projectile->AuthorityModeOverride = ELaunchAuthorityMode::ClientAuthoritative;
			ASSERT_THAT(IsTrue(
				TestLauncher.Launcher->GetActiveAuthorityMode() == ELaunchAuthorityMode::ClientAuthoritative));
		});
	}

	TEST_METHOD(Launcher_ProjectileExplosiveConfig_ConfiguresSpawnedProjectile)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestLauncher& TestLauncher = Spawner->SpawnActor<ANPSTestLauncher>();
			UBallisticsProjectileDataAsset* Projectile =
				NewObject<UBallisticsProjectileDataAsset>(&TestLauncher);
			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&TestLauncher);
			Explosive->DetonationTrigger = EDetonationTrigger::FuseTimer;
			Explosive->FuseTime = 30.f;
			Explosive->BlastDamage = 0.f;
			Explosive->ShrapnelCount = 0;

			Projectile->ProjectileClass = ANPSTestProjectile::StaticClass();
			Projectile->ExplosiveConfig = Explosive;
			Projectile->InitialSpeed = 1000.f;
			Projectile->DragCoefficient = 0.f;
			Projectile->bOverrideAuthorityMode = true;
			Projectile->AuthorityModeOverride = ELaunchAuthorityMode::ServerAuthoritative;
			TestLauncher.Launcher->SetProjectileDataAsset(Projectile);
			TestLauncher.Launcher->Launch(FVector(0.f, 0.f, 1000.f), FVector::ForwardVector);

			ANPSTestProjectile* SpawnedProjectile = nullptr;
			for (TActorIterator<ANPSTestProjectile> It(&Spawner->GetWorld()); It; ++It)
			{
				SpawnedProjectile = *It;
				break;
			}

			ASSERT_THAT(IsNotNull(SpawnedProjectile));
			ASSERT_THAT(IsNotNull(SpawnedProjectile->Detonation));
			ASSERT_THAT(IsTrue(
				SpawnedProjectile->Detonation->ExplosiveDataAsset == Explosive));
			ASSERT_THAT(IsTrue(SpawnedProjectile->Detonation->IsFuseArmed()));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
				SpawnedProjectile->Detonation->GetRemainingFuseTime(), 30.f)));
		});
	}

	TEST_METHOD(Explosive_ManualDetonation_IsIdempotentAndFindsBlastTarget)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& Source = Spawner->SpawnActor<AActor>();
			Source.SetActorLocation(FVector::ZeroVector);
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(100.f, 0.f, 0.f));
			Target.Collision->SetCollisionObjectType(ECC_Pawn);
			Target.Collision->UpdateOverlaps();

			UBallisticsExplosiveDataAsset* Explosive = NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->DetonationTrigger = EDetonationTrigger::Manual;
			Explosive->BlastRadius = 500.f;
			Explosive->BlastInnerRadius = 50.f;
			Explosive->BlastDamage = 100.f;
			Explosive->BlastMinDamage = 10.f;
			Explosive->bBlastRequiresLineOfSight = false;
			Explosive->ShrapnelCount = 0;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Source.AddInstanceComponent(Detonation);
			Detonation->ExplosiveDataAsset = Explosive;
			Detonation->RegisterComponent();

			const FDetonationResult First = Detonation->Detonate();
			ASSERT_THAT(IsTrue(Detonation->HasDetonated()));
			ASSERT_THAT(AreEqual(First.TotalActorsDamaged, 1));
			ASSERT_THAT(IsTrue(First.BlastHits.ContainsByPredicate(
				[&Target](const FShrapnelHit& Hit) { return Hit.HitActor.Get() == &Target; })));

			const FDetonationResult Second = Detonation->Detonate();
			ASSERT_THAT(AreEqual(Second.TotalActorsDamaged, 0));
			ASSERT_THAT(AreEqual(Second.BlastHits.Num(), 0));
		});
	}

	TEST_METHOD(Explosive_FuseArming_ReportsCountdownState)
	{
		TestCommandBuilder.Do([this]()
		{
			AActor& Source = Spawner->SpawnActor<AActor>();
			UBallisticsExplosiveDataAsset* Explosive = NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->DetonationTrigger = EDetonationTrigger::FuseTimer;
			Explosive->FuseTime = 1.25f;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Source.AddInstanceComponent(Detonation);
			Detonation->ExplosiveDataAsset = Explosive;
			Detonation->RegisterComponent();
			Detonation->ArmFuse();

			ASSERT_THAT(IsTrue(Detonation->IsFuseArmed()));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Detonation->GetRemainingFuseTime(), 1.25f)));
			ASSERT_THAT(IsFalse(Detonation->IsComponentTickEnabled()));
			ASSERT_THAT(IsFalse(Detonation->HasDetonated()));
		});
	}

	TEST_METHOD(Performance_ExplosiveFusesUseOneAggregatePass)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UBallisticsAggregateTick* Aggregate = Subsystem->GetAggregateTicker();
			ASSERT_THAT(IsNotNull(Aggregate));

			TArray<TWeakObjectPtr<UBallisticsDetonationComponent>> Fuses;
			for (int32 Index = 0; Index < 128; ++Index)
			{
				AActor& Source = Spawner->SpawnActor<AActor>();
				UBallisticsExplosiveDataAsset* Explosive =
					NewObject<UBallisticsExplosiveDataAsset>(&Source);
				Explosive->DetonationTrigger = EDetonationTrigger::FuseTimer;
				Explosive->FuseTime = 0.5f;
				Explosive->BlastDamage = 0.f;
				Explosive->ShrapnelCount = 0;

				UBallisticsDetonationComponent* Detonation =
					NewObject<UBallisticsDetonationComponent>(&Source);
				Detonation->bAutoArm = false;
				Source.AddInstanceComponent(Detonation);
				Detonation->RegisterComponent();
				Detonation->ConfigureExplosive(Explosive);
				Detonation->ArmFuse();
				Fuses.Add(Detonation);

				ASSERT_THAT(IsFalse(Detonation->IsComponentTickEnabled()));
			}

			ASSERT_THAT(AreEqual(Aggregate->GetArmedDetonationCount(), 128));
			Aggregate->TickServices(0.25f);
			ASSERT_THAT(AreEqual(Aggregate->GetArmedDetonationCount(), 128));
			for (const TWeakObjectPtr<UBallisticsDetonationComponent>& Fuse : Fuses)
			{
				ASSERT_THAT(IsTrue(Fuse.IsValid()));
				ASSERT_THAT(IsTrue(Fuse->IsFuseArmed()));
				ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
					Fuse->GetRemainingFuseTime(), 0.25f)));
			}

			Aggregate->TickServices(0.25f);
			ASSERT_THAT(AreEqual(Aggregate->GetArmedDetonationCount(), 0));
			for (const TWeakObjectPtr<UBallisticsDetonationComponent>& Fuse : Fuses)
			{
				ASSERT_THAT(IsTrue(Fuse.IsValid()));
				ASSERT_THAT(IsTrue(Fuse->HasDetonated()));
			}
		});
	}

	TEST_METHOD(Explosive_ImpactTrigger_DetonatesOnCollision)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestProjectile& Source = Spawner->SpawnActor<ANPSTestProjectile>();
			Source.SetActorLocation(FVector(0.f, 0.f, 100.f));
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));

			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->DetonationTrigger = EDetonationTrigger::OnImpact;
			Explosive->BlastDamage = 0.f;
			Explosive->ShrapnelCount = 0;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Source.AddInstanceComponent(Detonation);
			Detonation->ExplosiveDataAsset = Explosive;
			Detonation->RegisterComponent();

			FProjectileInitialConditions Conditions;
			Conditions.Origin = Source.GetActorLocation();
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 10000.f;
			Conditions.Mass = 9.5f;
			Conditions.DragCoefficient = 0.f;
			Source.Ballistics->InitializeProjectile(Conditions);
			Source.Ballistics->FastForward(0.2f);

			ASSERT_THAT(IsTrue(Detonation->HasDetonated()));
		});
	}

	TEST_METHOD(Explosive_ImpactTrigger_RespectsArmingDistance)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestProjectile& Source = Spawner->SpawnActor<ANPSTestProjectile>();
			Source.SetActorLocation(FVector(0.f, 0.f, 100.f));
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));

			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->DetonationTrigger = EDetonationTrigger::OnImpact;
			Explosive->ArmingDistance = 2000.f;
			Explosive->BlastDamage = 0.f;
			Explosive->ShrapnelCount = 0;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Source.AddInstanceComponent(Detonation);
			Detonation->ExplosiveDataAsset = Explosive;
			Detonation->RegisterComponent();

			FProjectileInitialConditions Conditions;
			Conditions.Origin = Source.GetActorLocation();
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 10000.f;
			Conditions.Mass = 9.5f;
			Conditions.DragCoefficient = 0.f;
			Source.Ballistics->InitializeProjectile(Conditions);
			Source.Ballistics->FastForward(0.2f);

			ASSERT_THAT(IsFalse(Detonation->HasDetonated()));
		});
	}

	TEST_METHOD(Explosive_RestTrigger_DetonatesWhenProjectileEmbeds)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSTestProjectile& Source = Spawner->SpawnActor<ANPSTestProjectile>();
			Source.SetActorLocation(FVector(0.f, 0.f, 100.f));
			ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
			Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));

			UBallisticsExplosiveDataAsset* Explosive =
				NewObject<UBallisticsExplosiveDataAsset>(&Source);
			Explosive->DetonationTrigger = EDetonationTrigger::OnRest;
			Explosive->BlastDamage = 0.f;
			Explosive->ShrapnelCount = 0;

			UBallisticsDetonationComponent* Detonation =
				NewObject<UBallisticsDetonationComponent>(&Source);
			Source.AddInstanceComponent(Detonation);
			Detonation->ExplosiveDataAsset = Explosive;
			Detonation->RegisterComponent();

			FProjectileInitialConditions Conditions;
			Conditions.Origin = Source.GetActorLocation();
			Conditions.Direction = FVector::ForwardVector;
			Conditions.InitialSpeed = 10000.f;
			Conditions.Mass = 9.5f;
			Conditions.DragCoefficient = 0.f;
			Source.Ballistics->InitializeProjectile(Conditions);
			Source.Ballistics->FastForward(0.2f);

			ASSERT_THAT(IsTrue(Source.Ballistics->GetCurrentState() == EProjectileState::Embedded));
			ASSERT_THAT(IsTrue(Detonation->HasDetonated()));
		});
	}

	TEST_METHOD(Explosive_FuseTimer_DetonatesAfterConfiguredDelay)
	{
		TestCommandBuilder
			.Do([this]()
			{
				AActor& Source = Spawner->SpawnActor<AActor>();
				UBallisticsExplosiveDataAsset* Explosive =
					NewObject<UBallisticsExplosiveDataAsset>(&Source);
				Explosive->DetonationTrigger = EDetonationTrigger::FuseTimer;
				Explosive->FuseTime = 0.02f;
				Explosive->BlastDamage = 0.f;
				Explosive->ShrapnelCount = 0;

				UBallisticsDetonationComponent* Detonation =
					NewObject<UBallisticsDetonationComponent>(&Source);
				Source.AddInstanceComponent(Detonation);
				Detonation->ExplosiveDataAsset = Explosive;
				Detonation->RegisterComponent();
				Detonation->ArmFuse();
				PendingDetonation = Detonation;
			})
			.Until(TEXT("Fuse expires"), [this]()
			{
				return PendingDetonation.IsValid() && PendingDetonation->HasDetonated();
			})
			.Then([this]()
			{
				ASSERT_THAT(IsTrue(PendingDetonation.IsValid()));
				ASSERT_THAT(IsTrue(PendingDetonation->HasDetonated()));
				ASSERT_THAT(IsFalse(PendingDetonation->IsFuseArmed() &&
					PendingDetonation->GetRemainingFuseTime() > 0.f));
			});
	}

	TEST_METHOD(Tracer_TerminalState_FadesAndDestroysProjectile)
	{
		TestCommandBuilder
			.Do([this]()
			{
				ANPSTestProjectile& Source = Spawner->SpawnActor<ANPSTestProjectile>();
				Source.SetActorLocation(FVector(0.f, 0.f, 100.f));
				Source.Tracer->FadeOutDuration = 0.01f;
				Source.Tracer->bDestroyAfterFadeOut = true;
				ANPSTestTarget& Target = Spawner->SpawnActor<ANPSTestTarget>();
				Target.SetActorLocation(FVector(1000.f, 0.f, 100.f));

				FProjectileInitialConditions Conditions;
				Conditions.Origin = Source.GetActorLocation();
				Conditions.Direction = FVector::ForwardVector;
				Conditions.InitialSpeed = 10000.f;
				Conditions.Mass = 9.5f;
				Conditions.DragCoefficient = 0.f;
				Source.Ballistics->InitializeProjectile(Conditions);
				Source.Ballistics->FastForward(0.2f);

				ASSERT_THAT(IsTrue(Source.Ballistics->GetCurrentState() == EProjectileState::Embedded));
				PendingTracerActor = &Source;
			})
			.Until(TEXT("Tracer fade destroys projectile"), [this]()
			{
				return !PendingTracerActor.IsValid() || PendingTracerActor->IsActorBeingDestroyed();
			});
	}

	TEST_METHOD(DebugCommands_AllDocumentedControls_AreRegistered)
	{
		static const TCHAR* CommandNames[] = {
			TEXT("NPS.Debug.Projectiles"),
			TEXT("NPS.Debug.Hitscan"),
			TEXT("NPS.Debug.Detonation"),
			TEXT("NPS.Debug.Explosions"),
			TEXT("NPS.Debug.Budget"),
			TEXT("NPS.Debug.All")
		};

		for (const TCHAR* CommandName : CommandNames)
		{
			ASSERT_THAT(IsNotNull(IConsoleManager::Get().FindConsoleObject(CommandName)));
		}
	}

	TEST_METHOD(Spread_RandomCone_IsDeterministicAndNormalized)
	{
		UBallisticsSpreadDataAsset* Spread = NewObject<UBallisticsSpreadDataAsset>();
		Spread->SpreadMode = ESpreadMode::RandomCone;
		Spread->ProjectileCount = 12;
		Spread->ConeHalfAngle = 6.f;

		FRandomStream A(1337);
		FRandomStream B(1337);
		TArray<FVector> DirectionsA;
		TArray<FVector> DirectionsB;
		Spread->GenerateProjectileDirections(FVector::ForwardVector, A, DirectionsA);
		Spread->GenerateProjectileDirections(FVector::ForwardVector, B, DirectionsB);

		ASSERT_THAT(AreEqual(DirectionsA.Num(), 12));
		ASSERT_THAT(AreEqual(DirectionsA.Num(), DirectionsB.Num()));
		const float MinimumForwardDot = FMath::Cos(FMath::DegreesToRadians(Spread->ConeHalfAngle));
		for (int32 Index = 0; Index < DirectionsA.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(DirectionsA[Index].Equals(DirectionsB[Index], UE_KINDA_SMALL_NUMBER)));
			ASSERT_THAT(IsTrue(DirectionsA[Index].IsNormalized()));
			ASSERT_THAT(IsTrue(
				FVector::DotProduct(FVector::ForwardVector, DirectionsA[Index]) >=
				MinimumForwardDot - UE_KINDA_SMALL_NUMBER));
		}
	}

	TEST_METHOD(Spread_FixedPattern_PreservesRepeatedYawPoints)
	{
		UBallisticsSpreadDataAsset* Spread = NewObject<UBallisticsSpreadDataAsset>();
		Spread->SpreadMode = ESpreadMode::FixedPattern;
		Spread->Pattern.SetNum(3);
		Spread->Pattern[0].YawOffset = 0.f;
		Spread->Pattern[0].PitchOffset = -2.f;
		Spread->Pattern[1].YawOffset = 0.f;
		Spread->Pattern[1].PitchOffset = 0.f;
		Spread->Pattern[2].YawOffset = 0.f;
		Spread->Pattern[2].PitchOffset = 2.f;
		FRandomStream Stream(7);
		TArray<FVector> Directions;
		Spread->GenerateProjectileDirections(FVector::ForwardVector, Stream, Directions);
		ASSERT_THAT(AreEqual(Directions.Num(), 3));
		ASSERT_THAT(IsTrue(Directions[0].Z < 0.f));
		ASSERT_THAT(IsTrue(FMath::IsNearlyZero(Directions[1].Z)));
		ASSERT_THAT(IsTrue(Directions[2].Z > 0.f));
	}

	TEST_METHOD(Explosive_Falloff_RespectsInnerOuterAndBeyondRadius)
	{
		UBallisticsExplosiveDataAsset* Explosive = NewObject<UBallisticsExplosiveDataAsset>();
		Explosive->BlastRadius = 500.f;
		Explosive->BlastInnerRadius = 100.f;
		Explosive->BlastDamage = 150.f;
		Explosive->BlastMinDamage = 10.f;

		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Explosive->CalculateBlastDamageAtDistance(50.f), 150.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Explosive->CalculateBlastDamageAtDistance(500.f), 10.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyZero(Explosive->CalculateBlastDamageAtDistance(501.f))));
	}

	TEST_METHOD(Explosive_ShrapnelConfig_ReflectsDataAsset)
	{
		UBallisticsExplosiveDataAsset* Explosive = NewObject<UBallisticsExplosiveDataAsset>();
		Explosive->ShrapnelRange = 4200.f;
		Explosive->ShrapnelMass = 2.5f;
		Explosive->ShrapnelInitialSpeed = 35000.f;
		Explosive->ShrapnelMaxRicochets = 2;
		Explosive->ShrapnelMaxPenetrations = 3;
		const FHitscanConfig Config = Explosive->BuildShrapnelHitscanConfig(99, {});
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Config.MaxRange, 4200.f)));
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Config.SimulatedMass, 2.5f)));
		ASSERT_THAT(AreEqual(Config.MaxRicochets, 2));
		ASSERT_THAT(AreEqual(Config.MaxPenetrations, 3));
		ASSERT_THAT(AreEqual(Config.RandomSeed, 99));
	}

	TEST_METHOD(ImpactBudget_Reset_ClearsFrameUsage)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UBallisticsImpactBudget* Budget = Subsystem->GetImpactBudget();
			ASSERT_THAT(IsNotNull(Budget));

			FImpactBudgetRequest Request;
			Request.Priority = EImpactPriority::Critical;
			Budget->SubmitImpact(Request);
			ASSERT_THAT(AreEqual(Budget->GetFrameImpactsUsed(), 1));
			Budget->ResetFrameBudget();
			ASSERT_THAT(AreEqual(Budget->GetFrameImpactsUsed(), 0));
		});
	}

	TEST_METHOD(Examples_DataAssetCatalog_LoadsWithoutMissingReferences)
	{
		static const TCHAR* AssetPaths[] = {
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Arrow.DA_NPS_Arrow"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientAuth.DA_NPS_ClientAuth"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientPredicted.DA_NPS_ClientPredicted"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Crosswind.DA_NPS_Crosswind"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveFuse.DA_NPS_ExplosiveFuse"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveImpact.DA_NPS_ExplosiveImpact"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveManual.DA_NPS_ExplosiveManual"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveRest.DA_NPS_ExplosiveRest"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Hitscan.DA_NPS_Hitscan"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Penetrator.DA_NPS_Penetrator"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Ricochet.DA_NPS_Ricochet"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ServerAuth.DA_NPS_ServerAuth"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadPattern.DA_NPS_SpreadPattern"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadRandom.DA_NPS_SpreadRandom"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Stress.DA_NPS_Stress"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Subsonic.DA_NPS_Subsonic"),
			TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchModifier.DA_NPS_LaunchModifier"),
			TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"),
			TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Fuse.DA_NPS_Fuse"),
			TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Impact.DA_NPS_Impact"),
			TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Manual.DA_NPS_Manual"),
			TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Rest.DA_NPS_Rest"),
			TEXT("/NPS/Examples/Data/Materials/DA_NPS_Embed.DA_NPS_Embed"),
			TEXT("/NPS/Examples/Data/Materials/DA_NPS_Penetration.DA_NPS_Penetration"),
			TEXT("/NPS/Examples/Data/Materials/DA_NPS_Ricochet.DA_NPS_Ricochet"),
			TEXT("/NPS/Examples/Data/Spread/DA_NPS_FixedPattern.DA_NPS_FixedPattern"),
			TEXT("/NPS/Examples/Data/Spread/DA_NPS_RandomCone.DA_NPS_RandomCone"),
			TEXT("/NPS/Examples/Data/Spread/DA_NPS_StressBurst.DA_NPS_StressBurst")
		};

		for (const TCHAR* Path : AssetPaths)
		{
			ASSERT_THAT(IsNotNull(StaticLoadObject(UObject::StaticClass(), nullptr, Path)));
		}
	}

	TEST_METHOD(Examples_ProjectileAssets_CoverModesWindSpreadHitscanAndExplosives)
	{
		UBallisticsProjectileDataAsset* Server = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ServerAuth.DA_NPS_ServerAuth"));
		UBallisticsProjectileDataAsset* Predicted = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientPredicted.DA_NPS_ClientPredicted"));
		UBallisticsProjectileDataAsset* Client = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientAuth.DA_NPS_ClientAuth"));
		UBallisticsProjectileDataAsset* Wind = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Crosswind.DA_NPS_Crosswind"));
		UBallisticsProjectileDataAsset* Spread = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadRandom.DA_NPS_SpreadRandom"));
		UBallisticsProjectileDataAsset* Hitscan = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Hitscan.DA_NPS_Hitscan"));
		UBallisticsProjectileDataAsset* Stress = LoadObject<UBallisticsProjectileDataAsset>(
			nullptr, TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Stress.DA_NPS_Stress"));

		ASSERT_THAT(IsNotNull(Server));
		ASSERT_THAT(IsNotNull(Predicted));
		ASSERT_THAT(IsNotNull(Client));
		ASSERT_THAT(IsNotNull(Wind));
		ASSERT_THAT(IsNotNull(Spread));
		ASSERT_THAT(IsNotNull(Hitscan));
		ASSERT_THAT(IsNotNull(Stress));
		ASSERT_THAT(IsTrue(Server->AuthorityModeOverride == ELaunchAuthorityMode::ServerAuthoritative));
		ASSERT_THAT(IsTrue(Predicted->AuthorityModeOverride == ELaunchAuthorityMode::ClientPredicted));
		ASSERT_THAT(IsTrue(Client->AuthorityModeOverride == ELaunchAuthorityMode::ClientAuthoritative));
		ASSERT_THAT(IsTrue(!Wind->WindAcceleration.IsNearlyZero()));
		const float CrosswindTravelTime = 4500.f / Wind->InitialSpeed;
		const float CrosswindDisplacement =
			0.5f * Wind->WindAcceleration.Y * FMath::Square(CrosswindTravelTime);
		ASSERT_THAT(IsTrue(CrosswindDisplacement > 150.f));
		ASSERT_THAT(IsTrue(Spread->UsesSpread()));
		ASSERT_THAT(IsTrue(Hitscan->bUseHitscan));
		ASSERT_THAT(IsTrue(Stress->UsesSpread()));
		ASSERT_THAT(AreEqual(Stress->SpreadConfig->ProjectileCount, 32));

		static const TCHAR* ExplosiveProjectilePaths[] = {
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveImpact.DA_NPS_ExplosiveImpact"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveFuse.DA_NPS_ExplosiveFuse"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveRest.DA_NPS_ExplosiveRest"),
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveManual.DA_NPS_ExplosiveManual")
		};
		for (const TCHAR* Path : ExplosiveProjectilePaths)
		{
			UBallisticsProjectileDataAsset* Projectile =
				LoadObject<UBallisticsProjectileDataAsset>(nullptr, Path);
			ASSERT_THAT(IsNotNull(Projectile));
			ASSERT_THAT(IsTrue(Projectile->ResolveProjectileClass() == ANPSExampleProjectile::StaticClass()));
			ASSERT_THAT(IsTrue(Projectile->IsExplosive()));
		}
	}

	TEST_METHOD(Examples_FocusedMaps_LoadWithExpectedDirectors)
	{
		const TPair<const TCHAR*, ENPSExampleScenario> Maps[] = {
			{TEXT("/NPS/Examples/Maps/NPS_Showcase.NPS_Showcase"), ENPSExampleScenario::Showcase},
			{TEXT("/NPS/Examples/Maps/NPS_AuthorityModes.NPS_AuthorityModes"), ENPSExampleScenario::AuthorityModes},
			{TEXT("/NPS/Examples/Maps/NPS_Trajectories.NPS_Trajectories"), ENPSExampleScenario::Trajectories},
			{TEXT("/NPS/Examples/Maps/NPS_MaterialResponses.NPS_MaterialResponses"), ENPSExampleScenario::MaterialResponses},
			{TEXT("/NPS/Examples/Maps/NPS_MultiProjectile.NPS_MultiProjectile"), ENPSExampleScenario::MultiProjectile},
			{TEXT("/NPS/Examples/Maps/NPS_Explosives.NPS_Explosives"), ENPSExampleScenario::Explosives},
			{TEXT("/NPS/Examples/Maps/NPS_Cosmetics.NPS_Cosmetics"), ENPSExampleScenario::Cosmetics},
			{TEXT("/NPS/Examples/Maps/NPS_NetworkScale.NPS_NetworkScale"), ENPSExampleScenario::NetworkScale},
			{TEXT("/NPS/Examples/Maps/NPS_Performance.NPS_Performance"), ENPSExampleScenario::Performance}
		};

		for (const TPair<const TCHAR*, ENPSExampleScenario>& Map : Maps)
		{
			UWorld* World = LoadObject<UWorld>(nullptr, Map.Key);
			ASSERT_THAT(IsNotNull(World));
			ANPSExampleShowcase* Director = nullptr;
			ADirectionalLight* DirectionalLight = nullptr;
			ASkyAtmosphere* SkyAtmosphere = nullptr;
			ASkyLight* SkyLight = nullptr;
			AExponentialHeightFog* HeightFog = nullptr;
			if (World && World->PersistentLevel)
			{
				for (AActor* Actor : World->PersistentLevel->Actors)
				{
					if (ANPSExampleShowcase* Candidate = Cast<ANPSExampleShowcase>(Actor))
					{
						Director = Candidate;
					}
					DirectionalLight = DirectionalLight
						? DirectionalLight
						: Cast<ADirectionalLight>(Actor);
					SkyAtmosphere = SkyAtmosphere
						? SkyAtmosphere
						: Cast<ASkyAtmosphere>(Actor);
					SkyLight = SkyLight ? SkyLight : Cast<ASkyLight>(Actor);
					HeightFog = HeightFog
						? HeightFog
						: Cast<AExponentialHeightFog>(Actor);
				}
			}
			ASSERT_THAT(IsNotNull(Director));
			ASSERT_THAT(IsTrue(Director && Director->Scenario == Map.Value));
			ASSERT_THAT(IsNotNull(DirectionalLight));
			ASSERT_THAT(IsNotNull(SkyAtmosphere));
			ASSERT_THAT(IsNotNull(SkyLight));
			ASSERT_THAT(IsNotNull(HeightFog));
			ASSERT_THAT(IsTrue(
				SkyLight &&
				SkyLight->GetLightComponent()->Mobility == EComponentMobility::Movable &&
				SkyLight->GetLightComponent()->IsRealTimeCaptureEnabled()));
			ASSERT_THAT(IsTrue(
				HeightFog &&
				HeightFog->GetComponent()->FogDensity > 0.f));
		}
	}

	TEST_METHOD(Cosmetics_SequenceEndRejectsDelayedBegin)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager = Subsystem->GetCosmeticsManager();
			ASSERT_THAT(IsNotNull(Manager));

			FLaunchSequenceCommand End;
			End.Type = ELaunchSequenceCommandType::End;
			End.SequenceID = 45;
			End.LaunchEvent.PlaybackMask =
				static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
			Manager->ReceiveSequenceBatch({ End });

			const uint64 Before = Manager->GetProcessedEventCount();
			FLaunchSequenceCommand DelayedBegin;
			DelayedBegin.Type = ELaunchSequenceCommandType::Begin;
			DelayedBegin.SequenceID = 45;
			DelayedBegin.LaunchesPerSecond = 10.f;
			DelayedBegin.LaunchEvent.Type = EProjectileCosmeticEventType::Launch;
			DelayedBegin.LaunchEvent.PlaybackMask =
				static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
			Manager->ReceiveSequenceBatch({ DelayedBegin });

			ASSERT_THAT(AreEqual(Manager->GetActiveSequenceCount(), 0));
			ASSERT_THAT(IsTrue(Manager->GetProcessedEventCount() == Before));
		});
	}

	TEST_METHOD(Cosmetics_SequenceWithoutStopFailsClosed)
	{
		TestCommandBuilder.Do([this]()
		{
			UProjectileBallisticsSubsystem* Subsystem =
				Spawner->GetWorld().GetSubsystem<UProjectileBallisticsSubsystem>();
			ASSERT_THAT(IsNotNull(Subsystem));
			UProjectileCosmeticsManager* Manager = Subsystem->GetCosmeticsManager();
			ASSERT_THAT(IsNotNull(Manager));

			FLaunchSequenceCommand Begin;
			Begin.Type = ELaunchSequenceCommandType::Begin;
			Begin.SequenceID = 46;
			Begin.LaunchesPerSecond = 10.f;
			Begin.LaunchEvent.Type = EProjectileCosmeticEventType::Launch;
			Manager->ReceiveSequenceBatch({ Begin });
			ASSERT_THAT(AreEqual(Manager->GetActiveSequenceCount(), 1));

			Manager->TickActiveSequencesForTesting(1.01f);
			ASSERT_THAT(AreEqual(Manager->GetActiveSequenceCount(), 0));
		});
	}

	TEST_METHOD(Examples_CatalogCoversMapsStationsAndLinkedAssets)
	{
		UNPSExampleCatalog* Catalog = UNPSExampleCatalog::LoadDefaultCatalog();
		ASSERT_THAT(IsNotNull(Catalog));
		ASSERT_THAT(IsTrue(Catalog && Catalog->Maps.Num() == 10));
		ASSERT_THAT(IsTrue(Catalog && Catalog->Stations.Num() == 22));
		if (!Catalog)
		{
			return;
		}

		TSet<FName> MapIds;
		for (const FNPSExampleMapEntry& Entry : Catalog->Maps)
		{
			ASSERT_THAT(IsFalse(Entry.MapId.IsNone()));
			ASSERT_THAT(IsFalse(Entry.Title.IsEmpty()));
			ASSERT_THAT(IsFalse(Entry.Summary.IsEmpty()));
			ASSERT_THAT(IsFalse(Entry.Map.IsNull()));
			ASSERT_THAT(IsNotNull(Entry.Map.LoadSynchronous()));
			ASSERT_THAT(IsFalse(MapIds.Contains(Entry.MapId)));
			MapIds.Add(Entry.MapId);
		}

		TSet<FName> StationIds;
		for (const FNPSExampleStationEntry& Entry : Catalog->Stations)
		{
			ASSERT_THAT(IsFalse(Entry.ExampleId.IsNone()));
			ASSERT_THAT(IsFalse(Entry.Title.IsEmpty()));
			ASSERT_THAT(IsFalse(Entry.Summary.IsEmpty()));
			ASSERT_THAT(IsFalse(Entry.ExpectedResult.IsEmpty()));
			ASSERT_THAT(IsNotNull(Entry.ProjectileData.LoadSynchronous()));
			ASSERT_THAT(IsFalse(StationIds.Contains(Entry.ExampleId)));
			StationIds.Add(Entry.ExampleId);
			ASSERT_THAT(IsTrue(
				Catalog->FindStation(Entry.ExampleId) == &Entry));
			ASSERT_THAT(IsTrue(
				Catalog->FindStationByTitle(Entry.Title.ToString()) == &Entry));
		}
	}

	TEST_METHOD(Examples_GuideHUDExposesDeterministicPanelState)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSExampleGuideHUD& HUD = Spawner->SpawnActor<ANPSExampleGuideHUD>();
			ASSERT_THAT(IsTrue(HUD.IsGuideVisible()));
			ASSERT_THAT(IsFalse(HUD.IsMapBrowserVisible()));
			HUD.ToggleGuide();
			HUD.ToggleMapBrowser();
			ASSERT_THAT(IsFalse(HUD.IsGuideVisible()));
			ASSERT_THAT(IsTrue(HUD.IsMapBrowserVisible()));
			HUD.SelectNextMap();
			ASSERT_THAT(AreEqual(HUD.GetSelectedMapIndex(), 1));
			HUD.SelectPreviousMap();
			ASSERT_THAT(AreEqual(HUD.GetSelectedMapIndex(), 0));
			HUD.ClosePanels();
			ASSERT_THAT(IsFalse(HUD.IsMapBrowserVisible()));
		});
	}

	TEST_METHOD(Examples_Showcase_SpawnsCompleteStationCatalog)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSExampleShowcase& Showcase =
				Spawner->SpawnActor<ANPSExampleShowcase>();
			ASSERT_THAT(AreEqual(Showcase.Launchers.Num(), 22));
			ASSERT_THAT(AreEqual(Showcase.Targets.Num(), 22));
			ASSERT_THAT(IsNotNull(Showcase.GetRootComponent()));
			ASSERT_THAT(IsTrue(Showcase.Launchers.ContainsByPredicate(
				[](const ANPSExampleLauncher* Launcher)
				{
					return Launcher &&
						Launcher->bDemonstrateSustainedSequence;
				})));
			ANPSExampleLauncher* CrosswindLauncher = nullptr;
			ANPSExampleLauncher* NetworkScaleLauncher = nullptr;
			ANPSExampleLauncher* PerformanceLauncher = nullptr;
			for (int32 Index = 0; Index < Showcase.Launchers.Num(); ++Index)
			{
				ANPSExampleLauncher* Launcher = Showcase.Launchers[Index];
				ASSERT_THAT(IsNotNull(Launcher));
				ASSERT_THAT(IsTrue(Launcher && !Launcher->ExampleId.IsNone()));
				ASSERT_THAT(IsNotNull(Launcher ? Launcher->GuideText : nullptr));
				for (int32 OtherIndex = Index + 1; OtherIndex < Showcase.Launchers.Num(); ++OtherIndex)
				{
					ASSERT_THAT(IsFalse(FMath::IsNearlyEqual(
						Launcher->GetActorLocation().Y,
						Showcase.Launchers[OtherIndex]->GetActorLocation().Y)));
				}
				if (Launcher->StationLabel == TEXT("COSMETICS BEYOND 100 CM NET CULL"))
				{
					NetworkScaleLauncher = Launcher;
				}
				else if (Launcher->StationLabel == TEXT("CROSSWIND DRIFT"))
				{
					CrosswindLauncher = Launcher;
				}
				else if (Launcher->StationLabel == TEXT("POOLED 32 PROJECTILE SPREAD"))
				{
					PerformanceLauncher = Launcher;
				}
			}
			ASSERT_THAT(IsNotNull(CrosswindLauncher));
			ASSERT_THAT(IsNotNull(NetworkScaleLauncher));
			ASSERT_THAT(IsNotNull(PerformanceLauncher));
			ASSERT_THAT(IsTrue(
				PerformanceLauncher &&
					PerformanceLauncher->ProjectilesPerCycle == 1 &&
					PerformanceLauncher->ProjectileData &&
					PerformanceLauncher->ProjectileData->SpreadConfig &&
					PerformanceLauncher->ProjectileData->SpreadConfig->ProjectileCount == 32));

			PerformanceLauncher->LaunchExample();
			ASSERT_THAT(AreEqual(PerformanceLauncher->LaunchCount, 1));
			int32 PerformanceProjectileCount = 0;
			float MinimumDirectionY = 1.f;
			float MaximumDirectionY = -1.f;
			const float MinimumForwardDot =
				FMath::Cos(FMath::DegreesToRadians(2.5f));
			for (TActorIterator<ANPSExampleProjectile> It(&Spawner->GetWorld()); It; ++It)
			{
				if (It->Ballistics->GetSourceProjectileData() !=
					PerformanceLauncher->ProjectileData)
				{
					continue;
				}
				++PerformanceProjectileCount;
				const FVector Direction = It->Ballistics->GetInitialConditions().Direction;
				MinimumDirectionY = FMath::Min(MinimumDirectionY, Direction.Y);
				MaximumDirectionY = FMath::Max(MaximumDirectionY, Direction.Y);
				ASSERT_THAT(IsTrue(
					FVector::DotProduct(
						PerformanceLauncher->GetLaunchDirection(), Direction) >=
						MinimumForwardDot - UE_KINDA_SMALL_NUMBER));
			}
			ASSERT_THAT(AreEqual(PerformanceProjectileCount, 32));
			ASSERT_THAT(IsTrue(MaximumDirectionY - MinimumDirectionY > 0.01f));

			CrosswindLauncher->LaunchExample();
			UProjectileBallisticsComponent* CrosswindBallistics = nullptr;
			for (TActorIterator<ANPSExampleProjectile> It(&Spawner->GetWorld()); It; ++It)
			{
				if (It->Ballistics->GetSourceProjectileData() ==
					CrosswindLauncher->ProjectileData)
				{
					CrosswindBallistics = It->Ballistics;
					break;
				}
			}
			ASSERT_THAT(IsNotNull(CrosswindBallistics));
			const float CrosswindTravelTime =
				4500.f / CrosswindLauncher->ProjectileData->InitialSpeed;
			const FProjectileSnapshot CrosswindSnapshot =
				CrosswindBallistics->EvaluateAtTime(CrosswindTravelTime);
			ASSERT_THAT(IsTrue(
				CrosswindSnapshot.Position.Y -
					CrosswindBallistics->GetInitialConditions().Origin.Y > 150.f));
		});
	}

	TEST_METHOD(Playground_ProjectileMatrix_CoversEveryRuntimeCombination)
	{
		struct FAuthorityDefinition
		{
			ELaunchAuthorityMode Mode;
			const TCHAR* Token;
		};
		const FAuthorityDefinition AuthorityDefinitions[] = {
			{ELaunchAuthorityMode::ServerAuthoritative, TEXT("Server")},
			{ELaunchAuthorityMode::ClientPredicted, TEXT("Predicted")},
			{ELaunchAuthorityMode::ClientAuthoritative, TEXT("Client")}
		};
		const TCHAR* GunTokens[] = {
			TEXT("Rifle"),
			TEXT("Pistol"),
			TEXT("Launcher")
		};

		int32 LoadedAssetCount = 0;
		for (const TCHAR* GunToken : GunTokens)
		{
			for (const FAuthorityDefinition& Authority : AuthorityDefinitions)
			{
				for (int32 ExplosiveIndex = 0; ExplosiveIndex < 2;
					++ExplosiveIndex)
				{
					for (int32 WindIndex = 0; WindIndex < 2; ++WindIndex)
					{
						const bool bExplosive = ExplosiveIndex != 0;
						const bool bWind = WindIndex != 0;
						const FString AssetName = FString::Printf(
							TEXT("DA_NPS_Playground_%s_%s_%s_%s"),
							GunToken,
							Authority.Token,
							bExplosive ? TEXT("Explosive") : TEXT("Standard"),
							bWind ? TEXT("Wind") : TEXT("Calm"));
						const FString AssetPath = FString::Printf(
							TEXT("/NPS/Examples/Data/Playground/%s/%s.%s"),
							GunToken,
							*AssetName,
							*AssetName);
						UBallisticsProjectileDataAsset* Projectile =
							LoadObject<UBallisticsProjectileDataAsset>(
								nullptr,
								*AssetPath);

						ASSERT_THAT(IsNotNull(Projectile));
						ASSERT_THAT(IsTrue(
							Projectile &&
								Projectile->AuthorityModeOverride ==
									Authority.Mode));
						ASSERT_THAT(IsTrue(
							Projectile &&
								Projectile->IsExplosive() == bExplosive));
						ASSERT_THAT(IsTrue(
							Projectile &&
								(!Projectile->WindAcceleration.IsNearlyZero()) ==
									bWind));
						ASSERT_THAT(IsTrue(
							Projectile &&
								Projectile->ResolveProjectileClass() ==
									ANPSExampleProjectile::StaticClass()));
						++LoadedAssetCount;
					}
				}
			}
		}

		ASSERT_THAT(AreEqual(LoadedAssetCount, 36));
	}

	TEST_METHOD(Playground_GunChildren_LoadEpicMeshesAndDisableActorTick)
	{
		const TPair<UClass*, ENPSPlaygroundGunType> GunClasses[] = {
			{
				ANPSPlaygroundRifleGun::StaticClass(),
				ENPSPlaygroundGunType::Rifle
			},
			{
				ANPSPlaygroundPistolGun::StaticClass(),
				ENPSPlaygroundGunType::Pistol
			},
			{
				ANPSPlaygroundLauncherGun::StaticClass(),
				ENPSPlaygroundGunType::Launcher
			}
		};

		for (const TPair<UClass*, ENPSPlaygroundGunType>& Definition :
			GunClasses)
		{
			ANPSPlaygroundGun* Gun =
				Cast<ANPSPlaygroundGun>(Definition.Key->GetDefaultObject());
			ASSERT_THAT(IsNotNull(Gun));
			ASSERT_THAT(IsTrue(Gun && Gun->GetGunType() == Definition.Value));
			ASSERT_THAT(IsFalse(
				Gun && Gun->PrimaryActorTick.bCanEverTick));
			ASSERT_THAT(IsNotNull(Gun ? Gun->GetLauncher() : nullptr));
		}

		static const TCHAR* MeshPaths[] = {
			TEXT("/NPS/FirstPerson/Weapons/Rifle/Meshes/"
				"SKM_Rifle.SKM_Rifle"),
			TEXT("/NPS/FirstPerson/Weapons/Pistol/Meshes/"
				"SKM_Pistol.SKM_Pistol"),
			TEXT("/NPS/FirstPerson/Weapons/GrenadeLauncher/Meshes/"
				"SKM_GrenadeLauncher.SKM_GrenadeLauncher")
		};
		for (const TCHAR* MeshPath : MeshPaths)
		{
			USkeletalMesh* Mesh =
				LoadObject<USkeletalMesh>(nullptr, MeshPath);
			ASSERT_THAT(IsNotNull(Mesh));
			ASSERT_THAT(IsTrue(Mesh && !Mesh->GetMaterials().IsEmpty()));
			if (!Mesh)
			{
				continue;
			}
			for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
			{
				ASSERT_THAT(IsNotNull(Material.MaterialInterface));
				ASSERT_THAT(IsTrue(
					Material.MaterialInterface &&
						Material.MaterialInterface->GetPathName().StartsWith(
							TEXT("/NPS/FirstPerson/Weapons/"))));
			}
		}
	}

	TEST_METHOD(Playground_CharacterSwitchesEveryRuntimeConfiguration)
	{
		TestCommandBuilder.Do([this]()
		{
			ANPSPlaygroundCharacter& Character =
				Spawner->SpawnActor<ANPSPlaygroundCharacter>();
			ASSERT_THAT(IsFalse(Character.PrimaryActorTick.bCanEverTick));

			const ELaunchAuthorityMode Modes[] = {
				ELaunchAuthorityMode::ServerAuthoritative,
				ELaunchAuthorityMode::ClientPredicted,
				ELaunchAuthorityMode::ClientAuthoritative
			};
			int32 ConfigurationCount = 0;
			for (int32 GunIndex = 0; GunIndex < 3; ++GunIndex)
			{
				for (ELaunchAuthorityMode Mode : Modes)
				{
					for (int32 ExplosiveIndex = 0; ExplosiveIndex < 2;
						++ExplosiveIndex)
					{
						for (int32 WindIndex = 0; WindIndex < 2;
							++WindIndex)
						{
							const bool bExplosive = ExplosiveIndex != 0;
							const bool bWind = WindIndex != 0;
							Character.SetPlaygroundConfiguration(
								GunIndex,
								Mode,
								bExplosive,
								bWind);
							ANPSPlaygroundGun* Gun =
								Character.GetCurrentGun();
							UBallisticsProjectileDataAsset* Projectile =
								Gun ? Gun->GetActiveProjectileData() : nullptr;

							ASSERT_THAT(IsNotNull(Gun));
							ASSERT_THAT(IsNotNull(Projectile));
							const FVector CameraForward =
								Character.GetFirstPersonCamera()->GetForwardVector();
							USkeletalMeshComponent* FirstPersonMesh =
								Gun
									? Gun->GetFirstPersonMeshComponent()
									: nullptr;
							const FVector MuzzleForward =
								FirstPersonMesh
									? FirstPersonMesh->GetSocketTransform(
											TEXT("Muzzle"),
											RTS_World).GetUnitAxis(EAxis::X)
									: FVector::ZeroVector;
							ASSERT_THAT(IsTrue(
								FirstPersonMesh &&
									FirstPersonMesh->DoesSocketExist(
										TEXT("Muzzle"))));
							ASSERT_THAT(IsTrue(
								FirstPersonMesh &&
									FirstPersonMesh->DoesSocketExist(
										TEXT("ShellEject"))));
							ASSERT_THAT(IsTrue(
								Gun &&
									Gun->GetLauncher()->
										LaunchEffectSourceComponent.GetComponent(
											Gun) == FirstPersonMesh));
							ASSERT_THAT(IsTrue(
								FVector::DotProduct(
									CameraForward,
									MuzzleForward) > 0.99f));
							ASSERT_THAT(IsTrue(
								Projectile &&
									Projectile->AuthorityModeOverride == Mode));
							ASSERT_THAT(IsTrue(
								Projectile &&
									Projectile->IsExplosive() == bExplosive));
							ASSERT_THAT(IsTrue(
								Projectile &&
									(!Projectile->WindAcceleration.IsNearlyZero()) ==
										bWind));
							++ConfigurationCount;
						}
					}
				}
			}
			ASSERT_THAT(AreEqual(ConfigurationCount, 36));
		});
	}

	TEST_METHOD(Playground_MapLoadsWithGameModeArenaStartsAndLighting)
	{
		UWorld* World = LoadObject<UWorld>(
			nullptr,
			TEXT("/NPS/Examples/Maps/NPS_Playground.NPS_Playground"));
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(
			World &&
				World->GetWorldSettings()->DefaultGameMode ==
					ANPSPlaygroundGameMode::StaticClass()));

		ANPSPlaygroundArena* Arena = nullptr;
		ADirectionalLight* DirectionalLight = nullptr;
		ASkyAtmosphere* SkyAtmosphere = nullptr;
		ASkyLight* SkyLight = nullptr;
		AExponentialHeightFog* HeightFog = nullptr;
		int32 PlayerStartCount = 0;
		if (World && World->PersistentLevel)
		{
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				Arena = Arena ? Arena : Cast<ANPSPlaygroundArena>(Actor);
				DirectionalLight = DirectionalLight
					? DirectionalLight
					: Cast<ADirectionalLight>(Actor);
				SkyAtmosphere = SkyAtmosphere
					? SkyAtmosphere
					: Cast<ASkyAtmosphere>(Actor);
				SkyLight = SkyLight ? SkyLight : Cast<ASkyLight>(Actor);
				HeightFog = HeightFog
					? HeightFog
					: Cast<AExponentialHeightFog>(Actor);
				PlayerStartCount += Cast<APlayerStart>(Actor) ? 1 : 0;
			}
		}

		ASSERT_THAT(IsNotNull(Arena));
		UStaticMeshComponent* Floor =
			Arena ? Arena->GetFloorMeshComponent() : nullptr;
		ASSERT_THAT(IsNotNull(Floor));
		ASSERT_THAT(IsTrue(
			Floor && Floor->CreationMethod == EComponentCreationMethod::Native));
		ASSERT_THAT(IsTrue(Floor && Floor->IsNameStableForNetworking()));
		ASSERT_THAT(AreEqual(PlayerStartCount, 4));
		ASSERT_THAT(IsNotNull(DirectionalLight));
		ASSERT_THAT(IsNotNull(SkyAtmosphere));
		ASSERT_THAT(IsNotNull(SkyLight));
		ASSERT_THAT(IsNotNull(HeightFog));
		ASSERT_THAT(IsTrue(
			HeightFog && HeightFog->GetComponent()->FogDensity > 0.f));
	}
};

#endif
