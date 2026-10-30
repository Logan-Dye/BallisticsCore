// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "EngineUtils.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "GameFramework/PlayerController.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsSpreadDataAsset.h"
#include "NPSTestEnvironment.h"
#include "NPSNetworkBackend.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileNetProxyComponent.h"

NETWORK_TEST_CLASS(FNPSAuthorityModeTests, "NPS.Network.Authority")
{
	struct FNPSNetworkState : public FBasePIENetworkComponentState
	{
		ANPSTestLauncher* Launcher = nullptr;
		ANPSTestTarget* Target = nullptr;
		ANPSTestProjectile* Projectile = nullptr;
		ANPSTestNetProxyActor* ProxyActor = nullptr;
		ANPSTestFlyByListener* FlyByListener = nullptr;
		uint64 ClientProxyTickBaseline = 0;
		double SettleUntil = 0.0;
	};

	FPIENetworkComponent<FNPSNetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };
	FPacketSimulationSettings BadNetwork;

	BEFORE_EACH()
	{
		// CQTest's transient untitled PIE worlds are intentionally not stable
		// NetGUID assets. Ignore only those known harness warnings; all NPS
		TestRunner->AddExpectedMessagePlain(
			TEXT("FNetGUIDCache::SupportsObject: Level /Temp/UEDPIE_"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);
		TestRunner->AddExpectedMessagePlain(
			TEXT("RegisterNetGUID_Client: Guid with pathname"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);
		TestRunner->AddExpectedMessagePlain(
			TEXT("FNetGUIDCache::SupportsObject: BallisticsLaunchCosmeticProfile /Temp/UEDPIE_"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);

		BadNetwork.PktLagMin = 100;
		BadNetwork.PktLagMax = 200;
		BadNetwork.PktLoss = 5;
		BadNetwork.PktIncomingLagMin = 100;
		BadNetwork.PktIncomingLagMax = 200;
		BadNetwork.PktIncomingLoss = 5;

		FNetworkComponentBuilder<FNPSNetworkState>()
			.WithClients(1)
			.WithGameInstanceClass(UGameInstance::StaticClass())
			.WithGameMode(AGameModeBase::StaticClass())
			.WithPacketSimulationSettings(&BadNetwork)
			.Build(Network);
		Network.ThenServer(TEXT("Verify server replication backend"),
			[this](FNPSNetworkState& State)
			{
				ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			})
			.ThenClients(TEXT("Verify client replication backend"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
				});
	}

	void PrepareLane(ELaunchAuthorityMode Mode)
	{
		Network
			.SpawnAndReplicate<ANPSTestTarget, &FNPSNetworkState::Target>(
				[](ANPSTestTarget& Target)
				{
					Target.SetActorLocation(FVector(3000.f, 0.f, 100.f));
				})
			.SpawnAndReplicate<ANPSTestFlyByListener, &FNPSNetworkState::FlyByListener>(
				[](ANPSTestFlyByListener& Listener)
				{
					Listener.SetActorLocation(FVector(1500.f, 100.f, 100.f));
				})
			.SpawnAndReplicate<ANPSTestLauncher, &FNPSNetworkState::Launcher>(
				[Mode](ANPSTestLauncher& Launcher)
				{
					Launcher.SetActorLocation(FVector(0.f, 0.f, 100.f));
					Launcher.ProjectileData->AuthorityModeOverride = Mode;
					Launcher.ProjectileData->InitialSpeed = 50000.f;
					Launcher.ProjectileData->DragCoefficient = 0.f;
					Launcher.ProjectileData->FlyBySound.Reset();
					Launcher.Launcher->LaunchCosmeticProfile = nullptr;
					Launcher.Launcher->bEnableCosmeticRouting = false;
				})
			.ThenServer(TEXT("Assign launcher to remote client"), [this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.ClientConnections.Num(), 1));
					APlayerController* ClientController = State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(ClientController));
					State.Launcher->SetOwner(ClientController);
					State.Launcher->ForceNetUpdate();
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					Subsystem->RegisterFlyByListener(State.FlyByListener);
				})
			.UntilClients(TEXT("Wait for client lane readiness"), [](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Target &&
						State.FlyByListener &&
						State.Launcher->HasLocalNetOwner() &&
						State.Target->GetActorLocation().Equals(
							FVector(3000.f, 0.f, 100.f), 1.f) &&
						State.FlyByListener->GetActorLocation().Equals(
							FVector(1500.f, 100.f, 100.f), 1.f);
				})
			.ThenClient(0, [this, Mode](FNPSNetworkState& State)
				{
					State.Launcher->ProjectileData->AuthorityModeOverride = Mode;
					State.Launcher->ProjectileData->InitialSpeed = 50000.f;
					State.Launcher->ProjectileData->DragCoefficient = 0.f;
					State.Launcher->ProjectileData->FlyBySound.Reset();
					State.Launcher->Launcher->ProjectileDataAsset =
						State.Launcher->ProjectileData;
					State.Launcher->Launcher->LaunchCosmeticProfile = nullptr;
					State.Launcher->Launcher->bEnableCosmeticRouting = false;
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					Subsystem->RegisterFlyByListener(State.FlyByListener);
				});
	}

	void LaunchAndRequireAuthoritativeDamage()
	{
		Network
			.ThenServer(TEXT("Grant one launch credit"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(1, 5.f);
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for launch credit policy"), [](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->Launch(
						FVector(0.f, 0.f, 100.f),
						FVector::ForwardVector);
				})
			.UntilServer(TEXT("Wait for authoritative target damage"), [](FNPSNetworkState& State)
				{
					return State.Target && State.Target->HitCount == 1;
				}, FTimespan::FromSeconds(10))
			.ThenServer(TEXT("Verify cosmetic-free authoritative events"), [this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->AuthoritativeImpactCount, 1));
					ASSERT_THAT(AreEqual(State.Launcher->AuthoritativeFlyByCount, 1));
				})
			.UntilClients(TEXT("Wait for replicated damage result"), [](FNPSNetworkState& State)
				{
					return State.Target && State.Target->HitCount == 1 &&
						State.Target->LastKineticEnergy > 0.f;
				}, FTimespan::FromSeconds(10));
	}

	void SendDirectRemoteLaunch(
		FNPSNetworkState& State,
		ELaunchAuthorityMode Mode,
		int32 ProjectileID,
		int32 LaunchRequestID)
	{
		const FVector Origin(0.f, 1000.f, 100.f);
		const FVector Direction = FVector::UpVector;
		const int32 RandomSeed = 1000 + ProjectileID;
		const float ClientTimestamp = State.World->GetTimeSeconds();
		switch (Mode)
		{
		case ELaunchAuthorityMode::ServerAuthoritative:
			State.Launcher->Launcher->ServerRequestLaunch(
				Origin,
				Direction,
				RandomSeed,
				ProjectileID,
				LaunchRequestID,
				0,
				1);
			break;
		case ELaunchAuthorityMode::ClientPredicted:
			State.Launcher->Launcher->ServerNotifyLaunch(
				Origin,
				Direction,
				RandomSeed,
				ClientTimestamp,
				ProjectileID,
				LaunchRequestID,
				0,
				1);
			break;
		case ELaunchAuthorityMode::ClientAuthoritative:
			State.Launcher->Launcher->ServerRegisterClientAuthoritativeLaunch(
				ProjectileID,
				Origin,
				Direction,
				RandomSeed,
				ClientTimestamp,
				LaunchRequestID,
				0,
				1);
			break;
		}
	}

	void RequireSingleCreditForDirectRequests(ELaunchAuthorityMode Mode)
	{
		PrepareLane(Mode);
		Network
			.ThenServer(TEXT("Enable one-credit admission"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(1, 5.f);
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for one-credit admission policy"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.ThenClient(0, [this, Mode](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(State, Mode, 100, 100);
					SendDirectRemoteLaunch(State, Mode, 101, 101);
				})
			.UntilServer(TEXT("Wait for one accepted direct request"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 1;
				},
				FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Verify one credit admits one request"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 1));
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						0));
				});
	}

	void LaunchWithSpoofedClientBallisticsAndRequireCanonicalServerData(
		ELaunchAuthorityMode Mode)
	{
		PrepareLane(Mode);
		Network
			.ThenClient(0, [](FNPSNetworkState& State)
				{
					State.Launcher->ProjectileData->InitialSpeed = 150000.f;
					State.Launcher->ProjectileData->Mass = 9500.f;
					State.Launcher->ProjectileData->Diameter = 20.f;
					State.Launcher->ProjectileData->DragCoefficient = 5.f;
					State.Launcher->ProjectileData->WindAcceleration =
						FVector(0.f, 50000.f, 50000.f);
					State.Launcher->Launcher->Launch(
						FVector(0.f, 0.f, 100.f),
						FVector::ForwardVector);
				})
			.UntilServer(TEXT("Wait for canonical server projectile damage"),
				[](FNPSNetworkState& State)
				{
					return State.Target && State.Target->HitCount == 1;
				},
				FTimespan::FromSeconds(10))
			.ThenServer(TEXT("Verify server-owned ballistic energy"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(IsTrue(State.Target->LastKineticEnergy > 1000.f));
					ASSERT_THAT(IsTrue(State.Target->LastKineticEnergy < 2000.f));
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 1));
				});
	}

	TEST_METHOD(ServerAuthoritative_ClientLaunch_DamagesExactlyOnce)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		LaunchAndRequireAuthoritativeDamage();
	}

	TEST_METHOD(ServerAuthoritative_OneCreditRejectsSecondDirectRequest)
	{
		RequireSingleCreditForDirectRequests(
			ELaunchAuthorityMode::ServerAuthoritative);
	}

	TEST_METHOD(ServerAuthoritative_IgnoresSpoofedClientBallisticProperties)
	{
		LaunchWithSpoofedClientBallisticsAndRequireCanonicalServerData(
			ELaunchAuthorityMode::ServerAuthoritative);
	}

	TEST_METHOD(ServerAuthoritative_NetProxyReplicatesTransformWithoutReplicatedMovement)
	{
		const FVector InitialLocation(0.f, 1500.f, 250.f);
		const FVector UpdatedLocation(1600.f, 1500.f, 400.f);
		const FVector ContinuousLocation(2400.f, 1500.f, 550.f);

		Network
			.SpawnAndReplicate<ANPSTestNetProxyActor, &FNPSNetworkState::ProxyActor>(
				[InitialLocation](ANPSTestNetProxyActor& ProxyActor)
				{
					ProxyActor.SetActorLocation(InitialLocation);
				})
			.UntilClients(TEXT("Wait for proxy projectile"), [](FNPSNetworkState& State)
				{
					return State.ProxyActor != nullptr;
				})
			.ThenServer(TEXT("Move authoritative projectile"), [UpdatedLocation](FNPSNetworkState& State)
				{
					State.ProxyActor->SetActorLocation(UpdatedLocation);
					State.ProxyActor->NetProxy->StartReplicatingTransform();
				})
			.UntilClients(TEXT("Wait for compact transform replication"),
				[UpdatedLocation](FNPSNetworkState& State)
				{
					return State.ProxyActor &&
						State.ProxyActor->GetActorLocation().Equals(
							UpdatedLocation, 10.f);
				},
				FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Continue authoritative projectile"), [ContinuousLocation](FNPSNetworkState& State)
				{
					State.ProxyActor->SetActorLocation(ContinuousLocation);
				})
			.UntilServer(TEXT("Wait for server proxy publication"),
				[ContinuousLocation](FNPSNetworkState& State)
				{
					return State.ProxyActor &&
						State.ProxyActor->NetProxy->GetPublishedLocationForTests().Equals(
							ContinuousLocation,
							1.f);
				},
				FTimespan::FromSeconds(5))
			.UntilClients(TEXT("Wait for replicated proxy target"),
				[ContinuousLocation](FNPSNetworkState& State)
				{
					return State.ProxyActor &&
						State.ProxyActor->NetProxy->HasReceivedTargetForTests() &&
						State.ProxyActor->NetProxy->GetTargetLocationForTests().Equals(
							ContinuousLocation,
							1.f);
				},
				FTimespan::FromSeconds(5))
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					ASSERT_THAT(IsTrue(State.ProxyActor->NetProxy->IsRegistered()));
					ASSERT_THAT(IsTrue(State.ProxyActor->NetProxy->IsActive()));
					ASSERT_THAT(IsTrue(State.ProxyActor->NetProxy->IsComponentTickEnabled()));
					ASSERT_THAT(IsFalse(State.ProxyActor->HasAuthority()));
					ASSERT_THAT(IsFalse(State.ProxyActor->IsReplicatingMovement()));
					ASSERT_THAT(IsFalse(
						State.ProxyActor->Ballistics->IsComponentTickEnabled()));
					ASSERT_THAT(IsFalse(
						State.ProxyActor->Ballistics
							->WasSimulationRegisteredForTests()));
					const UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					ASSERT_THAT(AreEqual(0, Subsystem->GetActiveProjectileCount()));
					ASSERT_THAT(IsTrue(
						State.ProxyActor->NetProxy->GetTickInvocationCountForTests() > 0));
					ASSERT_THAT(IsTrue(
						State.ProxyActor->NetProxy->PrimaryComponentTick.IsTickFunctionRegistered()));
					State.ClientProxyTickBaseline =
						State.ProxyActor->NetProxy->GetTickInvocationCountForTests();
				})
			.UntilClients(TEXT("Wait for client proxy interpolation ticks"),
				[ContinuousLocation](FNPSNetworkState& State)
				{
					return State.ProxyActor &&
						State.ProxyActor->NetProxy->GetTickInvocationCountForTests() >
							State.ClientProxyTickBaseline &&
						State.ProxyActor->GetActorLocation().Equals(
							ContinuousLocation,
							10.f);
				},
				FTimespan::FromSeconds(5));
	}

	TEST_METHOD(ClientPredicted_ClientLaunch_DamagesExactlyOnce)
	{
		PrepareLane(ELaunchAuthorityMode::ClientPredicted);
		LaunchAndRequireAuthoritativeDamage();
	}

	TEST_METHOD(ClientPredicted_OneCreditRejectsSecondDirectRequest)
	{
		RequireSingleCreditForDirectRequests(
			ELaunchAuthorityMode::ClientPredicted);
	}

	TEST_METHOD(ClientPredicted_IgnoresSpoofedClientBallisticProperties)
	{
		LaunchWithSpoofedClientBallisticsAndRequireCanonicalServerData(
			ELaunchAuthorityMode::ClientPredicted);
	}

	TEST_METHOD(ServerLaunchValidatorCanRejectProjectSpecificPolicy)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Reject launch through project validator"),
				[](FNPSNetworkState& State)
				{
					State.Launcher->bAllowValidatedLaunches = false;
				})
			.ThenClient(0, [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->Launch(
						FVector(0.f, 0.f, 100.f),
						FVector::ForwardVector);
				})
			.UntilServer(TEXT("Wait for server to observe the rejected request"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 1;
				},
				FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Start rejected launch observation window"),
				[](FNPSNetworkState& State)
				{
					State.SettleUntil = State.World->GetTimeSeconds() + 0.75;
				})
			.UntilServer(TEXT("Wait for rejected launch observation window"),
				[](FNPSNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				},
				FTimespan::FromSeconds(2))
			.ThenServer(TEXT("Verify rejected launch produced no damage"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 1));
					ASSERT_THAT(AreEqual(State.Target->HitCount, 0));
				});
	}

	TEST_METHOD(RejectedLaunchDoesNotConsumeCredit)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Grant credit and reject policy"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(1, 5.f);
					State.Launcher->bAllowValidatedLaunches = false;
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for credit policy"), [](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						200,
						200);
				})
			.UntilServer(TEXT("Wait for policy rejection"), [](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount == 1;
				})
			.ThenServer(TEXT("Allow next request"), [this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						1));
					State.Launcher->bAllowValidatedLaunches = true;
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						201,
						201);
				})
			.UntilServer(TEXT("Wait for accepted retry"), [](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount == 2;
				})
			.ThenServer(TEXT("Verify accepted retry consumed credit"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						0));
				});
	}

	TEST_METHOD(DuplicateRequestConsumesAtMostOneCredit)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Grant duplicate-request credits"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(2, 5.f);
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for duplicate-request policy"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						300,
						300);
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						300,
						300);
				})
			.UntilServer(TEXT("Wait for duplicate evaluation"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 1;
				})
			.ThenServer(TEXT("Verify duplicate consumed once"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 1));
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						1));
				});
	}

	TEST_METHOD(ExpiredCreditRejectsBeforeProjectValidation)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Grant short-lived credit"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(1, 0.05f);
					State.Launcher->ForceNetUpdate();
					State.SettleUntil = State.World->GetTimeSeconds() + 0.2;
				})
			.UntilClients(TEXT("Wait for short-lived credit policy"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.UntilServer(TEXT("Wait for credit expiry"), [](FNPSNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						400,
						400);
				})
			.ThenServer(TEXT("Start expiry observation"), [](FNPSNetworkState& State)
				{
					State.SettleUntil = State.World->GetTimeSeconds() + 0.5;
				})
			.UntilServer(TEXT("Wait for expired request processing"),
				[](FNPSNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				})
			.ThenServer(TEXT("Verify expiry rejection"), [this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 0));
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						0));
				});
	}

	TEST_METHOD(CadenceAllowsThreeRequestBurstAndRejectsFourth)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Configure launch cadence"), [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->MinLaunchInterval = 10.f;
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for launch cadence"), [](FNPSNetworkState& State)
				{
					return State.Launcher &&
						FMath::IsNearlyEqual(
							State.Launcher->Launcher->MinLaunchInterval,
							10.f);
				})
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					for (int32 Index = 0; Index < 4; ++Index)
					{
						SendDirectRemoteLaunch(
							State,
							ELaunchAuthorityMode::ServerAuthoritative,
							500 + Index,
							500 + Index);
					}
				})
			.UntilServer(TEXT("Wait for cadence burst"), [](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 3;
				})
			.ThenServer(TEXT("Verify fourth request was throttled"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 3));
				});
	}

	TEST_METHOD(SpreadConsumesOneCreditForAllMembers)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenServer(TEXT("Configure four-projectile spread"), [](FNPSNetworkState& State)
				{
					UBallisticsSpreadDataAsset* Spread =
						NewObject<UBallisticsSpreadDataAsset>(State.Launcher);
					Spread->SpreadMode = ESpreadMode::RandomCone;
					Spread->ProjectileCount = 4;
					State.Launcher->ProjectileData->SpreadConfig = Spread;
					State.Launcher->Launcher->bRequireLaunchCredits = true;
					State.Launcher->Launcher->GrantLaunchCredits(1, 5.f);
					State.Launcher->ForceNetUpdate();
				})
			.UntilClients(TEXT("Wait for spread credit policy"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher &&
						State.Launcher->Launcher->bRequireLaunchCredits;
				})
			.ThenClient(0, [](FNPSNetworkState& State)
				{
					UBallisticsSpreadDataAsset* Spread =
						NewObject<UBallisticsSpreadDataAsset>(State.Launcher);
					Spread->SpreadMode = ESpreadMode::RandomCone;
					Spread->ProjectileCount = 4;
					State.Launcher->ProjectileData->SpreadConfig = Spread;
					State.Launcher->Launcher->Launch(
						FVector(0.f, 1000.f, 100.f),
						FVector::UpVector);
				})
			.UntilServer(TEXT("Wait for every spread member"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 4;
				},
				FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Verify spread charged once"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 4));
					ASSERT_THAT(AreEqual(
						State.Launcher->Launcher->GetAvailableLaunchCredits(),
						0));
				});
	}

	TEST_METHOD(CreditsDisabledPreservesUnlimitedRemoteRequests)
	{
		PrepareLane(ELaunchAuthorityMode::ServerAuthoritative);
		Network
			.ThenClient(0, [this](FNPSNetworkState& State)
				{
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						600,
						600);
					SendDirectRemoteLaunch(
						State,
						ELaunchAuthorityMode::ServerAuthoritative,
						601,
						601);
				})
			.UntilServer(TEXT("Wait for unlimited requests"),
				[](FNPSNetworkState& State)
				{
					return State.Launcher->LaunchValidationCount >= 2;
				})
			.ThenServer(TEXT("Verify disabled credit policy"),
				[this](FNPSNetworkState& State)
				{
					ASSERT_THAT(IsFalse(
						State.Launcher->Launcher->bRequireLaunchCredits));
					ASSERT_THAT(AreEqual(State.Launcher->LaunchValidationCount, 2));
				});
	}

	TEST_METHOD(ClientPredicted_SecondProjectile_ReconcilesByMatchingID)
	{
		const FVector CorrectionPosition(500.f, 4321.f, 1000.f);
		const FVector CorrectionVelocity(1000.f, 0.f, 0.f);

		PrepareLane(ELaunchAuthorityMode::ClientPredicted);
		Network
			.ThenClient(0, [](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->Launch(
						FVector(0.f, 1000.f, 100.f),
						FVector::UpVector);
					State.Launcher->Launcher->Launch(
						FVector(0.f, 1000.f, 100.f),
						FVector::UpVector);
				})
			.ThenServer(TEXT("Correct second predicted projectile"),
				[CorrectionPosition, CorrectionVelocity](FNPSNetworkState& State)
				{
					State.Launcher->Launcher->ClientReconcileProjectile(
						1, CorrectionPosition, CorrectionVelocity, 0.1f);
				})
			.UntilClients(TEXT("Wait for matching projectile ID correction"),
				[CorrectionPosition](FNPSNetworkState& State)
				{
					for (TActorIterator<ANPSTestProjectile> It(State.World); It; ++It)
					{
						const UProjectileBallisticsComponent* Ballistics = It->Ballistics;
						if (Ballistics && Ballistics->IsLocallyPredicted() &&
							Ballistics->GetProjectileID() == 1)
						{
							const FVector Position =
								Ballistics->GetCurrentSnapshot().Position;
							return Position.X >= CorrectionPosition.X &&
								FMath::IsNearlyEqual(
									Position.Y, CorrectionPosition.Y, 1.f);
						}
					}
					return false;
				}, FTimespan::FromSeconds(10));
	}

	TEST_METHOD(ClientAuthoritative_ClientLaunch_ValidatesFlightLogAndDamagesExactlyOnce)
	{
		PrepareLane(ELaunchAuthorityMode::ClientAuthoritative);
		LaunchAndRequireAuthoritativeDamage();
	}

	TEST_METHOD(ClientAuthoritative_OneCreditRejectsSecondDirectRequest)
	{
		RequireSingleCreditForDirectRequests(
			ELaunchAuthorityMode::ClientAuthoritative);
	}

};

#endif
