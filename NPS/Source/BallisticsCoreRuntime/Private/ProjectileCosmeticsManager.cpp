// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileCosmeticsManager.h"

#include "AudioDevice.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsDebugDraw.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsImpactBudget.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileLauncherComponent.h"
#include "ProjectileCosmeticsRelay.h"
#include "BallisticsCosmeticWire.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/AudioSettings.h"

namespace
{
	constexpr float SequenceEndTombstoneLifetime = 31.f;
	constexpr int32 MaxSequenceEndTombstones = 8192;

	float SanitizeCosmeticScalar(float Value, float Minimum, float Maximum, float Fallback)
	{
		return FMath::IsFinite(Value)
			? FMath::Clamp(Value, Minimum, Maximum)
			: Fallback;
	}

	const TArray<FBallisticsCosmeticBoolParameter>& EmptyBoolParameters()
	{
		static const TArray<FBallisticsCosmeticBoolParameter> Empty;
		return Empty;
	}

	const TArray<FBallisticsCosmeticFloatParameter>& EmptyFloatParameters()
	{
		static const TArray<FBallisticsCosmeticFloatParameter> Empty;
		return Empty;
	}

	bool HasPlayback(
		const FProjectileCosmeticEvent& Event,
		EProjectileCosmeticPlayback Playback)
	{
		return (Event.PlaybackMask & static_cast<uint8>(Playback)) != 0;
	}

	bool HasVisualPayload(const FProjectileCosmeticEvent& Event)
	{
		return Event.LaunchEffects.Num() > 0 || !Event.Effect.IsNull() ||
			!Event.DecalMaterial.IsNull() ||
			Event.Type == EProjectileCosmeticEventType::SequenceTransform;
	}

	bool HasAudioPayload(const FProjectileCosmeticEvent& Event)
	{
		return Event.Sounds.Num() > 0;
	}
}

void UProjectileCosmeticsManager::Initialize(UWorld* InWorld)
{
	CachedWorld = InWorld;
}

void UProjectileCosmeticsManager::Shutdown()
{
	for (FActiveCosmeticEffect& Effect : ActiveCosmeticEffects)
	{
		if (UNiagaraComponent* Component = Effect.Component.Get())
		{
			Component->DeactivateImmediate();
			Component->DestroyComponent();
		}
	}
	ListenerQueues.Empty();
	Relays.Empty();
	ActiveSequences.Empty();
	EndedSequenceTombstones.Empty();
	SequenceRecipients.Empty();
	PendingRemoteDeliveries.Empty();
	PendingAudioEvents.Empty();
	ActiveCosmeticEffects.Empty();
	AudioConcurrencyGroups.Empty();
	ProcessedEventCount = 0;
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	SpawnedEffectCount = 0;
	SpawnedEffectCounts.Empty();
	SpawnedDecalCount = 0;
#endif
	RoutedDetonationCount = 0;
	NextRoutedEventID = 1;
	CachedWorld.Reset();
}

UWorld* UProjectileCosmeticsManager::GetWorld() const
{
	return CachedWorld.Get();
}

void UProjectileCosmeticsManager::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() != NM_Client)
	{
		TickPendingRemoteDeliveries();
		FlushServerQueues();
	}
	if (World->GetNetMode() != NM_DedicatedServer && ActiveSequences.Num() > 0)
	{
		TickActiveSequences(DeltaTime);
	}

	if (World->GetNetMode() != NM_DedicatedServer && EndedSequenceTombstones.Num() > 0)
	{
		TickSequenceTombstones(DeltaTime);
	}

	if (World->GetNetMode() != NM_DedicatedServer && PendingAudioEvents.Num() > 0)
	{
		TickPendingAudio(DeltaTime);
	}

	if (World->GetNetMode() != NM_DedicatedServer && ActiveCosmeticEffects.Num() > 0)
	{
		TickActiveCosmeticEffects(DeltaTime);
	}

	for (auto SourceIt = AudioConcurrencyGroups.CreateIterator(); SourceIt; ++SourceIt)
	{
		for (auto GroupIt = SourceIt.Value().CreateIterator(); GroupIt; ++GroupIt)
		{
			if (!GroupIt.Value().IsValid())
			{
				GroupIt.RemoveCurrent();
			}
		}
		if (SourceIt.Value().IsEmpty())
		{
			SourceIt.RemoveCurrent();
		}
	}
}

void UProjectileCosmeticsManager::QueueBroadcastEvent(
	const FProjectileCosmeticEvent& Event,
	APlayerController* ExcludedController)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual) &&
		(HasVisualPayload(Event) || Event.VisualRadius > 0.f))
	{
		RouteEventChannel(Event, EProjectileCosmeticPlayback::Visual, ExcludedController);
	}
	if (HasPlayback(Event, EProjectileCosmeticPlayback::Audio) && HasAudioPayload(Event))
	{
		RouteEventChannel(Event, EProjectileCosmeticPlayback::Audio, ExcludedController);
	}
}

void UProjectileCosmeticsManager::RouteEventChannel(
	const FProjectileCosmeticEvent& Event,
	EProjectileCosmeticPlayback Channel,
	APlayerController* ExcludedController)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float RequestedRadius = Channel == EProjectileCosmeticPlayback::Audio
		? Event.AudibleRadius
		: Event.VisualRadius;
	const float SafeRadius = FMath::Clamp(
		FMath::IsFinite(RequestedRadius) ? RequestedRadius : 0.f,
		0.f,
		GetRoutingRadiusLimit());
	if (SafeRadius <= 0.f)
	{
		return;
	}

	const float RadiusSq = FMath::Square(SafeRadius);
	const auto PreparedChannel = BallisticsCosmeticWire::ForChannel(Event, static_cast<uint8>(Channel));
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!Controller || Controller == ExcludedController)
		{
			continue;
		}
		FVector ListenerLocation;
		if (!TryGetListenerLocation(Controller, ListenerLocation) ||
			FVector::DistSquared(ListenerLocation, Event.Location) > RadiusSq)
		{
			continue;
		}

		FProjectileCosmeticEvent ChannelEvent = Event;
		ChannelEvent.WireTemplate = PreparedChannel;
		ChannelEvent.PlaybackMask = static_cast<uint8>(Channel);
		if (Channel == EProjectileCosmeticPlayback::Audio)
		{
			ChannelEvent.SourceActor = nullptr;
			ChannelEvent.LaunchEffects.Reset();
			ChannelEvent.LaunchEffectTransforms.Reset();
			ChannelEvent.Effect.Reset();
			ChannelEvent.DecalMaterial.Reset();
			ChannelEvent.bServerDelayedAudio = true;
			QueueAcousticDelivery(Controller, &ChannelEvent, nullptr, ListenerLocation);
		}
		else
		{
			ChannelEvent.Sounds.Reset();
			QueueForController(Controller, &ChannelEvent, nullptr);
		}
	}
}

void UProjectileCosmeticsManager::QueueTargetedEvent(
	const FProjectileCosmeticEvent& Event,
	APlayerController* TargetController)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !TargetController)
	{
		return;
	}

	FProjectileCosmeticEvent Targeted = Event;
	Targeted.WireTemplate = BallisticsCosmeticWire::ForChannel(Event, static_cast<uint8>(EProjectileCosmeticPlayback::Audio));
	Targeted.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
	Targeted.bServerDelayedAudio = true;
	Targeted.SourceActor = nullptr;
	Targeted.LaunchEffects.Reset();
	Targeted.LaunchEffectTransforms.Reset();
	Targeted.Effect.Reset();
	Targeted.DecalMaterial.Reset();
	QueueForController(TargetController, &Targeted, nullptr);
}

void UProjectileCosmeticsManager::RouteDetonation(
	const FVector& Location,
	UBallisticsExplosiveDataAsset* ExplosiveData)
{
	UWorld* World = GetWorld();
	if (!World || !ExplosiveData ||
		!ExplosiveData->DetonationCosmetic.HasAnyAsset())
	{
		return;
	}
	++RoutedDetonationCount;

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Detonation;
	Event.EventID = NextRoutedEventID++;
	Event.Location = Location;
	Event.Direction = FVector::UpVector;
	Event.EmissionServerTime = GetSynchronizedTime();
	const FBallisticsDetonationCosmetic& Cosmetic = ExplosiveData->DetonationCosmetic;
	Event.Effect = Cosmetic.Effect;
	if (!Cosmetic.Sound.IsNull())
	{
		Event.Sounds.Add(Cosmetic.Sound);
	}
	Event.EffectScale = Cosmetic.EffectScale;
	Event.EffectLifeSpan = Cosmetic.EffectLifeSpan;
	Event.VolumeMultiplier = Cosmetic.VolumeMultiplier;
	Event.PitchMultiplier = Cosmetic.PitchMultiplier;
	Event.VisualRadius = Cosmetic.VisualRadius;
	Event.AudibleRadius = Cosmetic.AudibleRadius;
	Event.PropagationSpeed = Cosmetic.PropagationSpeed;
	Event.bUsePropagationDelay = Cosmetic.bUsePropagationDelay;

	if (World->GetNetMode() == NM_Standalone)
	{
		PlayLocalEvent(Event);
		return;
	}
	if (World->GetNetMode() == NM_Client)
	{
		PlayLocalEvent(Event);
		return;
	}
	if (World->GetNetMode() != NM_Client)
	{
		QueueBroadcastEvent(Event);
	}
}

void UProjectileCosmeticsManager::QueueSequenceCommand(
	const FLaunchSequenceCommand& Command,
	APlayerController* ExcludedController)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (Command.Type == ELaunchSequenceCommandType::End)
	{
		for (const EProjectileCosmeticPlayback Channel :
			{ EProjectileCosmeticPlayback::Visual, EProjectileCosmeticPlayback::Audio })
		{
			const uint8 Mask = static_cast<uint8>(Channel);
		const auto PreparedChannel = Command.Type == ELaunchSequenceCommandType::End ? TSharedPtr<const FProjectileCosmeticStaticTemplate>() : BallisticsCosmeticWire::ForChannel(Command.LaunchEvent, Mask);
			const uint64 Key = MakeSequenceChannelKey(Command.SequenceID, Mask);
			if (TSet<TWeakObjectPtr<APlayerController>>* Recipients =
				SequenceRecipients.Find(Key))
			{
				for (const TWeakObjectPtr<APlayerController>& WeakController : *Recipients)
				{
					APlayerController* Controller = WeakController.Get();
					if (!Controller)
					{
						continue;
					}
					FLaunchSequenceCommand ChannelCommand = Command;
			ChannelCommand.LaunchEvent.WireTemplate = PreparedChannel;
					ChannelCommand.LaunchEvent.PlaybackMask = Mask;
					if (Channel == EProjectileCosmeticPlayback::Audio)
					{
						ChannelCommand.LaunchEvent.SourceActor = nullptr;
						ChannelCommand.LaunchEvent.LaunchEffects.Reset();
						ChannelCommand.LaunchEvent.LaunchEffectTransforms.Reset();
						ChannelCommand.LaunchEvent.Effect.Reset();
						ChannelCommand.LaunchEvent.DecalMaterial.Reset();
						ChannelCommand.LaunchEvent.bServerDelayedAudio = true;
						FVector ListenerLocation;
						if (TryGetListenerLocation(Controller, ListenerLocation))
						{
							QueueAcousticDelivery(
								Controller,
								nullptr,
								&ChannelCommand,
								ListenerLocation);
						}
					}
					else
					{
						ChannelCommand.LaunchEvent.Sounds.Reset();
						QueueForController(Controller, nullptr, &ChannelCommand);
					}
				}
				SequenceRecipients.Remove(Key);
			}
		}
		return;
	}

	for (const EProjectileCosmeticPlayback Channel :
		{ EProjectileCosmeticPlayback::Visual, EProjectileCosmeticPlayback::Audio })
	{
		if ((Channel == EProjectileCosmeticPlayback::Visual &&
			!HasVisualPayload(Command.LaunchEvent)) ||
			(Channel == EProjectileCosmeticPlayback::Audio &&
			!HasAudioPayload(Command.LaunchEvent)))
		{
			continue;
		}
		const float RequestedRadius = Channel == EProjectileCosmeticPlayback::Audio
			? Command.LaunchEvent.AudibleRadius
			: Command.LaunchEvent.VisualRadius;
		const float SafeRadius = FMath::Clamp(
			FMath::IsFinite(RequestedRadius) ? RequestedRadius : 0.f,
			0.f,
			GetRoutingRadiusLimit());
		if (SafeRadius <= 0.f)
		{
			continue;
		}
		const float RadiusSq = FMath::Square(SafeRadius);
		const uint8 Mask = static_cast<uint8>(Channel);
		const auto PreparedChannel = Command.Type == ELaunchSequenceCommandType::End ? TSharedPtr<const FProjectileCosmeticStaticTemplate>() : BallisticsCosmeticWire::ForChannel(Command.LaunchEvent, Mask);
		const uint64 Key = MakeSequenceChannelKey(Command.SequenceID, Mask);
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller || Controller == ExcludedController)
			{
				continue;
			}
			FVector ListenerLocation;
			if (!TryGetListenerLocation(Controller, ListenerLocation) ||
				FVector::DistSquared(ListenerLocation, Command.LaunchEvent.Location) > RadiusSq)
			{
				continue;
			}

			FLaunchSequenceCommand ChannelCommand = Command;
			ChannelCommand.LaunchEvent.WireTemplate = PreparedChannel;
			ChannelCommand.LaunchEvent.PlaybackMask = Mask;
			if (Channel == EProjectileCosmeticPlayback::Audio)
			{
				ChannelCommand.LaunchEvent.SourceActor = nullptr;
				ChannelCommand.LaunchEvent.LaunchEffects.Reset();
				ChannelCommand.LaunchEvent.LaunchEffectTransforms.Reset();
				ChannelCommand.LaunchEvent.Effect.Reset();
				ChannelCommand.LaunchEvent.DecalMaterial.Reset();
				ChannelCommand.LaunchEvent.bServerDelayedAudio = true;
				QueueAcousticDelivery(
					Controller,
					nullptr,
					&ChannelCommand,
					ListenerLocation);
			}
			else
			{
				ChannelCommand.LaunchEvent.Sounds.Reset();
				QueueForController(Controller, nullptr, &ChannelCommand);
			}
			SequenceRecipients.FindOrAdd(Key).Add(Controller);
		}
	}
}

void UProjectileCosmeticsManager::QueueForController(
	APlayerController* Controller,
	const FProjectileCosmeticEvent* Event,
	const FLaunchSequenceCommand* Command)
{
	if (!Controller)
	{
		return;
	}

	// Listen-server and standalone local controllers share this world. Avoid an
	// RPC and feed their local playback state directly.
	if (Controller->IsLocalController())
	{
		if (Event)
		{
			PlayEvent(*Event);
		}
		if (Command)
		{
			TArray<FLaunchSequenceCommand> LocalCommands;
			LocalCommands.Add(*Command);
			ReceiveSequenceBatch(LocalCommands);
		}
		return;
	}

	FListenerQueue& Queue = ListenerQueues.FindOrAdd(Controller);
	if (Event)
	{
		const int32 Budget = GetPerConnectionBudget();
		if (Queue.UnreliableEvents.Num() < Budget)
		{
			Queue.UnreliableEvents.Add(*Event);
		}
		else if (LastBudgetWarningFrame != GFrameCounter)
		{
			LastBudgetWarningFrame = GFrameCounter;
			NPS_LOG(Warning,
				TEXT("NPS cosmetics dropped events at the per-connection frame budget (%d)."),
				Budget);
		}
	}

	if (Command)
	{
		const int32 StateBudget = FMath::Clamp(
			FMath::Max(GetPerConnectionBudget(), GetSequenceBudget() * 2),
			1,
			256);
		if (Queue.ReliableCommands.Num() >= StateBudget &&
			Command->Type == ELaunchSequenceCommandType::End)
		{
			const int32 BeginIndex = Queue.ReliableCommands.IndexOfByPredicate(
				[](const FLaunchSequenceCommand& QueuedCommand)
				{
					return QueuedCommand.Type == ELaunchSequenceCommandType::Begin;
				});
			if (BeginIndex != INDEX_NONE)
			{
				Queue.ReliableCommands.RemoveAt(BeginIndex, 1, EAllowShrinking::No);
			}
		}

		if (Queue.ReliableCommands.Num() < StateBudget)
		{
			Queue.ReliableCommands.Add(*Command);
		}
	}

	GetOrCreateRelay(Controller);
}

uint64 UProjectileCosmeticsManager::MakeSequenceChannelKey(
	int32 SequenceID,
	uint8 PlaybackMask)
{
	return (static_cast<uint64>(static_cast<uint32>(SequenceID)) << 8) |
		static_cast<uint64>(PlaybackMask);
}

void UProjectileCosmeticsManager::QueueAcousticDelivery(
	APlayerController* Controller,
	const FProjectileCosmeticEvent* Event,
	const FLaunchSequenceCommand* Command,
	const FVector& ListenerLocation)
{
	if (!Controller || (!Event && !Command))
	{
		return;
	}

	const FProjectileCosmeticEvent& SourceEvent = Event
		? *Event
		: Command->LaunchEvent;
	const float Now = GetSynchronizedTime();
	float DueTime = Now;
	if (SourceEvent.bUsePropagationDelay &&
		FMath::IsFinite(SourceEvent.PropagationSpeed) &&
		SourceEvent.PropagationSpeed > 1.f)
	{
		const float TravelTime = FVector::Distance(
			ListenerLocation,
			SourceEvent.Location) / SourceEvent.PropagationSpeed;
		const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
		const float MaxDelay = Settings
			? FMath::Clamp(Settings->MaxCosmeticAudioDelay, 0.f, 30.f)
			: 15.f;
		if (!FMath::IsFinite(TravelTime) || TravelTime > MaxDelay)
		{
			return;
		}
		const float EmissionTime = SourceEvent.EmissionServerTime > 0.f
			? SourceEvent.EmissionServerTime
			: Now;
		DueTime = EmissionTime + TravelTime;
	}

	if (Command && Command->Type == ELaunchSequenceCommandType::End)
	{
		for (const FPendingRemoteDelivery& Pending : PendingRemoteDeliveries)
		{
			if (Pending.Controller == Controller && Pending.bSequenceCommand &&
				Pending.Command.Type == ELaunchSequenceCommandType::Begin &&
				Pending.Command.SequenceID == Command->SequenceID &&
				Pending.Command.LaunchEvent.PlaybackMask ==
					Command->LaunchEvent.PlaybackMask)
			{
				DueTime = FMath::Max(
					DueTime,
					static_cast<float>(Pending.DueServerTime + KINDA_SMALL_NUMBER));
			}
		}
	}

	if (DueTime <= Now + KINDA_SMALL_NUMBER)
	{
		QueueForController(Controller, Event, Command);
		return;
	}

	if (PendingRemoteDeliveries.Num() >= GetPendingAudioEventBudget())
	{
		if (Command && Command->Type == ELaunchSequenceCommandType::End)
		{
			int32 EvictionIndex = INDEX_NONE;
			for (int32 Index = 0; Index < PendingRemoteDeliveries.Num(); ++Index)
			{
				const FPendingRemoteDelivery& Pending = PendingRemoteDeliveries[Index];
				if (!Pending.bSequenceCommand ||
					Pending.Command.Type != ELaunchSequenceCommandType::End)
				{
					EvictionIndex = Index;
					break;
				}
			}

			if (EvictionIndex != INDEX_NONE)
			{
				PendingRemoteDeliveries.HeapRemoveAt(
					EvictionIndex,
					EAllowShrinking::No);
			}
			else
			{
				QueueForController(Controller, nullptr, Command);
				return;
			}
		}
		else
		{
			if (LastAudioBudgetWarningFrame != GFrameCounter)
			{
				LastAudioBudgetWarningFrame = GFrameCounter;
				NPS_LOG(Warning,
					TEXT("NPS dropped remote acoustic delivery at the aggregate queue budget (%d)."),
					GetPendingAudioEventBudget());
			}
			return;
		}
	}

	FPendingRemoteDelivery Pending;
	Pending.DueServerTime = DueTime;
	Pending.StableOrder = NextRemoteDeliveryOrder++;
	Pending.Controller = Controller;
	Pending.bSequenceCommand = Command != nullptr;
	if (Event)
	{
		Pending.Event = *Event;
	}
	else
	{
		Pending.Command = *Command;
	}
	PendingRemoteDeliveries.HeapPush(MoveTemp(Pending));
}

void UProjectileCosmeticsManager::TickPendingRemoteDeliveries()
{
	const double Now = GetSynchronizedTime();
	while (PendingRemoteDeliveries.Num() > 0 &&
		PendingRemoteDeliveries.HeapTop().DueServerTime <= Now)
	{
		FPendingRemoteDelivery Ready;
		PendingRemoteDeliveries.HeapPop(Ready, EAllowShrinking::No);
		APlayerController* Controller = Ready.Controller.Get();
		if (!Controller)
		{
			continue;
		}
		if (Ready.bSequenceCommand)
		{
			QueueForController(Controller, nullptr, &Ready.Command);
		}
		else
		{
			QueueForController(Controller, &Ready.Event, nullptr);
		}
	}
}

AProjectileCosmeticsRelay* UProjectileCosmeticsManager::GetOrCreateRelay(
	APlayerController* Controller)
{
	if (!Controller)
	{
		return nullptr;
	}

	if (FRelayEntry* Existing = Relays.Find(Controller))
	{
		if (AProjectileCosmeticsRelay* Relay = Existing->Relay.Get())
		{
			return Relay;
		}
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = Controller;
	Params.Instigator = Controller->GetPawn();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	AProjectileCosmeticsRelay* Relay = World->SpawnActor<AProjectileCosmeticsRelay>(
		AProjectileCosmeticsRelay::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		Params);

	if (Relay)
	{
		FRelayEntry& Entry = Relays.FindOrAdd(Controller);
		Entry.Relay = Relay;
		Entry.CreatedFrame = GFrameCounter;
		Relay->ForceNetUpdate();
	}
	return Relay;
}

void UProjectileCosmeticsManager::FlushServerQueues()
{
	for (auto& Pair : Relays)
	{
		if (AProjectileCosmeticsRelay* Relay = Pair.Value.Relay.Get()) { Relay->FlushCosmeticQueue(); }
	}
	if (ListenerQueues.Num() == 0)
	{
		return;
	}

	for (auto It = ListenerQueues.CreateIterator(); It; ++It)
	{
		APlayerController* Controller = It.Key().Get();
		if (!Controller)
		{
			It.RemoveCurrent();
			continue;
		}

		AProjectileCosmeticsRelay* Relay = GetOrCreateRelay(Controller);
		FRelayEntry* RelayEntry = Relays.Find(Controller);
		if (!Relay || !RelayEntry || RelayEntry->CreatedFrame >= GFrameCounter)
		{
			// An unreliable RPC in the actor's spawn frame can be discarded
			// before its channel exists. Preserve the batch until next frame.
			continue;
		}

		FListenerQueue& Queue = It.Value();
		if (Queue.ReliableCommands.Num() > 0)
		{
			Relay->SendSequenceCommands(Queue.ReliableCommands);
			Queue.ReliableCommands.Reset();
		}
		if (Queue.UnreliableEvents.Num() > 0)
		{
			Relay->SendCosmeticEvents(Queue.UnreliableEvents);
			Queue.UnreliableEvents.Reset();
		}

		if (Queue.ReliableCommands.Num() == 0 && Queue.UnreliableEvents.Num() == 0)
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = Relays.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().Relay.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UProjectileCosmeticsManager::ReceiveCosmeticBatch(
	const TArray<FProjectileCosmeticEvent>& Events)
{
	const int32 Budget = GetPerConnectionBudget();
	const int32 Count = FMath::Min(Events.Num(), Budget);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FProjectileCosmeticEvent& Event = Events[Index];
		if (Event.Type == EProjectileCosmeticEventType::SequenceTransform)
		{
			const uint64 Key = MakeSequenceChannelKey(
				Event.SequenceID,
				Event.PlaybackMask);
			if (FActiveSequence* Sequence = ActiveSequences.Find(Key))
			{
				Sequence->Event.Location = Event.Location;
				Sequence->Event.Direction = Event.Direction;
				Sequence->Event.LaunchEffectTransforms =
					Event.LaunchEffectTransforms;
				Sequence->TimeSinceSignal = 0.f;
			}
			continue;
		}
		PlayEvent(Event);
	}
}

void UProjectileCosmeticsManager::ReceiveSequenceBatch(
	const TArray<FLaunchSequenceCommand>& Commands)
{
	for (const FLaunchSequenceCommand& Command : Commands)
	{
		if (Command.SequenceID == INDEX_NONE)
		{
			continue;
		}

		if (Command.Type == ELaunchSequenceCommandType::End)
		{
			const uint64 Key = MakeSequenceChannelKey(
				Command.SequenceID,
				Command.LaunchEvent.PlaybackMask);
			ActiveSequences.Remove(Key);
			if (EndedSequenceTombstones.Num() >= MaxSequenceEndTombstones &&
				!EndedSequenceTombstones.Contains(Key))
			{
				uint64 OldestKey = 0;
				float LowestRemaining = MAX_flt;
				for (const TPair<uint64, float>& Tombstone : EndedSequenceTombstones)
				{
					if (Tombstone.Value < LowestRemaining)
					{
						OldestKey = Tombstone.Key;
						LowestRemaining = Tombstone.Value;
					}
				}
				EndedSequenceTombstones.Remove(OldestKey);
			}
			EndedSequenceTombstones.Add(Key, SequenceEndTombstoneLifetime);
			continue;
		}
		const uint64 Key = MakeSequenceChannelKey(
			Command.SequenceID,
			Command.LaunchEvent.PlaybackMask);
		if (EndedSequenceTombstones.Contains(Key))
		{
			continue;
		}

		if (!ActiveSequences.Contains(Key) &&
			ActiveSequences.Num() >= GetSequenceBudget())
		{
			NPS_LOG(Warning,
				TEXT("NPS rejected sustained launch sequence %d: active-sequence budget reached."),
				Command.SequenceID);
			continue;
		}

		const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
		const float MaxRate = Settings
			? FMath::Clamp(Settings->MaxLaunchCosmeticRate, 1.f, 120.f)
			: 60.f;
		const float Rate = FMath::Clamp(Command.LaunchesPerSecond, 0.1f, MaxRate);

		FActiveSequence& Sequence = ActiveSequences.FindOrAdd(Key);
		Sequence.Event = Command.LaunchEvent;
		Sequence.Event.SequenceID = Command.SequenceID;
		Sequence.Interval = 1.f / Rate;
		Sequence.TimeUntilNext = Sequence.Interval;
		Sequence.TimeSinceSignal = 0.f;

		// Begin represents the first remote launch; do not wait one interval.
		PlayEvent(Sequence.Event);
	}
}

void UProjectileCosmeticsManager::TickActiveSequences(float DeltaTime)
{
	const int32 PerFrameBudget = GetPerConnectionBudget();
	int32 PlayedThisFrame = 0;

	for (auto It = ActiveSequences.CreateIterator(); It; ++It)
	{
		FActiveSequence& Sequence = It.Value();
		Sequence.TimeSinceSignal += FMath::Max(0.f, DeltaTime);
		const float SilenceLimit = FMath::Clamp(
			FMath::Max(1.f, Sequence.Interval * 2.f),
			1.f,
			30.f);
		if (Sequence.TimeSinceSignal > SilenceLimit)
		{
			NPS_LOG(Warning,
				TEXT("NPS stopped sustained launch sequence %d after %.2f seconds without an update."),
				Sequence.Event.SequenceID,
				SilenceLimit);
			It.RemoveCurrent();
			continue;
		}
		Sequence.TimeUntilNext -= FMath::Max(0.f, DeltaTime);

		// A local hitch must not create an unbounded catch-up burst.
		int32 CatchUpCount = 0;
		while (Sequence.TimeUntilNext <= 0.f &&
			CatchUpCount < 4 &&
			PlayedThisFrame < PerFrameBudget)
		{
			PlayEvent(Sequence.Event);
			Sequence.TimeUntilNext += Sequence.Interval;
			++CatchUpCount;
			++PlayedThisFrame;
		}

		if (Sequence.TimeUntilNext <= -Sequence.Interval * 4.f)
		{
			Sequence.TimeUntilNext = 0.f;
		}
	}
}

void UProjectileCosmeticsManager::PlayLocalEvent(
	const FProjectileCosmeticEvent& Event)
{
	if (UWorld* World = GetWorld())
	{
		if (World->GetNetMode() != NM_DedicatedServer)
		{
			PlayEvent(Event);
		}
	}
}

void UProjectileCosmeticsManager::PlayEvent(const FProjectileCosmeticEvent& Event)
{
	if (Event.Type == EProjectileCosmeticEventType::SequenceTransform)
	{
		return;
	}
	if (!TryConsumeLocalEventBudget())
	{
		return;
	}

	FProjectileCosmeticEvent PlaybackEvent = Event;
	if (PlaybackEvent.Type == EProjectileCosmeticEventType::Launch)
	{
		ResolveLocalLaunchEffectTransforms(PlaybackEvent);
	}

	OnCosmeticEvent.Broadcast(PlaybackEvent);
	LastProcessedEvent = PlaybackEvent;
	++ProcessedEventCount;

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	if (Settings && !Settings->bEnableBuiltInCosmetics)
	{
		return;
	}

	switch (PlaybackEvent.Type)
	{
	case EProjectileCosmeticEventType::Launch:
		PlayLaunch(PlaybackEvent);
		break;
	case EProjectileCosmeticEventType::Impact:
		PlayImpact(PlaybackEvent);
		break;
	case EProjectileCosmeticEventType::FlyBy:
		PlayFlyBy(PlaybackEvent);
		break;
	case EProjectileCosmeticEventType::Detonation:
		PlayDetonation(PlaybackEvent);
		break;
	default:
		break;
	}
}

void UProjectileCosmeticsManager::ResolveLocalLaunchEffectTransforms(
	FProjectileCosmeticEvent& Event) const
{
	if (Event.LaunchEffects.Num() == 0)
	{
		return;
	}

	UProjectileLauncherComponent* SourceLauncher = Event.SourceActor
		? Event.SourceActor->FindComponentByClass<UProjectileLauncherComponent>()
		: nullptr;
	if (!SourceLauncher)
	{
		return;
	}

	const int32 EffectCount = FMath::Min(Event.LaunchEffects.Num(), 16);
	Event.LaunchEffectTransforms.SetNum(EffectCount);
	for (int32 EffectIndex = 0; EffectIndex < EffectCount; ++EffectIndex)
	{
		const FBallisticsLaunchEffect& Effect = Event.LaunchEffects[EffectIndex];
		FBallisticsLaunchEffectTransform& EffectTransform =
			Event.LaunchEffectTransforms[EffectIndex];

		FTransform BaseTransform(Event.Direction.Rotation(), Event.Location);
		if (!Effect.SocketName.IsNone() &&
			!SourceLauncher->ResolveLaunchSourceTransform(
				Effect.SourceBinding,
				Effect.SocketName,
				BaseTransform))
		{
			EffectTransform.bValid = false;
			continue;
		}

		const FVector Location = BaseTransform.TransformPosition(Effect.LocationOffset);
		const FQuat Rotation =
			BaseTransform.GetRotation() * Effect.RotationOffset.Quaternion();
		EffectTransform.Location = Location;
		EffectTransform.Rotation = Rotation.Rotator();
		EffectTransform.bValid = true;
	}
}

void UProjectileCosmeticsManager::TickSequenceTombstones(float DeltaTime)
{
	const float SafeDeltaTime = FMath::Max(0.f, DeltaTime);
	for (auto It = EndedSequenceTombstones.CreateIterator(); It; ++It)
	{
		It.Value() -= SafeDeltaTime;
		if (It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}
}

void UProjectileCosmeticsManager::PlayLaunch(const FProjectileCosmeticEvent& Event)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual))
	{
		const int32 EffectCount = FMath::Min(
			Event.LaunchEffects.Num(),
			Event.LaunchEffectTransforms.Num());
		for (int32 EffectIndex = 0; EffectIndex < EffectCount; ++EffectIndex)
		{
			const FBallisticsLaunchEffect& LaunchEffect = Event.LaunchEffects[EffectIndex];
			const FBallisticsLaunchEffectTransform& Transform =
				Event.LaunchEffectTransforms[EffectIndex];
			if (Transform.bValid)
			{
				if (UNiagaraSystem* Effect = LaunchEffect.Effect.LoadSynchronous())
				{
					SpawnEffect(
						Effect,
						Transform.Location,
						Transform.Rotation,
						LaunchEffect.Scale,
						LaunchEffect.LifeSpan,
						&Event.BoolParameters,
						&Event.FloatParameters,
						&LaunchEffect);
				}
			}
		}
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Audio))
	{
		for (const TSoftObjectPtr<USoundBase>& Sound : Event.Sounds)
		{
			ScheduleSound(
				Sound,
				Event,
				Event.VolumeMultiplier,
				Event.PitchMultiplier,
				Event.PropagationSpeed,
				Event.bUsePropagationDelay,
				&Event.BoolParameters,
				&Event.FloatParameters);
		}
	}
}

void UProjectileCosmeticsManager::PlayImpact(const FProjectileCosmeticEvent& Event)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	EImpactBudgetResult BudgetResult = EImpactBudgetResult::Allowed;
	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual))
	{
		if (UProjectileBallisticsSubsystem* Subsystem =
			World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			if (UBallisticsImpactBudget* Budget = Subsystem->GetImpactBudget())
			{
				FImpactBudgetRequest Request;
				Request.ImpactLocation = Event.Location;
				Request.ImpactNormal = Event.Direction;
				Request.ImpactResponse = static_cast<EImpactResponse>(Event.ImpactResponse);
				Request.SurfaceType = Event.SurfaceType;
				Request.ProjectileID = Event.EventID;
				BudgetResult = Budget->SubmitImpact(Request);
			}
		}
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual) &&
		BudgetResult != EImpactBudgetResult::Denied && !Event.Effect.IsNull())
	{
		if (UNiagaraSystem* Effect = Event.Effect.LoadSynchronous())
		{
			SpawnEffect(
				Effect,
				Event.Location,
				Event.Direction.Rotation(),
				Event.EffectScale,
				Event.EffectLifeSpan,
				&Event.BoolParameters,
				&Event.FloatParameters);
		}
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual) &&
		BudgetResult == EImpactBudgetResult::Allowed && !Event.DecalMaterial.IsNull())
	{
		if (UMaterialInterface* Material = Event.DecalMaterial.LoadSynchronous())
		{
			if (UGameplayStatics::SpawnDecalAtLocation(
				World,
				Material,
				Event.DecalSize.ComponentMax(FVector::ZeroVector).ComponentMin(
					FVector(10000.f)),
				Event.Location,
				Event.Direction.Rotation(),
				SanitizeCosmeticScalar(
					Event.DecalLifeSpan,
					0.f,
					300.f,
					10.f)))
			{
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
				++SpawnedDecalCount;
#endif
			}
		}
	}

	if (HasPlayback(Event, EProjectileCosmeticPlayback::Audio))
	{
		for (const TSoftObjectPtr<USoundBase>& Sound : Event.Sounds)
		{
			ScheduleSound(Sound, Event, Event.VolumeMultiplier, Event.PitchMultiplier,
				Event.PropagationSpeed, Event.bUsePropagationDelay,
				&Event.BoolParameters, &Event.FloatParameters);
		}
	}
}

void UProjectileCosmeticsManager::PlayFlyBy(const FProjectileCosmeticEvent& Event)
{
	if (!HasPlayback(Event, EProjectileCosmeticPlayback::Audio))
	{
		return;
	}
	for (const TSoftObjectPtr<USoundBase>& Sound : Event.Sounds)
	{
		ScheduleSound(Sound, Event, Event.VolumeMultiplier, Event.PitchMultiplier,
			Event.PropagationSpeed, Event.bUsePropagationDelay,
			&Event.BoolParameters, &Event.FloatParameters);
	}
}

void UProjectileCosmeticsManager::PlayDetonation(const FProjectileCosmeticEvent& Event)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (HasPlayback(Event, EProjectileCosmeticPlayback::Visual) && !Event.Effect.IsNull())
	{
		if (UNiagaraSystem* Effect = Event.Effect.LoadSynchronous())
		{
			SpawnEffect(
				Effect,
				Event.Location,
				Event.Direction.Rotation(),
				Event.EffectScale,
				Event.EffectLifeSpan,
				&Event.BoolParameters,
				&Event.FloatParameters);
		}
	}
	if (HasPlayback(Event, EProjectileCosmeticPlayback::Audio))
	{
		for (const TSoftObjectPtr<USoundBase>& Sound : Event.Sounds)
		{
			ScheduleSound(Sound, Event, Event.VolumeMultiplier, Event.PitchMultiplier,
				Event.PropagationSpeed, Event.bUsePropagationDelay,
				&Event.BoolParameters, &Event.FloatParameters);
		}
	}
}

void UProjectileCosmeticsManager::ScheduleSound(
	TSoftObjectPtr<USoundBase> Sound,
	const FProjectileCosmeticEvent& Event,
	float VolumeMultiplier,
	float PitchMultiplier,
	float PropagationSpeed,
	bool bUsePropagationDelay,
	const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters,
	const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters)
{
	if (Sound.IsNull())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	float Delay = 0.f;
	if (!Event.bServerDelayedAudio && bUsePropagationDelay &&
		FMath::IsFinite(PropagationSpeed) &&
		PropagationSpeed > 1.f)
	{
		APlayerController* LocalController = World->GetFirstPlayerController();
		FVector ListenerLocation;
		if (TryGetListenerLocation(LocalController, ListenerLocation))
		{
			const float TravelTime =
				FVector::Distance(ListenerLocation, Event.Location) / PropagationSpeed;
			Delay = Event.EmissionServerTime + TravelTime - GetSynchronizedTime();
		}
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float MaxDelay = Settings
		? FMath::Clamp(Settings->MaxCosmeticAudioDelay, 0.f, 30.f)
		: 15.f;
	Delay = SanitizeCosmeticScalar(Delay, 0.f, MaxDelay, 0.f);

	if (Delay <= KINDA_SMALL_NUMBER)
	{
		PlaySoundNow(
			Sound,
			Event.SourceActor.Get(),
			Event.AudioSourceID,
			Event.Location,
			VolumeMultiplier,
			PitchMultiplier,
			Event.bPassesRight,
			Event.MissDistance,
			Event.ProjectileSpeed,
			BoolParameters,
			FloatParameters);
		return;
	}

	if (PendingAudioEvents.Num() >= GetPendingAudioEventBudget())
	{
		if (LastAudioBudgetWarningFrame != GFrameCounter)
		{
			LastAudioBudgetWarningFrame = GFrameCounter;
			NPS_LOG(Warning,
				TEXT("NPS dropped delayed cosmetic audio at the aggregate queue budget (%d)."),
				GetPendingAudioEventBudget());
		}
		return;
	}

	FPendingAudioEvent& Pending = PendingAudioEvents.Emplace_GetRef();
	Pending.Sound = Sound;
	Pending.SourceActor = Event.SourceActor;
	Pending.AudioSourceID = Event.AudioSourceID;
	Pending.Location = Event.Location;
	Pending.VolumeMultiplier = VolumeMultiplier;
	Pending.PitchMultiplier = PitchMultiplier;
	Pending.TimeRemaining = Delay;
	Pending.bPassesRight = Event.bPassesRight;
	Pending.MissDistance = Event.MissDistance;
	Pending.ProjectileSpeed = Event.ProjectileSpeed;
	if (BoolParameters)
	{
		Pending.BoolParameters = *BoolParameters;
	}
	if (FloatParameters)
	{
		Pending.FloatParameters = *FloatParameters;
	}
}

TSet<USoundConcurrency*> UProjectileCosmeticsManager::ResolveAudioConcurrency(
	USoundBase* Sound, const FGuid& SourceID)
{
	TSet<USoundConcurrency*> Result;
	if (!Sound || !SourceID.IsValid())
	{
		return Result;
	}

	auto ResolveGroup = [this, &SourceID](UObject* SettingsObject,
		const FSoundConcurrencySettings& Settings)
	{
		TWeakObjectPtr<USoundConcurrency>& Entry =
			AudioConcurrencyGroups.FindOrAdd(SourceID).FindOrAdd(SettingsObject->GetUniqueID());
		USoundConcurrency* Group = Entry.Get();
		if (!Group)
		{
			Group = NewObject<USoundConcurrency>(this, NAME_None, RF_Transient);
			Group->Concurrency = Settings;
			// This group already belongs to one source, including when its actor is not replicated.
			Group->Concurrency.bLimitToOwner = false;
			Entry = Group;
		}
		return Group;
	};

	if (Sound->bOverrideConcurrency)
	{
		if (Sound->ConcurrencyOverrides.bLimitToOwner)
		{
			Result.Add(ResolveGroup(Sound, Sound->ConcurrencyOverrides));
		}
		return Result;
	}

	auto AddGroup = [&Result, &ResolveGroup](USoundConcurrency* Group)
	{
		if (Group)
		{
			Result.Add(Group->Concurrency.bLimitToOwner
				? ResolveGroup(Group, Group->Concurrency)
				: Group);
		}
	};
	if (Sound->ConcurrencySet.IsEmpty())
	{
		AddGroup(GetDefault<UAudioSettings>()->GetDefaultSoundConcurrency());
	}
	else
	{
		for (USoundConcurrency* Group : Sound->ConcurrencySet)
		{
			AddGroup(Group);
		}
	}
	return Result;
}

void UProjectileCosmeticsManager::PlaySoundNow(
	TSoftObjectPtr<USoundBase> Sound,
	AActor* SourceActor,
	const FGuid& AudioSourceID,
	FVector Location,
	float VolumeMultiplier,
	float PitchMultiplier,
	bool bPassesRight,
	float MissDistance,
	float ProjectileSpeed,
	const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters,
	const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters)
{
	UWorld* World = GetWorld();
	if (!World || Sound.IsNull() || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (USoundBase* SoundAsset = Sound.LoadSynchronous())
	{
		FAudioDevice::FCreateComponentParams Params(World, IsValid(SourceActor) ? SourceActor : nullptr);
		Params.SetLocation(Location);
		Params.bStopWhenOwnerDestroyed = false;
		Params.ConcurrencySet = ResolveAudioConcurrency(SoundAsset, AudioSourceID);
		if (UAudioComponent* Audio = FAudioDevice::CreateComponent(SoundAsset, Params))
		{
			Audio->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
			Audio->SetVolumeMultiplier(SanitizeCosmeticScalar(VolumeMultiplier, 0.f, 10.f, 1.f));
			Audio->SetPitchMultiplier(SanitizeCosmeticScalar(PitchMultiplier, 0.01f, 4.f, 1.f));
			Audio->bAllowSpatialization = Params.ShouldUseAttenuation();
			Audio->bIsUISound = !World->IsGameWorld();
			Audio->bAutoDestroy = true;
			Audio->SubtitlePriority = SoundAsset->GetSubtitlePriority();
			// Optional MetaSound parameters.
			Audio->SetBoolParameter(TEXT("Direction"), bPassesRight);
			Audio->SetFloatParameter(TEXT("MissDistance"), MissDistance);
			Audio->SetFloatParameter(TEXT("ProjectileSpeed"), ProjectileSpeed);
			UBallisticsCosmeticLibrary::ApplyAudioParameters(
				Audio,
				BoolParameters
					? *BoolParameters
					: EmptyBoolParameters(),
				FloatParameters
					? *FloatParameters
					: EmptyFloatParameters());
			Audio->Play();
		}
	}
}

void UProjectileCosmeticsManager::TickPendingAudio(float DeltaTime)
{
	for (int32 Index = PendingAudioEvents.Num() - 1; Index >= 0; --Index)
	{
		FPendingAudioEvent& Pending = PendingAudioEvents[Index];
		Pending.TimeRemaining -= FMath::Max(DeltaTime, 0.f);
		if (Pending.TimeRemaining > 0.f)
		{
			continue;
		}

		FPendingAudioEvent Ready = MoveTemp(Pending);
		PendingAudioEvents.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		PlaySoundNow(
			Ready.Sound,
			Ready.SourceActor.Get(),
			Ready.AudioSourceID,
			Ready.Location,
			Ready.VolumeMultiplier,
			Ready.PitchMultiplier,
			Ready.bPassesRight,
			Ready.MissDistance,
			Ready.ProjectileSpeed,
			&Ready.BoolParameters,
			&Ready.FloatParameters);
	}
}

void UProjectileCosmeticsManager::TickActiveCosmeticEffects(float DeltaTime)
{
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.f);
	for (int32 Index = ActiveCosmeticEffects.Num() - 1; Index >= 0; --Index)
	{
		FActiveCosmeticEffect& Effect = ActiveCosmeticEffects[Index];
		UNiagaraComponent* Component = Effect.Component.Get();
		if (!Component)
		{
			ActiveCosmeticEffects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		Effect.TimeRemaining -= SafeDeltaTime;
		if (Effect.TimeRemaining > 0.f)
		{
			continue;
		}

		Component->DeactivateImmediate();
		Component->DestroyComponent();
		ActiveCosmeticEffects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
}

void UProjectileCosmeticsManager::TrackCosmeticEffect(
	UNiagaraComponent* Component,
	float LifeSpan)
{
	if (!Component)
	{
		return;
	}

	FActiveCosmeticEffect& Active = ActiveCosmeticEffects.Emplace_GetRef();
	Active.Component = Component;
	Active.TimeRemaining = SanitizeCosmeticScalar(LifeSpan, 0.05f, 60.f, 2.f);
}

#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
void UProjectileCosmeticsManager::TrackCosmeticEffectForTesting(
	UNiagaraComponent* Component,
	float LifeSpan)
{
	TrackCosmeticEffect(Component, LifeSpan);
}
#endif

void UProjectileCosmeticsManager::SpawnEffect(
	UNiagaraSystem* Effect,
	FVector Location,
	FRotator Rotation,
	float Scale,
	float LifeSpan,
	const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters,
	const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters,
	const FBallisticsLaunchEffect* LaunchEffectParameters)
{
	UWorld* World = GetWorld();
	if (!World || !Effect)
	{
		return;
	}

	const int32 Budget = GetActiveCosmeticEffectBudget();
	while (ActiveCosmeticEffects.Num() >= Budget)
	{
		FActiveCosmeticEffect Oldest = ActiveCosmeticEffects[0];
		ActiveCosmeticEffects.RemoveAt(0, 1, EAllowShrinking::No);
		if (UNiagaraComponent* Component = Oldest.Component.Get())
		{
			Component->DeactivateImmediate();
			Component->DestroyComponent();
		}
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		Effect,
		Location,
		Rotation,
		FVector(SanitizeCosmeticScalar(Scale, 0.f, 100.f, 1.f)),
		true,
		false,
		ENCPoolMethod::None,
		false);
	if (!Component)
	{
		return;
	}
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	++SpawnedEffectCount;
	++SpawnedEffectCounts.FindOrAdd(FSoftObjectPath(Effect));
#endif

	UBallisticsCosmeticLibrary::ApplyNiagaraParameters(
		Component,
		BoolParameters
			? *BoolParameters
			: EmptyBoolParameters(),
		FloatParameters
			? *FloatParameters
			: EmptyFloatParameters());
	if (LaunchEffectParameters)
	{
		UBallisticsCosmeticLibrary::ApplyLaunchEffectNiagaraParameters(
			Component,
			*LaunchEffectParameters);
	}
	Component->Activate(true);

	TrackCosmeticEffect(Component, LifeSpan);
}

bool UProjectileCosmeticsManager::TryGetListenerLocation(
	APlayerController* Controller,
	FVector& OutLocation) const
{
	if (!Controller)
	{
		return false;
	}

	if (Controller->PlayerCameraManager)
	{
		OutLocation = Controller->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	if (const AActor* ViewTarget = Controller->GetViewTarget())
	{
		OutLocation = ViewTarget->GetActorLocation();
		return true;
	}
	if (const APawn* Pawn = Controller->GetPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}

	// Dedicated servers do not create PlayerCameraManager instances, and a
	// controller can briefly have no pawn while joining, respawning, or
	// spectating. Controller focal location remains a valid routing origin.
	OutLocation = Controller->GetFocalLocation();
	return !OutLocation.ContainsNaN();
}

float UProjectileCosmeticsManager::GetSynchronizedTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

int32 UProjectileCosmeticsManager::GetPerConnectionBudget() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	return Settings
		? FMath::Clamp(Settings->MaxCosmeticEventsPerConnectionPerFrame, 1, 256)
		: 64;
}

int32 UProjectileCosmeticsManager::GetSequenceBudget() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	return Settings
		? FMath::Clamp(Settings->MaxActiveLaunchSequencesPerClient, 1, 128)
		: 32;
}

int32 UProjectileCosmeticsManager::GetPendingAudioEventBudget() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	return Settings
		? FMath::Clamp(Settings->MaxPendingCosmeticAudioEvents, 1, 2048)
		: 256;
}

int32 UProjectileCosmeticsManager::GetActiveCosmeticEffectBudget() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	return Settings
		? FMath::Clamp(Settings->MaxActiveCosmeticEffects, 1, 2048)
		: 256;
}

float UProjectileCosmeticsManager::GetRoutingRadiusLimit() const
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float Configured = Settings
		? Settings->MaxCosmeticRoutingRadius
		: 2000000.f;
	return SanitizeCosmeticScalar(Configured, 1000.f, 10000000.f, 2000000.f);
}

bool UProjectileCosmeticsManager::TryConsumeLocalEventBudget()
{
	if (LocalEventBudgetFrame != GFrameCounter)
	{
		LocalEventBudgetFrame = GFrameCounter;
		LocalEventsThisFrame = 0;
	}

	const int32 Budget = GetPerConnectionBudget();
	if (LocalEventsThisFrame >= Budget)
	{
		if (LastLocalBudgetWarningFrame != GFrameCounter)
		{
			LastLocalBudgetWarningFrame = GFrameCounter;
			NPS_LOG(Warning,
				TEXT("NPS dropped local cosmetic playback at the world frame budget (%d)."),
				Budget);
		}
		return false;
	}

	++LocalEventsThisFrame;
	return true;
}
