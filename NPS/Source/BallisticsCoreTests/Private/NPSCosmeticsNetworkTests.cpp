// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "BallisticsDetonationComponent.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsProjectileDataAsset.h"
#include "EngineUtils.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "NPSTestEnvironment.h"
#include "NPSNetworkBackend.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "ProjectileCosmeticsRelay.h"
#include "ProjectileLauncherComponent.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

NETWORK_TEST_CLASS(FNPSCosmeticsNetworkTests, "NPS.Network.Cosmetics")
{
	struct FNPSCosmeticsNetworkState : public FBasePIENetworkComponentState
	{
		ANPSTestLauncher* Launcher = nullptr;
		ANPSTestAuthoredLauncher* AuthoredLauncher = nullptr;
		AProjectileCosmeticsRelay* Relay = nullptr;
		uint64 ProcessedEventBaseline = 0;
		uint64 SpawnedEffectBaseline = 0;
		double SettleUntil = 0.0;
	};

	FPIENetworkComponent<FNPSCosmeticsNetworkState> Network{
		TestRunner,
		TestCommandBuilder,
		bInitializing
	};
	FPacketSimulationSettings StableNetwork;

	void VerifyRemoteOwnerReceivesDetonation(ELaunchAuthorityMode AuthorityMode)
	{
		const FVector DetonationLocation(1000.f, 0.f, 100.f);
		constexpr uint64 ExpectedChannelEvents = 2;

		Network
			.ThenClients(TEXT("Capture owning-client detonation baselines"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					ASSERT_THAT(IsNotNull(Subsystem->GetCosmeticsManager()));
					State.ProcessedEventBaseline =
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount();
				})
			.ThenServer(TEXT("Detonate projectile owned by remote client"),
				[this, AuthorityMode, DetonationLocation](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.ClientConnections.Num(), 2));
					APlayerController* OwningController =
						State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(OwningController));

					FActorSpawnParameters LauncherSpawnParameters;
					LauncherSpawnParameters.Owner = OwningController;
					LauncherSpawnParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					State.Launcher = State.World->SpawnActor<ANPSTestLauncher>(
						ANPSTestLauncher::StaticClass(),
						DetonationLocation,
						FRotator::ZeroRotator,
						LauncherSpawnParameters);
					ASSERT_THAT(IsNotNull(State.Launcher));
					State.Launcher->ProjectileData->AuthorityModeOverride = AuthorityMode;
					State.Launcher->Launcher->bEnableCosmeticRouting = true;

					ANPSTestProjectile* Projectile =
						State.World->SpawnActor<ANPSTestProjectile>(
							ANPSTestProjectile::StaticClass(),
							DetonationLocation,
							FRotator::ZeroRotator);
					ASSERT_THAT(IsNotNull(Projectile));
					ASSERT_THAT(IsNotNull(Projectile->Ballistics));
					ASSERT_THAT(IsNotNull(Projectile->Detonation));

					UBallisticsExplosiveDataAsset* Explosive =
						LoadObject<UBallisticsExplosiveDataAsset>(
							nullptr,
							TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Fuse."
								"DA_NPS_Fuse"));
					ASSERT_THAT(IsNotNull(Explosive));
					ASSERT_THAT(IsFalse(Explosive->DetonationCosmetic.Effect.IsNull()));
					ASSERT_THAT(IsFalse(Explosive->DetonationCosmetic.Sound.IsNull()));
					ASSERT_THAT(IsTrue(Explosive->DetonationCosmetic.VisualRadius > 0.f));
					ASSERT_THAT(IsTrue(Explosive->DetonationCosmetic.AudibleRadius > 0.f));

					Projectile->Ballistics->SetSourceLauncher(
						State.Launcher->Launcher,
						State.Launcher->ProjectileData);
					Projectile->Detonation->bAutoArm = false;
					Projectile->Detonation->ConfigureExplosive(Explosive);
					Projectile->Detonation->DetonateAtLocation(DetonationLocation);
				})
			.UntilClients(TEXT("Wait for one visual and one audio detonation event"),
				[ExpectedChannelEvents](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() >=
							State.ProcessedEventBaseline + ExpectedChannelEvents;
				},
				FTimespan::FromSeconds(10))
			.ThenClients(TEXT("Verify detonation event count and type"),
				[this, ExpectedChannelEvents](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(AreEqual(
						Manager->GetProcessedEventCount(),
						State.ProcessedEventBaseline + ExpectedChannelEvents));
					ASSERT_THAT(AreEqual(
						Manager->GetLastProcessedEvent().Type,
						EProjectileCosmeticEventType::Detonation));
				})
			.ThenServer(TEXT("Begin detonation duplicate drain"),
				[](FNPSCosmeticsNetworkState& State)
				{
					State.SettleUntil = State.World->GetTimeSeconds() + 0.5;
				})
			.UntilServer(TEXT("Drain detonation relay traffic"),
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				},
				FTimespan::FromSeconds(2))
			.ThenClients(TEXT("Verify no duplicate detonation delivery"),
				[this, ExpectedChannelEvents](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(AreEqual(
						Manager->GetProcessedEventCount(),
						State.ProcessedEventBaseline + ExpectedChannelEvents));
				});
	}

	BEFORE_EACH()
	{
		// CQTest creates transient test worlds and test-only data objects that
		// cannot receive stable NetGUIDs. Ignore only those harness messages.
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
		TestRunner->AddExpectedMessagePlain(
			TEXT("FNetGUIDCache::SupportsObject: BallisticsProjectileDataAsset /Temp/UEDPIE_"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);

		FNetworkComponentBuilder<FNPSCosmeticsNetworkState>()
			.WithClients(2)
			.WithGameInstanceClass(UGameInstance::StaticClass())
			.WithGameMode(AGameModeBase::StaticClass())
			.WithPacketSimulationSettings(&StableNetwork)
			.Build(Network);
		Network.ThenServer(TEXT("Verify server replication backend"),
			[this](FNPSCosmeticsNetworkState& State)
			{
				ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			})
			.ThenClients(TEXT("Verify client replication backend"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
				});
	}

	TEST_METHOD(Relay_DisposableBatchesWaitForReplicationAndStayOwnerOnly)
	{
		Network
			.ThenClients(TEXT("Capture cosmetic baselines"),
				[](FNPSCosmeticsNetworkState& State)
				{
					State.ProcessedEventBaseline = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount();
				})
			.ThenServer(TEXT("Create a relay for the first client"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					FActorSpawnParameters Spawn;
					Spawn.Owner = State.ClientConnections[0]->PlayerController;
					State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
					ASSERT_THAT(IsNotNull(State.Relay));
					State.ProcessedEventBaseline = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount();
					State.SettleUntil = State.World->GetTimeSeconds() + 0.1;
				})
			.UntilServer(TEXT("Leave the relay spawn frame"),
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				})
			.ThenServer(TEXT("Bootstrap the owner channel reliably"),
				[](FNPSCosmeticsNetworkState& State)
				{
					FProjectileCosmeticEvent Event;
					Event.EventID = 101;
					State.Relay->SendCosmeticEvents({ Event });
				})
			.UntilClient(TEXT("Wait for initial owner delivery"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount()
						== State.ProcessedEventBaseline + 1;
				}, FTimespan::FromSeconds(5))
			.UntilServer(TEXT("Wait for profile ACK before testing steady unreliable priority"),
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.Relay->GetAcknowledgedProfileCount() == 1;
				}, FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Verify effects queue without spending the movement send budget"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UNetConnection* Connection = State.ClientConnections[0];
					UActorChannel* Channel = nullptr;
#if UE_WITH_IRIS
					UNetDriver* Driver = State.World->GetNetDriver();
					if (Driver->IsUsingIrisReplication())
					{
						// Iris represents the relay with a net object, not a legacy actor channel.
						UReplicationSystem* System = Driver->GetReplicationSystem();
						ASSERT_THAT(IsNotNull(System));
						ASSERT_THAT(IsTrue(System->GetReplicationBridge()
							->GetReplicatedRefHandle(State.Relay).IsValid()));
					}
					else
#endif
					{
						Channel = Connection->FindActorChannelRef(State.Relay);
						ASSERT_THAT(IsNotNull(Channel));
						ASSERT_THAT(IsTrue(Channel->ActorReplicator.IsValid()));
					}
					const int64 SendBits = Connection->SendBuffer.GetNumBits();
					const int32 SentPackets = Connection->OutTotalPackets;
					const int32 QueuedBits = Connection->QueuedBits;
					FProjectileCosmeticEvent Event;
					Event.EventID = 102;
					State.Relay->SendCosmeticEvents({ Event });
					State.Relay->ForceNetUpdate();
					if (Channel)
					{
						ASSERT_THAT(IsNotNull(Channel->ActorReplicator->RemoteFunctions));
					}
					ASSERT_THAT(AreEqual(Connection->SendBuffer.GetNumBits(), SendBits));
					ASSERT_THAT(AreEqual(Connection->OutTotalPackets, SentPackets));
					ASSERT_THAT(AreEqual(Connection->QueuedBits, QueuedBits));
					ASSERT_THAT(AreEqual(State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount(),
						State.ProcessedEventBaseline));
				})
			.UntilClient(TEXT("Wait for the queued owner batch"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount()
						== State.ProcessedEventBaseline + 2;
				}, FTimespan::FromSeconds(5))
			.ThenServer(TEXT("Allow unintended deliveries to drain"),
				[](FNPSCosmeticsNetworkState& State)
				{
					State.SettleUntil = State.World->GetTimeSeconds() + 0.5;
				})
			.UntilServer(TEXT("Drain relay traffic"),
				[](FNPSCosmeticsNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				})
			.ThenClient(TEXT("Owner receives each batch exactly once"), 0,
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
					ASSERT_THAT(AreEqual(Manager->GetProcessedEventCount(),
						State.ProcessedEventBaseline + 2));
					ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().EventID, 102));
				})
			.ThenClient(TEXT("Other clients never receive this listener's relay or effects"), 1,
				[this](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager()->GetProcessedEventCount(),
						State.ProcessedEventBaseline));
					TActorIterator<AProjectileCosmeticsRelay> It(State.World);
					ASSERT_THAT(IsFalse(static_cast<bool>(It)));
				});
	}

	TEST_METHOD(BeyondLauncherCull_OneTimeAndSustainedCosmeticsReachListener)
	{
		const FVector DistantLaunchLocation(10000.f, 0.f, 100.f);

		Network
			.ThenServer(TEXT("Spawn owned launcher beyond cull range"),
				[this, DistantLaunchLocation](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsTrue(State.ClientConnections.Num() >= 2));
					APlayerController* LaunchingController =
						State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(LaunchingController));

					FActorSpawnParameters SpawnParameters;
					SpawnParameters.Owner = LaunchingController;
					SpawnParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					State.Launcher = State.World->SpawnActor<ANPSTestCosmeticLauncher>(
						ANPSTestCosmeticLauncher::StaticClass(),
						DistantLaunchLocation,
						FRotator::ZeroRotator,
						SpawnParameters);
					ASSERT_THAT(IsNotNull(State.Launcher));
					UBallisticsLaunchCosmeticProfile* Profile =
						State.Launcher->Launcher->LaunchCosmeticProfile;
					ASSERT_THAT(IsNotNull(Profile));
					ASSERT_THAT(IsTrue(Profile->Effects.Num() > 0));
					const FBallisticsLaunchEffect& LaunchEffect = Profile->Effects[0];
					ASSERT_THAT(IsTrue(LaunchEffect.SourceBinding == TEXT("Muzzle")));
					ASSERT_THAT(IsTrue(LaunchEffect.SocketName == TEXT("Muzzle")));
					ASSERT_THAT(IsTrue(Profile->VisualRadius >= 10000.f));
					State.Launcher->Launcher->bRouteCosmeticEventsWithoutAssets = false;
					State.Launcher->SetNetCullDistanceSquared(FMath::Square(100.f));
					// Keep the shooter at the launch source for Iris spatial filtering.
					// The other client's view stays outside the launcher's cull radius.
					LaunchingController->SetViewTarget(State.Launcher);
					State.Launcher->ForceNetUpdate();
				})
			.UntilClient(TEXT("Wait for launching-client ownership"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					for (TActorIterator<ANPSTestLauncher> It(State.World); It; ++It)
					{
						if (It->GetOwner())
						{
							State.Launcher = *It;
							return true;
						}
					}
					return false;
				},
				FTimespan::FromSeconds(5))
			.UntilClient(TEXT("Prove launcher never becomes relevant to listener"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					for (TActorIterator<ANPSTestLauncher> It(State.World); It; ++It)
					{
						return false;
					}
					return true;
				},
				FTimespan::FromSeconds(1))
			.ThenClient(TEXT("Verify launching client cosmetic configuration"), 0,
				[this](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsNotNull(State.Launcher));
					ASSERT_THAT(IsNotNull(State.Launcher->GetOwner()));
					ASSERT_THAT(IsTrue(State.Launcher->HasLocalNetOwner()));
					ASSERT_THAT(IsNotNull(State.Launcher->Launcher));
					ASSERT_THAT(IsTrue(State.Launcher->Launcher->bEnableCosmeticRouting));
					ASSERT_THAT(IsNotNull(State.Launcher->Launcher->LaunchCosmeticProfile));
					ASSERT_THAT(IsTrue(
						State.Launcher->Launcher->LaunchCosmeticProfile->Effects.Num() > 0));
					ASSERT_THAT(IsFalse(
						State.Launcher->Launcher->LaunchCosmeticProfile->Effects[0].Effect.IsNull()));
					FTransform SourceTransform;
					ASSERT_THAT(IsTrue(State.Launcher->Launcher->ResolveLaunchSourceTransform(
						TEXT("Muzzle"),
						TEXT("Muzzle"),
						SourceTransform)));
				})
			.ThenClients(TEXT("Capture cosmetic baselines"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					ASSERT_THAT(IsNotNull(Subsystem->GetCosmeticsManager()));
					State.ProcessedEventBaseline =
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount();
					State.SpawnedEffectBaseline =
						Subsystem->GetCosmeticsManager()->GetSpawnedEffectCount();
				})
			.ThenClient(TEXT("Launch one projectile outside actor relevancy"), 0,
				[this, DistantLaunchLocation](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsNotNull(State.Launcher));
					State.Launcher->Launcher->Launch(
						DistantLaunchLocation,
						FVector::ForwardVector);
				})
			.UntilClient(TEXT("Prove immediate local launch dispatch"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() >
							State.ProcessedEventBaseline &&
						(!FApp::CanEverRender() ||
							Subsystem->GetCosmeticsManager()->GetSpawnedEffectCount() >
								State.SpawnedEffectBaseline);
				},
				FTimespan::FromSeconds(10))
			.UntilServer(TEXT("Prove server accepted launch and created listener relay"),
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetRelayCount() > 0;
				},
				FTimespan::FromSeconds(10))
			.UntilClient(TEXT("Wait for immediate visual-only relay batch"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() ==
							State.ProcessedEventBaseline + 1;
				},
				FTimespan::FromSeconds(2))
			.ThenClient(TEXT("Verify visual packet contains no sound assets"), 1,
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(IsTrue(Manager->GetLastProcessedEvent().LaunchEffects.Num() > 0));
					ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().Sounds.Num(), 0));
					if (FApp::CanEverRender())
					{
						ASSERT_THAT(IsTrue(
							Manager->GetSpawnedEffectCount() > State.SpawnedEffectBaseline));
					}
					int32 MuzzleEffectCount = 0;
					int32 EjectionEffectCount = 0;
					for (const FBallisticsLaunchEffect& Effect :
						Manager->GetLastProcessedEvent().LaunchEffects)
					{
						MuzzleEffectCount += Effect.SocketName == TEXT("Muzzle") ? 1 : 0;
						EjectionEffectCount +=
							Effect.SocketName == TEXT("ShellEject") ? 1 : 0;
					}
					ASSERT_THAT(AreEqual(MuzzleEffectCount, 2));
					ASSERT_THAT(AreEqual(EjectionEffectCount, 1));
				})
			.UntilServer(TEXT("Prove acoustic packet waits in the aggregate queue"),
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetPendingRemoteAcousticDeliveryCount() > 0;
				},
				FTimespan::FromSeconds(2))
			.UntilClient(TEXT("Wait for server-delayed acoustic-only relay batch"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() >=
							State.ProcessedEventBaseline + 2;
				},
				FTimespan::FromSeconds(10))
			.ThenClient(TEXT("Verify acoustic packet contains no visual assets"), 1,
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(IsTrue(Manager->GetLastProcessedEvent().Sounds.Num() > 0));
					ASSERT_THAT(AreEqual(Manager->GetLastProcessedEvent().LaunchEffects.Num(), 0));
					ASSERT_THAT(IsTrue(Manager->GetLastProcessedEvent().Effect.IsNull()));
					ASSERT_THAT(IsTrue(Manager->GetLastProcessedEvent().DecalMaterial.IsNull()));
				})
			.ThenClient(TEXT("Begin sustained launch reconstruction"), 0,
				[DistantLaunchLocation](FNPSCosmeticsNetworkState& State)
				{
					State.Launcher->Launcher->BeginLaunchSequence(
						DistantLaunchLocation,
						FVector::ForwardVector,
						12.f);
				})
			.UntilClient(TEXT("Wait for reliable sequence begin"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetActiveSequenceCount() == 1;
				},
				FTimespan::FromSeconds(10))
			.ThenClient(TEXT("End sustained launch reconstruction"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					State.Launcher->Launcher->EndLaunchSequence();
				})
			.UntilClient(TEXT("Wait for reliable sequence end"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetActiveSequenceCount() == 0;
				},
				FTimespan::FromSeconds(10));
	}

	TEST_METHOD(RelevantLauncher_UsesReceivingClientSocketTransforms)
	{
		const FVector ActorLocation(1000.f, 0.f, 100.f);
		const FVector ClaimedLaunchOrigin(1000.f, 0.f, -100.f);

		Network
			.ThenServer(TEXT("Spawn relevant cosmetic launcher with incorrect server pose"),
				[this, ActorLocation](FNPSCosmeticsNetworkState& State)
				{
					APlayerController* Owner = State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(Owner));
					FActorSpawnParameters SpawnParameters;
					SpawnParameters.Owner = Owner;
					SpawnParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					ANPSTestCosmeticLauncher* Launcher =
						State.World->SpawnActor<ANPSTestCosmeticLauncher>(
							ANPSTestCosmeticLauncher::StaticClass(),
							ActorLocation,
							FRotator::ZeroRotator,
							SpawnParameters);
					ASSERT_THAT(IsNotNull(Launcher));
					ASSERT_THAT(IsNotNull(Launcher->CosmeticSource));
					ASSERT_THAT(IsNotNull(Launcher->Launcher->LaunchCosmeticProfile));
					Launcher->CosmeticSource->SetRelativeLocation(FVector(0.f, 0.f, -500.f));
					Launcher->Launcher->LaunchCosmeticProfile->LaunchSound.Reset();
					Launcher->Launcher->LaunchCosmeticProfile->DistantSound.Reset();
					Launcher->SetNetCullDistanceSquared(FMath::Square(100000.f));
					Launcher->ForceNetUpdate();
					State.Launcher = Launcher;
				})
			.UntilClients(TEXT("Wait for relevant cosmetic launcher"),
				[ActorLocation](FNPSCosmeticsNetworkState& State)
				{
					for (TActorIterator<ANPSTestCosmeticLauncher> It(State.World); It; ++It)
					{
						if (It->GetActorLocation().Equals(ActorLocation, 1.f))
						{
							State.Launcher = *It;
							return true;
						}
					}
					return false;
				},
				FTimespan::FromSeconds(5))
			.ThenClient(TEXT("Set owning-client cosmetic pose"), 0,
				[this](FNPSCosmeticsNetworkState& State)
				{
					ANPSTestCosmeticLauncher* Launcher =
						Cast<ANPSTestCosmeticLauncher>(State.Launcher);
					ASSERT_THAT(IsNotNull(Launcher));
					Launcher->CosmeticSource->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
				})
			.ThenClient(TEXT("Set receiving-client cosmetic pose and baseline"), 1,
				[this](FNPSCosmeticsNetworkState& State)
				{
					ANPSTestCosmeticLauncher* Launcher =
						Cast<ANPSTestCosmeticLauncher>(State.Launcher);
					ASSERT_THAT(IsNotNull(Launcher));
					Launcher->CosmeticSource->SetRelativeLocation(FVector(0.f, 0.f, 200.f));
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(IsNotNull(Manager));
					State.ProcessedEventBaseline = Manager->GetProcessedEventCount();
				})
			.ThenClient(TEXT("Launch from owning client"), 0,
				[ClaimedLaunchOrigin](FNPSCosmeticsNetworkState& State)
				{
					State.Launcher->Launcher->Launch(
						ClaimedLaunchOrigin,
						FVector::ForwardVector);
				})
			.UntilClient(TEXT("Wait for receiving-client visual launch"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					return Manager &&
						Manager->GetProcessedEventCount() > State.ProcessedEventBaseline &&
						Manager->GetLastProcessedEvent().Type ==
							EProjectileCosmeticEventType::Launch &&
						Manager->GetLastProcessedEvent().PlaybackMask ==
							static_cast<uint8>(EProjectileCosmeticPlayback::Visual);
				},
				FTimespan::FromSeconds(5))
			.ThenClient(TEXT("Verify receiving-client socket transforms"), 1,
				[this, ClaimedLaunchOrigin](FNPSCosmeticsNetworkState& State)
				{
					ANPSTestCosmeticLauncher* Launcher =
						Cast<ANPSTestCosmeticLauncher>(State.Launcher);
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(IsNotNull(Launcher));
					ASSERT_THAT(IsNotNull(Manager));
					const FProjectileCosmeticEvent& Event = Manager->GetLastProcessedEvent();
					ASSERT_THAT(IsTrue(Event.SourceActor == Launcher));
					ASSERT_THAT(AreEqual(
						Event.LaunchEffects.Num(),
						Event.LaunchEffectTransforms.Num()));
					ASSERT_THAT(IsTrue(Event.LaunchEffects.Num() > 0));

					bool bVerifiedMuzzle = false;
					bool bVerifiedEject = false;
					for (int32 EffectIndex = 0;
						EffectIndex < Event.LaunchEffects.Num();
						++EffectIndex)
					{
						const FBallisticsLaunchEffect& Effect = Event.LaunchEffects[EffectIndex];
						const FBallisticsLaunchEffectTransform& Actual =
							Event.LaunchEffectTransforms[EffectIndex];
						FTransform SourceTransform;
						ASSERT_THAT(IsTrue(Launcher->Launcher->ResolveLaunchSourceTransform(
							Effect.SourceBinding,
							Effect.SocketName,
							SourceTransform)));
						const FVector ExpectedLocation =
							SourceTransform.TransformPosition(Effect.LocationOffset);
						ASSERT_THAT(IsTrue(Actual.bValid));
						ASSERT_THAT(IsTrue(
							Actual.Location.Equals(ExpectedLocation, 1.f)));
						ASSERT_THAT(IsFalse(
							Actual.Location.Equals(ClaimedLaunchOrigin, 1.f)));
						bVerifiedMuzzle |= Effect.SocketName == TEXT("Muzzle");
						bVerifiedEject |= Effect.SocketName == TEXT("ShellEject");
					}
					ASSERT_THAT(IsTrue(bVerifiedMuzzle));
					ASSERT_THAT(IsTrue(bVerifiedEject));
				});
	}

	TEST_METHOD(DelayedDetonation_ReachesClientsAfterLauncherIsDestroyed)
	{
		const FVector DetonationLocation(10000.f, 0.f, 100.f);

		Network
			.ThenClients(TEXT("Capture detonation baselines"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					ASSERT_THAT(IsNotNull(Subsystem->GetCosmeticsManager()));
					State.ProcessedEventBaseline =
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount();
				})
			.ThenServer(TEXT("Detonate after destroying source launcher"),
				[this, DetonationLocation](FNPSCosmeticsNetworkState& State)
				{
					State.Launcher = State.World->SpawnActor<ANPSTestLauncher>(
						ANPSTestLauncher::StaticClass(),
						DetonationLocation,
						FRotator::ZeroRotator);
					ASSERT_THAT(IsNotNull(State.Launcher));

					ANPSTestProjectile* Projectile =
						State.World->SpawnActor<ANPSTestProjectile>(
							ANPSTestProjectile::StaticClass(),
							DetonationLocation,
							FRotator::ZeroRotator);
					ASSERT_THAT(IsNotNull(Projectile));
					ASSERT_THAT(IsNotNull(Projectile->Ballistics));
					ASSERT_THAT(IsNotNull(Projectile->Detonation));

					UBallisticsExplosiveDataAsset* Explosive =
						LoadObject<UBallisticsExplosiveDataAsset>(
							nullptr,
							TEXT("/NPS/Examples/Data/Explosives/DA_NPS_Fuse."
								"DA_NPS_Fuse"));
					ASSERT_THAT(IsNotNull(Explosive));
					Projectile->Ballistics->SetSourceLauncher(
						State.Launcher->Launcher,
						nullptr);
					Projectile->Detonation->bAutoArm = false;
					Projectile->Detonation->ConfigureExplosive(Explosive);

					State.Launcher->Destroy();
					State.Launcher = nullptr;
					Projectile->Detonation->DetonateAtLocation(
						DetonationLocation);
				})
			.UntilClients(TEXT("Wait for launcher-independent detonation"),
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() >
							State.ProcessedEventBaseline;
				},
				FTimespan::FromSeconds(10));
	}

	TEST_METHOD(ClientAuthoritativeImpact_PlaysLocallyAndReachesRemoteListener)
	{
		const FVector ImpactLocation(1000.f, 0.f, 100.f);
		const FString ProjectileAssetPath =
			TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientAuth.DA_NPS_ClientAuth");

		Network
			.ThenServer(TEXT("Spawn client-owned launcher with impact cosmetics"),
				[this, ProjectileAssetPath, ImpactLocation](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsTrue(State.ClientConnections.Num() >= 2));
					APlayerController* LaunchingController =
						State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(LaunchingController));

					FActorSpawnParameters SpawnParameters;
					SpawnParameters.Owner = LaunchingController;
					SpawnParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					State.Launcher = State.World->SpawnActor<ANPSTestLauncher>(
						ANPSTestLauncher::StaticClass(),
						ImpactLocation,
						FRotator::ZeroRotator,
						SpawnParameters);
					ASSERT_THAT(IsNotNull(State.Launcher));

					UBallisticsProjectileDataAsset* ProjectileData =
						LoadObject<UBallisticsProjectileDataAsset>(
							nullptr,
							*ProjectileAssetPath);
					ASSERT_THAT(IsNotNull(ProjectileData));
					ASSERT_THAT(IsFalse(
						ProjectileData->DefaultImpactCosmetic.Effect.IsNull()));
					ASSERT_THAT(IsFalse(
						ProjectileData->DefaultImpactCosmetic.Sound.IsNull()));
					ASSERT_THAT(IsTrue(ProjectileData->ImpactVisualRadius > 0.f));
					ASSERT_THAT(IsTrue(ProjectileData->ImpactAudibleRadius > 0.f));
					ASSERT_THAT(IsTrue(
						State.Launcher->Launcher->SetProjectileDataAsset(ProjectileData)));
					State.Launcher->Launcher->bEnableCosmeticRouting = true;
					State.Launcher->ForceNetUpdate();
				})
			.UntilClient(TEXT("Wait for client-owned impact launcher"), 0,
				[ProjectileAssetPath](FNPSCosmeticsNetworkState& State)
				{
					for (TActorIterator<ANPSTestLauncher> It(State.World); It; ++It)
					{
						UBallisticsProjectileDataAsset* ProjectileData =
							It->Launcher->GetProjectileDataAsset();
						if (It->GetOwner() && ProjectileData &&
							ProjectileData->GetPathName() == ProjectileAssetPath)
						{
							State.Launcher = *It;
							return true;
						}
					}
					return false;
				},
				FTimespan::FromSeconds(5))
			.ThenClients(TEXT("Capture impact cosmetic baselines"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					ASSERT_THAT(IsNotNull(Subsystem));
					ASSERT_THAT(IsNotNull(Subsystem->GetCosmeticsManager()));
					State.ProcessedEventBaseline =
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount();
					State.SpawnedEffectBaseline =
						Subsystem->GetCosmeticsManager()->GetSpawnedEffectCount();
				})
			.ThenClient(TEXT("Report client-authoritative impact"), 0,
				[this, ImpactLocation](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsNotNull(State.Launcher));
					FHitResult Hit;
					Hit.ImpactPoint = ImpactLocation;
					Hit.Location = ImpactLocation;
					Hit.ImpactNormal = FVector::UpVector;
					Hit.Normal = FVector::UpVector;
					State.Launcher->Launcher->HandleProjectileImpactCosmetic(
						Hit,
						EImpactResponse::Embed,
						4242,
						State.Launcher->Launcher->GetProjectileDataAsset());
				})
			.UntilClient(TEXT("Wait for immediate owning-client impact"), 0,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() ==
							State.ProcessedEventBaseline + 1 &&
						(!FApp::CanEverRender() ||
							Subsystem->GetCosmeticsManager()->GetSpawnedEffectCount() >
								State.SpawnedEffectBaseline);
				},
				FTimespan::FromSeconds(5))
			.UntilClient(TEXT("Wait for relayed impact channels"), 1,
				[](FNPSCosmeticsNetworkState& State)
				{
					UProjectileBallisticsSubsystem* Subsystem =
						State.World->GetSubsystem<UProjectileBallisticsSubsystem>();
					return Subsystem && Subsystem->GetCosmeticsManager() &&
						Subsystem->GetCosmeticsManager()->GetProcessedEventCount() >=
							State.ProcessedEventBaseline + 2 &&
						(!FApp::CanEverRender() ||
							Subsystem->GetCosmeticsManager()->GetSpawnedEffectCount() >
								State.SpawnedEffectBaseline);
				},
				FTimespan::FromSeconds(10))
			.ThenClient(TEXT("Verify owning-client impact was not duplicated"), 0,
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(AreEqual(
						Manager->GetProcessedEventCount(),
						State.ProcessedEventBaseline + 1));
					ASSERT_THAT(AreEqual(
						Manager->GetLastProcessedEvent().Type,
						EProjectileCosmeticEventType::Impact));
				})
			.ThenClient(TEXT("Verify remote impact visual and audio delivery"), 1,
				[this](FNPSCosmeticsNetworkState& State)
				{
					UProjectileCosmeticsManager* Manager = State.World
						->GetSubsystem<UProjectileBallisticsSubsystem>()
						->GetCosmeticsManager();
					ASSERT_THAT(AreEqual(
						Manager->GetProcessedEventCount(),
						State.ProcessedEventBaseline + 2));
					ASSERT_THAT(AreEqual(
						Manager->GetLastProcessedEvent().Type,
						EProjectileCosmeticEventType::Impact));
				});
	}

	TEST_METHOD(AuthoredLauncherAssetsRemainAvailableOnClients)
	{
		Network
			.ThenServer(TEXT("Spawn Blueprint-authored launcher"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					UClass* LauncherClass = LoadClass<ANPSTestAuthoredLauncher>(
						nullptr,
						TEXT("/Game/Tests/BP_NPSTestAuthoredLauncher."
							"BP_NPSTestAuthoredLauncher_C"));
					ASSERT_THAT(IsNotNull(LauncherClass));
					State.AuthoredLauncher = State.World->SpawnActorDeferred<
						ANPSTestAuthoredLauncher>(
						LauncherClass,
						FTransform::Identity,
						nullptr,
						nullptr,
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					ASSERT_THAT(IsNotNull(State.AuthoredLauncher));
					State.AuthoredLauncher->FinishSpawning(FTransform::Identity);
					ASSERT_THAT(IsNotNull(State.AuthoredLauncher->Launcher));
					ASSERT_THAT(IsNotNull(
						State.AuthoredLauncher->Launcher->GetProjectileDataAsset()));
					ASSERT_THAT(IsNotNull(
						State.AuthoredLauncher->Launcher->LaunchCosmeticProfile));
					ASSERT_THAT(IsTrue(
						State.AuthoredLauncher->Launcher->bEnableCosmeticRouting));
				})
			.UntilClients(TEXT("Wait for authored launcher assets"),
				[](FNPSCosmeticsNetworkState& State)
				{
					TActorIterator<ANPSTestAuthoredLauncher> It(State.World);
					if (It)
					{
						State.AuthoredLauncher = *It;
					}
					return State.AuthoredLauncher &&
						State.AuthoredLauncher->Launcher &&
						State.AuthoredLauncher->Launcher->GetProjectileDataAsset() &&
						State.AuthoredLauncher->Launcher->LaunchCosmeticProfile &&
						State.AuthoredLauncher->Launcher->bEnableCosmeticRouting;
				},
				FTimespan::FromSeconds(5))
			.ThenClients(TEXT("Verify authored launcher configuration"),
				[this](FNPSCosmeticsNetworkState& State)
				{
					ASSERT_THAT(IsNotNull(State.AuthoredLauncher));
					ASSERT_THAT(IsTrue(
						State.AuthoredLauncher->Launcher->GetProjectileDataAsset()
							->GetPathName() ==
						TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ServerAuth."
							"DA_NPS_ServerAuth")));
					ASSERT_THAT(IsTrue(
						State.AuthoredLauncher->Launcher->LaunchCosmeticProfile
							->GetPathName() ==
						TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile."
							"DA_NPS_LaunchProfile")));
				});
	}

	TEST_METHOD(Detonation_OwningRemoteClientReceivesExactlyOnce_ServerAuthoritative)
	{
		VerifyRemoteOwnerReceivesDetonation(
			ELaunchAuthorityMode::ServerAuthoritative);
	}

	TEST_METHOD(Detonation_OwningRemoteClientReceivesExactlyOnce_ClientAuthoritative)
	{
		VerifyRemoteOwnerReceivesDetonation(
			ELaunchAuthorityMode::ClientAuthoritative);
	}
};

#endif
