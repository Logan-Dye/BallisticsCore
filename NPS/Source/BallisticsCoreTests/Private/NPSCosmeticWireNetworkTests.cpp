// Copyright Persidera Industries LLC. All Rights Reserved.

#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST
#include "BallisticsCosmeticTypes.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NPSNetworkBackend.h"
#include "NPSTestEnvironment.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "ProjectileCosmeticsRelay.h"
#include "Serialization/BitWriter.h"

NETWORK_TEST_CLASS(FNPSCosmeticWireNetworkTests, "NPS.Network.CosmeticWire")
{
	struct FState : FBasePIENetworkComponentState
	{
		double Start = 0.0;
		int32 Shots = 0;
		int32 TargetShots = 200;
		float ShotRate = 20.f;
		uint32 PeakPending = 0;
		uint32 PeakFlights = 0;
		TArray<int32> StartBytes;
		TArray<int32> StartInBytes;
		FProjectileCosmeticEvent Event;
		uint64 ProcessedBefore = 0;
		uint64 TemplateBuildsBefore = 0;
		uint32 ChannelsPerShot = 1;
		AProjectileCosmeticsRelay* Relay = nullptr;
		AProjectileCosmeticsRelay* OtherRelay = nullptr;
		ANPSTestLauncher* Source = nullptr;
		TRefCountPtr<FProjectileCosmeticSendQueue> QueueOwner;
	};
	FPIENetworkComponent<FState> Network{TestRunner, TestCommandBuilder, bInitializing};

	BEFORE_EACH()
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("NPSCosmeticWireBaseline")))
		{
			TestRunner->AddExpectedMessagePlain(TEXT("Splitting multicast net object attachment"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		}
		FNetworkComponentBuilder<FState>()
			.WithClients(2)
			.WithGameInstanceClass(UGameInstance::StaticClass())
			.WithGameMode(AGameModeBase::StaticClass())
			.Build(Network);
	}

	TEST_METHOD(SequenceBeginEndBeforeProfileAck)
	{
		Network.ThenClients(TEXT("Capture initial playback count"), [](FState& State)
		{
			State.ProcessedBefore = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()
				->GetCosmeticsManager()->GetProcessedEventCount();
		})
		.ThenServer(TEXT("Queue Begin, transform and End before any profile ACK"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			FLaunchSequenceCommand Begin;
			Begin.SequenceID = 732;
			Begin.LaunchesPerSecond = 0.1f;
			Begin.LaunchEvent.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Visual);
			State.Relay->SendSequenceCommands({Begin});
			FProjectileCosmeticEvent Transform = Begin.LaunchEvent;
			Transform.Type = EProjectileCosmeticEventType::SequenceTransform;
			Transform.SequenceID = Begin.SequenceID;
			Transform.Location = FVector(300.f, 0.f, 0.f);
			State.Relay->SendCosmeticEvents({Transform});
			FLaunchSequenceCommand End = Begin;
			End.Type = ELaunchSequenceCommandType::End;
			State.Relay->SendSequenceCommands({End});
			ASSERT_THAT(AreEqual(State.Relay->GetPendingRecordCount(), uint32(0)));
		ASSERT_THAT(AreEqual(State.Relay->GetReliableRecordCount(), uint64(2)));
		})
		.UntilServer(TEXT("Check sequence delivery has no ACK barrier"), [](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			return State.Relay->GetPendingRecordCount() == 0;
		}, FTimespan::FromSeconds(5))
		.UntilClient(TEXT("Begin plays once and End leaves no active sequence"), 0, [](FState& State)
		{
			const auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			return Manager->GetProcessedEventCount() == State.ProcessedBefore + 1 && Manager->GetActiveSequenceCount() == 0;
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Other owner receives no sequence"), 1, [this](FState& State)
		{
			ASSERT_THAT(AreEqual(State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()
				->GetProcessedEventCount(), State.ProcessedBefore));
		});
	}

	TEST_METHOD(BurstAndDeepProfileChange)
	{
		Network.ThenClients(TEXT("Capture burst playback baseline"), [](FState& State)
		{
			State.ProcessedBefore = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()
				->GetCosmeticsManager()->GetProcessedEventCount();
		})
		.ThenServer(TEXT("Send 64 records sharing one static profile"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Visual);
			FBallisticsCosmeticBoolParameter Bool;
			Bool.Name = TEXT("Suppressed");
			State.Event.BoolParameters.Add(Bool);
			FBallisticsCosmeticFloatParameter Float;
			Float.Name = TEXT("Heat");
			State.Event.FloatParameters.Add(Float);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.Event.EventID = -1;
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("ACK warm profile before unreliable burst"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.UntilClient(TEXT("Warm event arrived"), 0, [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetProcessedEventCount() == State.ProcessedBefore + 1;
		}, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Queue64 established-profile unreliable events"), [this](FState& State)
		{
			TArray<FProjectileCosmeticEvent> Events;
			for (int32 Index = 0; Index < 64; ++Index)
			{
				State.Event.EventID = Index;
				Events.Add(State.Event);
			}
			State.Relay->SendCosmeticEvents(Events);
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(1)));
		})
		.UntilServer(TEXT("Flush the burst after profile ACK"), [](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			return State.Relay->GetPendingRecordCount() == 0;
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Start burst delivery observation"), 0, [](FState& State)
		{
			State.Start = State.World->GetTimeSeconds();
		})
		.UntilClient(TEXT("All 64 zero-loss events arrived"), 0, [this](FState& State)
		{
			const auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			if (!State.Shots && State.World->GetTimeSeconds() - State.Start > 4.0)
			{
				TestRunner->AddInfo(FString::Printf(TEXT("NPS burst received=%llu last_event=%d"),
					Manager->GetProcessedEventCount() - State.ProcessedBefore, Manager->GetLastProcessedEvent().EventID));
				State.Shots = 1;
			}
			return Manager->GetProcessedEventCount() == State.ProcessedBefore + 65;
		}, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Change deep parameters in the same hash bucket and send a large pose"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(State.Relay->GetMaxUnreliableSerializedBits() > 0));
			ASSERT_THAT(IsTrue(State.Relay->GetMaxUnreliableSerializedBits() <= FProjectileCosmeticWireBatch::MaxUnreliableSerializedBits));
			State.Event.WireTemplate.Reset();
		State.Event.BoolParameters[0].bValue = true;
			State.Event.FloatParameters[0].Value = 0.75f;
			State.Event.EventID = 65;
			State.Event.LaunchEffectTransforms.SetNum(16);
			for (auto& Transform : State.Event.LaunchEffectTransforms)
			{
				Transform.bValid = true;
				Transform.Location = FVector(1234.f, 5678.f, 9012.f);
				Transform.Rotation = FRotator(10.f, 20.f, 30.f);
			}
			State.Relay->SendCosmeticEvents({State.Event});
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(2)));
		})
		.UntilServer(TEXT("Flush changed profile and reliable large record"), [](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			return State.Relay->GetPendingRecordCount() == 0;
		}, FTimespan::FromSeconds(5))
		.UntilClient(TEXT("Changed profile arrived"), 0, [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()
				->GetProcessedEventCount() == State.ProcessedBefore + 66;
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Deep bool, float and all fallback poses survive"), 0, [this](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			ASSERT_THAT(AreEqual(Event.EventID, 65));
			ASSERT_THAT(IsTrue(Event.BoolParameters[0].bValue));
			ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Event.FloatParameters[0].Value, 0.75f)));
			ASSERT_THAT(AreEqual(Event.LaunchEffectTransforms.Num(), 16));
			ASSERT_THAT(IsTrue(FVector(Event.LaunchEffectTransforms[15].Location).Equals(FVector(1234.f, 5678.f, 9012.f), 0.21)));
		});
	}

	TEST_METHOD(SourceResolvesAfterOwnerRelevanceChanges)
	{
		Network.ThenClients(TEXT("Capture source test baseline"), [](FState& State)
		{
			State.ProcessedBefore = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()
				->GetCosmeticsManager()->GetProcessedEventCount();
		})
		.ThenServer(TEXT("Create a source only relevant to the other owner"), [](FState& State)
		{
			State.World->SpawnActor<ANPSTestTarget>();
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			Spawn.CustomPreSpawnInitialization = [](AActor* Actor) { Actor->bOnlyRelevantToOwner = true; };
			State.Source = State.World->SpawnActor<ANPSTestLauncher>(Spawn);
			State.Source->ForceNetUpdate();
			State.OtherRelay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			Spawn.Owner = State.ClientConnections[1]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
		})
		.UntilClient(TEXT("Source is replicated to its first owner"), 0, [](FState& State)
		{
			return static_cast<bool>(TActorIterator<ANPSTestLauncher>(State.World));
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Listener has no source actor"), 1, [this](FState& State)
		{
			ASSERT_THAT(IsFalse(static_cast<bool>(TActorIterator<ANPSTestLauncher>(State.World))));
		})
		.ThenServer(TEXT("Send cached source reference, sound and fallback pose"), [](FState& State)
		{
			State.Event.SourceActor = State.Source;
			State.Event.EventID = 81;
			State.Event.bUsePropagationDelay = false;
			State.Event.Sounds.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/NPS/FirstPerson/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"))));
			FBallisticsLaunchEffectTransform Transform;
			Transform.Location = FVector(789.f, 123.f, 456.f);
			State.Event.LaunchEffectTransforms.Add(Transform);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.OtherRelay->SendCosmeticEvents({State.Event});
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilClient(TEXT("First owner resolves its own world's source"), 0, [](FState& State)
		{
			TActorIterator<ANPSTestLauncher> Source(State.World);
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			return Source && Event.EventID == 81 && Event.SourceActor == *Source && Event.SourceActor->GetWorld() == State.World;
		}, FTimespan::FromSeconds(5))
		.UntilServer(TEXT("Flush first source event"), [](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			return State.Relay->GetPendingRecordCount() == 0;
		}, FTimespan::FromSeconds(5))
		.UntilClient(TEXT("Unmapped source does not block playback"), 1, [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()
				->GetProcessedEventCount() == State.ProcessedBefore + 1;
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Fallback pose and real sound reference survive"), 1, [this](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			ASSERT_THAT(IsTrue(Event.SourceActor == nullptr));
			ASSERT_THAT(AreEqual(Event.LaunchEffectTransforms.Num(), 1));
			ASSERT_THAT(IsTrue(FVector(Event.LaunchEffectTransforms[0].Location).Equals(FVector(789.f, 123.f, 456.f), 0.21)));
			ASSERT_THAT(AreEqual(Event.Sounds.Num(), 1));
			ASSERT_THAT(IsTrue(Event.Sounds[0].ToSoftObjectPath().ToString().Contains(TEXT("FirstPersonTemplateWeaponFire02"))));
		})
		.ThenServer(TEXT("Make the same source relevant to the listener"), [](FState& State)
		{
			State.Source->SetOwner(State.ClientConnections[1]->PlayerController);
			State.Source->ForceNetUpdate();
		})
		.UntilClient(TEXT("Source actor becomes mapped"), 1, [](FState& State)
		{
			return static_cast<bool>(TActorIterator<ANPSTestLauncher>(State.World));
		}, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Reuse the profile after source relevance changes"), [this](FState& State)
		{
			State.Event.EventID = 82;
			State.Relay->SendCosmeticEvents({State.Event});
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(1)));
		})
		.UntilClient(TEXT("Dynamic source reference resolves on the next event"), 1, [](FState& State)
		{
			const auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			return Manager->GetProcessedEventCount() == State.ProcessedBefore + 2 &&
				Manager->GetLastProcessedEvent().EventID == 82 && IsValid(Manager->GetLastProcessedEvent().SourceActor);
		}, FTimespan::FromSeconds(5))
		.ThenClient(TEXT("Resolved source is exactly the actor in this client world"), 1, [this](FState& State)
		{
			TActorIterator<ANPSTestLauncher> Source(State.World);
			ASSERT_THAT(IsTrue(static_cast<bool>(Source)));
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			ASSERT_THAT(IsTrue(Event.SourceActor == *Source));
			ASSERT_THAT(IsTrue(Event.SourceActor->GetWorld() == State.World));
		});
	}

	TEST_METHOD(NewProfileDoesNotStallActiveTrafficUnderLoss)
	{
		Network.ThenServer(TEXT("Warm an established profile"), [](FState& State)
		{
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.EventID = 1;
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Established profile is ACKed"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Hold new-profile ACKs and apply10percent packet loss"), [](FState& State)
		{
			State.Relay->bHoldProfileAcknowledgementsForTesting = true;
			FPacketSimulationSettings Loss;
			Loss.PktLoss = 10;
			State.World->GetNetDriver()->SetPacketSimulationSettings(Loss);
			FProjectileCosmeticEvent New = State.Event;
			New.WireTemplate.Reset();
			New.VolumeMultiplier = 0.75f;
			New.EventID = 2;
			State.Relay->SendCosmeticEvents({New});
			State.Start = State.World->GetTimeSeconds();
		})
		.UntilServer(TEXT("Established events continue while the new profile remains unacknowledged"), [this](FState& State)
		{
			if (State.World->GetTimeSeconds() - State.Start >= State.Shots * 0.05 && State.Shots < 20)
			{
				State.Event.EventID = 100 + ++State.Shots;
				State.Relay->SendCosmeticEvents({State.Event});
			}
			State.Relay->FlushCosmeticQueue();
			if (State.Shots < 20) { return false; }
			if (!this->Assert.AreEqual(State.Relay->GetAcknowledgedProfileCount(), uint32(1))) { return false; }
			if (!this->Assert.IsTrue(State.Relay->GetUnreliableBatchCount() > 0)) { return false; }
			if (!this->Assert.AreEqual(State.Relay->GetPendingRecordCount(), uint32(0))) { return false; }
			State.World->GetNetDriver()->SetPacketSimulationSettings(FPacketSimulationSettings());
			return true;
		}, FTimespan::FromSeconds(10))
		.UntilClient(TEXT("Active traffic reached the owner despite the held ACK and loss"), 0, [](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			return Event.EventID >= 100;
		}, FTimespan::FromSeconds(10))
		.ThenServer(TEXT("Drop one established-profile batch completely"), [](FState& State)
		{
			State.Relay->bHoldProfileAcknowledgementsForTesting = false;
			FPacketSimulationSettings Loss; Loss.PktLoss = 100;
			State.World->GetNetDriver()->SetPacketSimulationSettings(Loss);
			State.Event.EventID = 777;
			State.Relay->SendCosmeticEvents({State.Event});
			State.Start = State.World->GetTimeSeconds();
		})
		.UntilServer(TEXT("Let the unreliable packet be discarded"), [](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			return State.World->GetTimeSeconds() - State.Start > 0.3;
		})
		.ThenServer(TEXT("Restore loss-free transport"), [](FState& State) { State.World->GetNetDriver()->SetPacketSimulationSettings(FPacketSimulationSettings()); })
		.ThenServer(TEXT("Send fresh traffic after the locally discarded packet"), [](FState& State)
		{
			State.Event.EventID = 778;
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilClient(TEXT("Fresh unreliable traffic recovers without replaying the dropped shot"), 0, [](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			return Event.EventID == 778;
		}, FTimespan::FromSeconds(15));
	}

	TEST_METHOD(SharedProfilesAndSourceLifetimes)
	{
		Network.ThenServer(TEXT("Many weapon lifetimes share one prepared loadout"), [this](FState& State)
		{
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.TemplateBuildsBefore = BallisticsCosmeticWire::GetTemplateBuildCount();
			for (int32 Index = 0; Index < 32; ++Index)
			{
				auto* Source = State.World->SpawnActor<ANPSTestLauncher>();
				State.Event.SourceActor = Source;
				State.Event.AudioSourceID = FGuid(Index + 1, 2, 3, 4);
				State.Event.EventID = Index;
				State.Relay->SendCosmeticEvents({State.Event});
				Source->Destroy();
			}
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(1)));
			ASSERT_THAT(AreEqual(State.Relay->GetProfileDefinitionCount(), uint64(1)));
			ASSERT_THAT(AreEqual(State.Relay->GetAudioSourceCount(), uint32(32)));
			ASSERT_THAT(AreEqual(BallisticsCosmeticWire::GetTemplateBuildCount(), State.TemplateBuildsBefore));
		})
		.UntilServer(TEXT("Warm shared profile ACK"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.UntilClient(TEXT("Every lifetime retained its per-weapon GUID"), 0, [](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			return Event.EventID == 31 && Event.AudioSourceID == FGuid(32, 2, 3, 4);
		}, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Delay packets while160 additional static profiles register"), [](FState& State)
		{
			FPacketSimulationSettings Lag;
			Lag.PktLag = 300;
			State.World->GetNetDriver()->SetPacketSimulationSettings(Lag);
			State.Event.SourceActor = nullptr;
			for (int32 Index = 0; Index < 64; ++Index) { State.Event.EventID = 100 + Index; State.Relay->SendCosmeticEvents({State.Event}); }
			for (int32 Index = 0; Index < 160; ++Index)
			{
				FProjectileCosmeticEvent Event;
				Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
				Event.EventID = 1000 + Index;
				Event.VolumeMultiplier = float(Index + 2);
				Event.WireTemplate = BallisticsCosmeticWire::Prepare(Event);
				State.Relay->SendCosmeticEvents({Event});
			}
		})
		.UntilServer(TEXT("Drain the static catalog workload"), [this](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			if (!this->Assert.IsTrue(State.Relay->GetRegisteredProfileCount() <= AProjectileCosmeticsRelay::MaxProfiles)) { return false; }
			if (!this->Assert.IsTrue(State.Relay->GetQueuedUnreliableBatchCount() <= 10)) { return false; }
			return State.Relay->GetPendingRecordCount() == 0;
		}, FTimespan::FromSeconds(15))
		.UntilClient(TEXT("All256 valid records decoded with161 lifetime profiles"), 0, [this](FState& State)
		{
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It)
			{
				if (It->GetReceivedRecordCount() == 256)
				{
					if (!this->Assert.AreEqual(It->GetSkippedRecordCount(), uint64(0))) { return false; }
					if (!this->Assert.IsTrue(It->GetRegisteredProfileCount() <= AProjectileCosmeticsRelay::MaxProfiles)) { return false; }
					return true;
				}
			}
			return false;
		}, FTimespan::FromSeconds(20))
		.ThenServer(TEXT("Reuse retains every definition and shared pointer"), [this](FState& State)
		{
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(161)));
			ASSERT_THAT(AreEqual(State.Relay->GetProfileDefinitionCount(), uint64(161)));
			ASSERT_THAT(IsTrue(State.Relay->GetProfileTemplate(1) == State.Event.WireTemplate.Get()));
			State.Relay->SendCosmeticEvents({State.Event});
			ASSERT_THAT(AreEqual(State.Relay->GetProfileDefinitionCount(), uint64(161)));
			State.World->GetNetDriver()->SetPacketSimulationSettings(FPacketSimulationSettings());
		});
	}

	TEST_METHOD(MixedUnknownAndMalformedRecordsContinueSafely)
	{
		Network.ThenServer(TEXT("Create receiver profile"), [](FState& State)
		{
			FActorSpawnParameters Spawn;
			Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Wait for receiver definition"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Good, unknown profile, empty frame, bad type, sequence Begin and End, then good"), [this](FState& State)
		{
			FBitWriter Writer(FProjectileCosmeticWireBatch::MaxBytes * 8, false);
			uint8 Count = 7;
			Writer.SerializeBits(&Count, 8);
			auto Add = [&](FProjectileCosmeticWireRecord Record)
			{
				FBitWriter Encoded(4096, false);
				BallisticsCosmeticWire::SerializeRecord(Encoded, Record);
				uint16 Bits = uint16(Encoded.GetNumBits());
				Writer.SerializeBits(&Bits, 16);
				Writer.SerializeBits(Encoded.GetData(), Bits);
			};
			FProjectileCosmeticWireRecord Good;
			Good.ProfileID = 1;
			Good.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			Good.Event.EventID = 91;
			Add(Good);
			auto Unknown = Good; Unknown.ProfileID = MAX_uint32; Add(Unknown);
			uint16 Empty = 0; Writer.SerializeBits(&Empty, 16);
			uint16 BadBits = 4; uint8 BadType = 14;
			Writer.SerializeBits(&BadBits, 16); Writer.SerializeBits(&BadType, 4);
			auto Begin = Good; Begin.bSequenceCommand = true; Begin.Event.SequenceID = 794; Begin.Event.EventID = 92; Begin.LaunchesPerSecond = 0.1f; Add(Begin);
			auto End = Begin; End.CommandType = ELaunchSequenceCommandType::End; End.ProfileID = 0; Add(End);
			Good.Event.EventID = 93; Add(Good);
			ASSERT_THAT(IsFalse(Writer.IsError()));
			FProjectileCosmeticWireBatch Batch;
			Batch.BitCount = uint16(Writer.GetNumBits());
			FMemory::Memcpy(Batch.Data, Writer.GetData(), Writer.GetNumBytes());
			State.Relay->ClientReceiveSequenceBatch(Batch);
		})
		.UntilClient(TEXT("Following valid event and sequence End both dispatch"), 0, [this](FState& State)
		{
			const auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It)
			{
				if (Manager->GetLastProcessedEvent().EventID == 93 && Manager->GetActiveSequenceCount() == 0)
				{
					if (!this->Assert.AreEqual(It->GetSkippedRecordCount(), uint64(3))) { return false; }
					if (!this->Assert.AreEqual(It->GetReceivedRecordCount(), uint64(5))) { return false; }
					return true;
				}
			}
			return false;
		}, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Reject a truncated frame and accept the next batch"), [](FState& State)
		{
			FBitWriter Bad(256, false);
			uint8 One = 1; uint16 Impossible = MAX_uint16;
			Bad.SerializeBits(&One, 8); Bad.SerializeBits(&Impossible, 16);
			FProjectileCosmeticWireBatch Batch;
			Batch.BitCount = uint16(Bad.GetNumBits());
			FMemory::Memcpy(Batch.Data, Bad.GetData(), Bad.GetNumBytes());
			State.Relay->ClientReceiveSequenceBatch(Batch);
			State.Event.EventID = 94;
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilClient(TEXT("Malformed frame did not affect later traffic"), 0, [](FState& State)
		{
			const auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It)
			{
				if (Manager->GetLastProcessedEvent().EventID == 94) { return It->GetSkippedRecordCount() == 4 && It->GetReceivedRecordCount() == 6; }
			}
			return false;
		}, FTimespan::FromSeconds(5));
	}

	TEST_METHOD(CatalogHardCapPreservesEstablishedProfiles)
	{
		Network.ThenServer(TEXT("Create lifetime content catalog"), [this](FState& State)
		{
			TestRunner->AddExpectedMessagePlain(TEXT("NPS cosmetic profile catalog is full"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			FActorSpawnParameters Spawn; Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
		})
		.UntilServer(TEXT("Register1024 content profiles at four per frame"), [](FState& State)
		{
			for (int32 I = 0; I < 4 && State.Shots < 1024; ++I)
			{
				FProjectileCosmeticEvent Event = State.Event;
				Event.VolumeMultiplier = float(++State.Shots);
				if (State.Shots != 1) { Event.WireTemplate.Reset(); Event.WireTemplate = BallisticsCosmeticWire::Prepare(Event); }
				State.Relay->SendCosmeticEvents({Event});
			}
			return State.Relay->GetAcknowledgedProfileCount() == 1024;
		}, FTimespan::FromSeconds(30))
		.ThenServer(TEXT("Reject overflow and reuse the original shared template"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(State.Relay->GetProfileTemplate(1) == State.Event.WireTemplate.Get()));
			FProjectileCosmeticEvent Overflow = State.Event; Overflow.WireTemplate.Reset(); Overflow.VolumeMultiplier = 2048.f;
			State.Relay->SendCosmeticEvents({Overflow});
			ASSERT_THAT(AreEqual(State.Relay->GetRejectedProfileCount(), uint64(1)));
			State.Event.EventID = 5000; State.Relay->SendCosmeticEvents({State.Event});
			ASSERT_THAT(AreEqual(State.Relay->GetProfileDefinitionCount(), uint64(1024)));
			ASSERT_THAT(AreEqual(State.Relay->GetRegisteredProfileCount(), uint32(1024)));
		})
		.UntilClient(TEXT("Established traffic survives the full catalog"), 0, [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent().EventID == 5000;
		}, FTimespan::FromSeconds(10));
	}

	TEST_METHOD(QueuedOwnershipUnderBurstPressure)
	{
		Network.ThenServer(TEXT("Warm one profile before the queued burst"), [](FState& State)
		{
			FActorSpawnParameters Spawn; Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.AudioSourceID = BallisticsCosmeticWire::NewAudioSourceID();
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Definition acknowledged"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.ThenClients(TEXT("Apply the return half of the burst RTT"), [](FState& State)
		{
			int32 RTT = 300; FParse::Value(FCommandLine::Get(), TEXT("NPSCosmeticWireRTT="), RTT);
			FPacketSimulationSettings Lag; Lag.PktLag = RTT / 2; State.World->GetNetDriver()->SetPacketSimulationSettings(Lag);
		})
		.ThenServer(TEXT("Queue600 records while connection bandwidth is constrained"), [this](FState& State)
		{
			int32 RTT = 300; FParse::Value(FCommandLine::Get(), TEXT("NPSCosmeticWireRTT="), RTT);
			FPacketSimulationSettings Lag; Lag.PktLag = RTT / 2; State.World->GetNetDriver()->SetPacketSimulationSettings(Lag);
			State.ClientConnections[0]->CurrentNetSpeed = 1000;
			State.Start = State.World->GetTimeSeconds();
			TArray<FProjectileCosmeticEvent> Burst;
			for (int32 I = 1; I <= 600; ++I) { auto Event = State.Event; Event.EventID = I; Burst.Add(MoveTemp(Event)); }
			State.Relay->SendCosmeticEvents(Burst);
			State.PeakPending = State.Relay->GetPendingRecordCount();
			State.PeakFlights = State.Relay->GetQueuedUnreliableBatchCount();
			ASSERT_THAT(IsTrue(State.PeakPending > 0));
		})
		.UntilServer(TEXT("Restore bandwidth and drain actual queued ownership"), [this](FState& State)
		{
			State.Relay->FlushCosmeticQueue();
			State.PeakFlights = FMath::Max(State.PeakFlights, State.Relay->GetQueuedUnreliableBatchCount());
			if (!this->Assert.IsTrue(State.PeakFlights <= 10)) { return false; }
			if (State.World->GetTimeSeconds() - State.Start < 0.5) { return false; }
			State.ClientConnections[0]->CurrentNetSpeed = 100000;
			if (State.Relay->GetPendingRecordCount() || State.Relay->GetQueuedUnreliableBatchCount()) { return false; }
			TestRunner->AddInfo(FString::Printf(TEXT("NPS queue burst=600 peak_pending=%u peak_queued=%u drained_seconds=%.3f"), State.PeakPending, State.PeakFlights, State.World->GetTimeSeconds()-State.Start));
			return true;
		}, FTimespan::FromSeconds(10))
		.UntilClient(TEXT("All zero-loss burst records arrive without overwrite"), 0, [](FState& State)
		{
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It) { return It->GetReceivedRecordCount() == 601 && It->GetSkippedRecordCount() == 0; }
			return false;
		}, FTimespan::FromSeconds(15));
	}

	TEST_METHOD(ManyUnmappedLauncherLifetimes)
	{
		Network.ThenServer(TEXT("Prepare common loadout and an irrelevant owning actor"), [](FState& State)
		{
			FActorSpawnParameters Spawn; Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Source = State.World->SpawnActor<ANPSTestLauncher>();
			State.Source->SetReplicates(false);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Shared profile ready"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.UntilServer(TEXT("Destroy and recreate1100 pairs of launchers on one unmapped actor"), [this](FState& State)
		{
			for (int32 I = 0; I < 8 && State.Shots < 1100; ++I)
			{
				auto* A = NewObject<UProjectileLauncherComponent>(State.Source);
				auto* B = NewObject<UProjectileLauncherComponent>(State.Source);
				if (!this->Assert.IsTrue(A->GetAudioSourceID() != B->GetAudioSourceID())) { return false; }
				for (auto* Component : {A, B})
				{
					State.Event.AudioSourceID = Component->GetAudioSourceID(); State.Event.EventID = ++State.TargetShots;
					State.Relay->SendCosmeticEvents({State.Event}); Component->DestroyComponent();
				}
				++State.Shots;
			}
			State.Relay->FlushCosmeticQueue();
			return State.Shots == 1100 && !State.Relay->GetPendingRecordCount();
		}, FTimespan::FromSeconds(20))
		.ThenServer(TEXT("No per-instance relay entries or static duplication"), [this](FState& State)
		{
			ASSERT_THAT(AreEqual(State.Relay->GetAudioSourceCount(), uint32(0)));
			ASSERT_THAT(AreEqual(State.Relay->GetProfileDefinitionCount(), uint64(1)));
			State.Source->Destroy();
		})
		.UntilClient(TEXT("All2200 unmapped identities dispatch"), 0, [](FState& State)
		{
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It) { return It->GetReceivedRecordCount() == 2201 && It->GetSkippedRecordCount() == 0; }
			return false;
		}, FTimespan::FromSeconds(20));
	}

	TEST_METHOD(CompatibilityDefinitionCanFollowReceivedRecord)
	{
		Network.ThenServer(TEXT("Warm static profile"), [](FState& State)
		{
			FActorSpawnParameters Spawn; Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Profile ready"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Deliver an indexed record before its reliable compatibility definition"), [](FState& State)
		{
			FProjectileCosmeticWireRecord Record; Record.ProfileID = 1; Record.AudioSourceIndex = 1; Record.Event.EventID = 777;
			Record.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			FBitWriter Encoded(4096, false); BallisticsCosmeticWire::SerializeRecord(Encoded, Record);
			FBitWriter Writer(FProjectileCosmeticWireBatch::MaxBytes * 8, false);
			uint8 One = 1; uint16 Bits = uint16(Encoded.GetNumBits());
			Writer.SerializeBits(&One, 8); Writer.SerializeBits(&Bits, 16); Writer.SerializeBits(Encoded.GetData(), Bits);
			FProjectileCosmeticWireBatch Batch; Batch.BitCount = uint16(Writer.GetNumBits()); FMemory::Memcpy(Batch.Data, Writer.GetData(), Writer.GetNumBytes());
			State.Relay->ClientReceiveSequenceBatch(Batch);
			State.Relay->ClientRegisterAudioSource(1, FGuid(10,20,30,40));
		})
		.UntilClient(TEXT("Received record dispatches once with the exact external identity"), 0, [](FState& State)
		{
			const auto& Event = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetLastProcessedEvent();
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It) { return Event.EventID == 777 && Event.AudioSourceID == FGuid(10,20,30,40) && It->GetReceivedRecordCount() == 2 && It->GetSkippedRecordCount() == 0; }
			return false;
		}, FTimespan::FromSeconds(5));
	}

	TEST_METHOD(RelayTeardownReleasesQueuedOwnership)
	{
		Network.ThenServer(TEXT("Warm relay profile"), [](FState& State)
		{
			FActorSpawnParameters Spawn; Spawn.Owner = State.ClientConnections[0]->PlayerController;
			State.Relay = State.World->SpawnActor<AProjectileCosmeticsRelay>(Spawn);
			State.Event.PlaybackMask = uint8(EProjectileCosmeticPlayback::Visual);
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
			State.Relay->SendCosmeticEvents({State.Event});
		})
		.UntilServer(TEXT("Definition ready"), [](FState& State) { return State.Relay->GetAcknowledgedProfileCount() == 1; }, FTimespan::FromSeconds(5))
		.ThenServer(TEXT("Destroy a relay immediately after enqueue under total packet loss"), [](FState& State)
		{
			State.QueueOwner = State.Relay->GetSendQueueForTesting();
			FPacketSimulationSettings Loss; Loss.PktLoss = 100; State.World->GetNetDriver()->SetPacketSimulationSettings(Loss);
			State.Relay->SendCosmeticEvents({State.Event});
			State.Relay->Destroy();
		})
		.UntilServer(TEXT("Native queued owner is released without any delivery ACK"), [](FState& State)
		{
			return State.QueueOwner->Queued.GetValue() == 0;
		}, FTimespan::FromSeconds(5));
	}

	TEST_METHOD(RepresentativeTraffic)
	{
		Network.ThenServer(TEXT("Prepare identity-bearing authored workload"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			State.Source = State.World->SpawnActor<ANPSTestCosmeticLauncher>();
			ASSERT_THAT(IsNotNull(State.Source));
			auto* Profile = LoadObject<UBallisticsLaunchCosmeticProfile>(nullptr, TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"));
			ASSERT_THAT(IsNotNull(Profile));
			State.TargetShots = FParse::Param(FCommandLine::Get(), TEXT("NPSCosmeticWirePeak")) ? 600 : 200;
			State.ShotRate = State.TargetShots == 600 ? 60.f : 20.f;
			State.Event.Location = FVector(1000.f, 0.f, 100.f);
			State.Event.Direction = FVector::ForwardVector;
			State.Event.SourceActor = State.Source;
			State.Event.AudioSourceID = BallisticsCosmeticWire::NewAudioSourceID();
			State.Event.LaunchEffects = Profile->Effects;
			if (!Profile->LaunchSound.IsNull()) { State.Event.Sounds.Add(Profile->LaunchSound); }
			if (!Profile->DistantSound.IsNull()) { State.Event.Sounds.Add(Profile->DistantSound); }
			State.ChannelsPerShot = State.Event.Sounds.IsEmpty() ? 1 : 2;
			State.Event.VisualRadius = State.Event.AudibleRadius = 100000.f;
			State.Event.bUsePropagationDelay = false;
			for (const auto& Effect : Profile->Effects)
			{
				FBallisticsLaunchEffectTransform Transform;
				Transform.Location = State.Event.Location + Effect.LocationOffset;
				Transform.Rotation = Effect.RotationOffset;
				State.Event.LaunchEffectTransforms.Add(Transform);
			}
			State.Event.WireTemplate = BallisticsCosmeticWire::Prepare(State.Event);
		})
		.UntilClients(TEXT("Source exists in each receiving world and local templates are warm"), [](FState& State)
		{
			for (TActorIterator<ANPSTestCosmeticLauncher> It(State.World); It; ++It) { It->Launcher->GetPreparedLaunchCosmeticKey(); return true; }
			return false;
		}, FTimespan::FromSeconds(10))
		.ThenServer(TEXT("Warm profile definitions before measured traffic"), [](FState& State)
		{
			State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->QueueBroadcastEvent(State.Event);
		})
		.UntilServer(TEXT("Initial profiles acknowledged"), [](FState& State)
		{
			int32 Relays = 0;
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It) { ++Relays; if (It->GetAcknowledgedProfileCount() != State.ChannelsPerShot) { return false; } }
			return Relays == 2;
		}, FTimespan::FromSeconds(10))
		.UntilClients(TEXT("Warm shot delivered"), [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetProcessedEventCount() >= State.ChannelsPerShot;
		}, FTimespan::FromSeconds(10))
		.ThenClients(TEXT("Capture client counters and configured outgoing delay"), [this](FState& State)
		{
			ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			State.ProcessedBefore = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetProcessedEventCount();
			State.TargetShots = FParse::Param(FCommandLine::Get(), TEXT("NPSCosmeticWirePeak")) ? 600 : 200;
			auto* Profile = LoadObject<UBallisticsLaunchCosmeticProfile>(nullptr, TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile"));
			State.ChannelsPerShot = Profile->LaunchSound.IsNull() && Profile->DistantSound.IsNull() ? 1 : 2;
			int32 RTT = 0; FParse::Value(FCommandLine::Get(), TEXT("NPSCosmeticWireRTT="), RTT);
			FPacketSimulationSettings Lag; Lag.PktLag = RTT / 2; State.World->GetNetDriver()->SetPacketSimulationSettings(Lag);
		})
		.ThenServer(TEXT("Begin matched steady measurement"), [this](FState& State)
		{
			State.Start = State.World->GetTimeSeconds();
			State.TemplateBuildsBefore = BallisticsCosmeticWire::GetTemplateBuildCount();
			for (auto* Connection : State.ClientConnections) { State.StartBytes.Add(Connection->OutTotalBytes); State.StartInBytes.Add(Connection->InTotalBytes); }
			int32 RTT = 0; FParse::Value(FCommandLine::Get(), TEXT("NPSCosmeticWireRTT="), RTT);
			FPacketSimulationSettings Lag; Lag.PktLag = RTT / 2; State.World->GetNetDriver()->SetPacketSimulationSettings(Lag);
			TestRunner->AddInfo(FString::Printf(TEXT("NPS wire workload shots=%d rate=%.0f effects=%d sounds=%d channels=%u actor=1 RTT=%d"), State.TargetShots, State.ShotRate, State.Event.LaunchEffects.Num(), State.Event.Sounds.Num(), State.ChannelsPerShot, RTT));
		})
		.UntilServer(TEXT("Drive sustained authored traffic"), [](FState& State)
		{
			const double Elapsed = State.World->GetTimeSeconds() - State.Start;
			const int32 Due = FMath::Min(State.TargetShots, FMath::FloorToInt(Elapsed * State.ShotRate));
			auto* Manager = State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager();
			while (State.Shots < Due) { State.Event.EventID = ++State.Shots; State.Event.EmissionServerTime = State.World->GetGameState()->GetServerWorldTimeSeconds(); Manager->QueueBroadcastEvent(State.Event); }
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It) { State.PeakPending = FMath::Max(State.PeakPending, It->GetPendingRecordCount()); State.PeakFlights = FMath::Max(State.PeakFlights, It->GetQueuedUnreliableBatchCount()); }
			return Elapsed >= 10.5;
		}, FTimespan::FromSeconds(30))
		.ThenServer(TEXT("Measure wire and cache counters"), [this](FState& State)
		{
			ASSERT_THAT(AreEqual(State.Shots, State.TargetShots));
			ASSERT_THAT(AreEqual(BallisticsCosmeticWire::GetTemplateBuildCount(), State.TemplateBuildsBefore));
			const double Seconds = State.World->GetTimeSeconds() - State.Start;
			TestRunner->AddInfo(FString::Printf(TEXT("NPS wire throughput shots=%d peak_pending=%u peak_queued=%u template_rebuilds=0"), State.Shots, State.PeakPending, State.PeakFlights));
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It)
			{
				ASSERT_THAT(AreEqual(It->GetProfileAcknowledgementCount(), uint64(State.ChannelsPerShot)));
				ASSERT_THAT(AreEqual(It->GetPendingRecordCount(), uint32(0)));
				ASSERT_THAT(IsTrue(It->GetMaxUnreliableSerializedBits() <= FProjectileCosmeticWireBatch::MaxUnreliableSerializedBits));
				TestRunner->AddInfo(FString::Printf(TEXT("NPS wire max_unreliable_parameter_bits=%u profiles=%u pending=%u profile_ACKs=%llu"), It->GetMaxUnreliableSerializedBits(), It->GetRegisteredProfileCount(), It->GetPendingRecordCount(), It->GetProfileAcknowledgementCount()));
			}
			for (int32 Index = 0; Index < State.ClientConnections.Num(); ++Index)
			{
				const int32 Out = State.ClientConnections[Index]->OutTotalBytes - State.StartBytes[Index];
				const int32 In = State.ClientConnections[Index]->InTotalBytes - State.StartInBytes[Index];
				TestRunner->AddInfo(FString::Printf(TEXT("NPS wire traffic client=%d bytes=%d seconds=%.3f out_KBps=%.3f in_bytes=%d in_KBps=%.3f"), Index, Out, Seconds, Out/(1024.0*Seconds), In, In/(1024.0*Seconds)));
				TestRunner->AddInfo(FString::Printf(TEXT("NPS wire latency client=%d outgoing_lag_ms=%d measured_rtt_ms=%.1f"), Index, State.ClientConnections[Index]->PacketSimulationSettings.PktLag, 1000.f * State.ClientConnections[Index]->AvgLag));
			}
		})
		.UntilClients(TEXT("Every zero-loss event dispatches"), [](FState& State)
		{
			return State.World->GetSubsystem<UProjectileBallisticsSubsystem>()->GetCosmeticsManager()->GetProcessedEventCount() == State.ProcessedBefore + State.TargetShots * State.ChannelsPerShot;
		}, FTimespan::FromSeconds(25))
		.ThenClients(TEXT("Report actual receive counters"), [this](FState& State)
		{
			for (TActorIterator<AProjectileCosmeticsRelay> It(State.World); It; ++It)
			{
				TestRunner->AddInfo(FString::Printf(TEXT("NPS wire received world=%s records=%llu skipped=%llu return_lag_ms=%d"), *State.World->GetName(), It->GetReceivedRecordCount() - State.ChannelsPerShot, It->GetSkippedRecordCount(), State.World->GetNetDriver()->ServerConnection->PacketSimulationSettings.PktLag));
			}
		})
		.ThenServer(TEXT("Report final drain time"), [this](FState& State) { TestRunner->AddInfo(FString::Printf(TEXT("NPS wire final_seconds=%.3f"), State.World->GetTimeSeconds()-State.Start)); });
	}
};
#endif
