// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BallisticsCosmeticTypes.h"
#include "ProjectileCosmeticsManager.generated.h"

class APlayerController;
class AProjectileCosmeticsRelay;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USoundConcurrency;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnProjectileCosmeticEvent,
	const FProjectileCosmeticEvent&, Event);

/** Batches and plays projectile cosmetic events from the aggregate update. */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UProjectileCosmeticsManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld);
	void Shutdown();
	void Tick(float DeltaTime);

	virtual UWorld* GetWorld() const override;

	/** Route immediate visuals and server-delayed audio from one resolved event. */
	void QueueBroadcastEvent(
		const FProjectileCosmeticEvent& Event,
		APlayerController* ExcludedController = nullptr);

	/** Queue a fly-by or other event for one listener only. */
	void QueueTargetedEvent(
		const FProjectileCosmeticEvent& Event,
		APlayerController* TargetController);

	/** Queue a reliable sustained-sequence state transition. */
	void QueueSequenceCommand(
		const FLaunchSequenceCommand& Command,
		APlayerController* ExcludedController = nullptr);

	/** Play a locally generated event immediately; also feeds the extension delegate. */
	void PlayLocalEvent(const FProjectileCosmeticEvent& Event);

	/** Route a detonation without depending on the source launcher's lifetime. */
	void RouteDetonation(
		const FVector& Location,
		UBallisticsExplosiveDataAsset* ExplosiveData);

	/** Called by the automatic owner-only relay. */
	void ReceiveCosmeticBatch(const TArray<FProjectileCosmeticEvent>& Events);
	void ReceiveSequenceBatch(const TArray<FLaunchSequenceCommand>& Commands);

	/** Extension point for project-specific audio/VFX layered over or instead of built-in assets. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Cosmetics")
	FOnProjectileCosmeticEvent OnCosmeticEvent;

	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	int32 GetActiveSequenceCount() const { return ActiveSequences.Num(); }

	/** Number of events accepted for local playback; useful for diagnostics and automation proof. */
	uint64 GetProcessedEventCount() const { return ProcessedEventCount; }
	uint64 GetRoutedDetonationCount() const { return RoutedDetonationCount; }
	int32 GetRelayCount() const { return Relays.Num(); }
	int32 GetQueuedListenerCount() const { return ListenerQueues.Num(); }
	int32 GetPendingAudioEventCount() const { return PendingAudioEvents.Num(); }
	int32 GetPendingRemoteAcousticDeliveryCount() const { return PendingRemoteDeliveries.Num(); }
	int32 GetActiveCosmeticEffectCount() const { return ActiveCosmeticEffects.Num(); }
	const FProjectileCosmeticEvent& GetLastProcessedEvent() const { return LastProcessedEvent; }

#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	uint64 GetSpawnedEffectCount() const { return SpawnedEffectCount; }
	uint64 GetSpawnedEffectCountForSystem(const FSoftObjectPath& SystemPath) const
	{
		return SpawnedEffectCounts.FindRef(SystemPath);
	}
	uint64 GetSpawnedDecalCount() const { return SpawnedDecalCount; }
	void TrackCosmeticEffectForTesting(UNiagaraComponent* Component, float LifeSpan);
	void TickActiveSequencesForTesting(float DeltaTime)
	{
		TickActiveSequences(DeltaTime);
	}
	TSet<USoundConcurrency*> ResolveAudioConcurrencyForTesting(USoundBase* Sound, const FGuid& SourceID)
	{
		return ResolveAudioConcurrency(Sound, SourceID);
	}
#endif

private:
	struct FListenerQueue
	{
		TArray<FProjectileCosmeticEvent> UnreliableEvents;
		TArray<FLaunchSequenceCommand> ReliableCommands;
	};

	struct FRelayEntry
	{
		TWeakObjectPtr<AProjectileCosmeticsRelay> Relay;
		uint64 CreatedFrame = 0;
	};

	struct FActiveSequence
	{
		FProjectileCosmeticEvent Event;
		float Interval = 1.f;
		float TimeUntilNext = 0.f;
		float TimeSinceSignal = 0.f;
	};

	struct FPendingRemoteDelivery
	{
		double DueServerTime = 0.0;
		uint64 StableOrder = 0;
		TWeakObjectPtr<APlayerController> Controller;
		FProjectileCosmeticEvent Event;
		FLaunchSequenceCommand Command;
		bool bSequenceCommand = false;

		bool operator<(const FPendingRemoteDelivery& Other) const
		{
			// TArray heaps put the smallest element first: deliver the earliest sound
			// before later reports can hold it back and collapse a burst into one batch.
			if (DueServerTime != Other.DueServerTime)
			{
				return DueServerTime < Other.DueServerTime;
			}
			return StableOrder < Other.StableOrder;
		}
	};

	struct FPendingAudioEvent
	{
		TSoftObjectPtr<USoundBase> Sound;
		TWeakObjectPtr<AActor> SourceActor;
		FGuid AudioSourceID;
		FVector Location = FVector::ZeroVector;
		float VolumeMultiplier = 1.f;
		float PitchMultiplier = 1.f;
		float TimeRemaining = 0.f;
		float MissDistance = 0.f;
		float ProjectileSpeed = 0.f;
		bool bPassesRight = false;
		TArray<FBallisticsCosmeticBoolParameter> BoolParameters;
		TArray<FBallisticsCosmeticFloatParameter> FloatParameters;
	};

	struct FActiveCosmeticEffect
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		float TimeRemaining = 0.f;
	};

	TWeakObjectPtr<UWorld> CachedWorld;
	TMap<TWeakObjectPtr<APlayerController>, FListenerQueue> ListenerQueues;
	TMap<TWeakObjectPtr<APlayerController>, FRelayEntry> Relays;
	TMap<uint64, FActiveSequence> ActiveSequences;
	TMap<uint64, float> EndedSequenceTombstones;
	TMap<uint64, TSet<TWeakObjectPtr<APlayerController>>> SequenceRecipients;
	TArray<FPendingRemoteDelivery> PendingRemoteDeliveries;
	TArray<FPendingAudioEvent> PendingAudioEvents;
	TArray<FActiveCosmeticEffect> ActiveCosmeticEffects;
	// Audio components retain the transient groups only for the lifetime of their sounds.
	TMap<FGuid, TMap<uint32, TWeakObjectPtr<USoundConcurrency>>> AudioConcurrencyGroups;
	uint64 LastBudgetWarningFrame = MAX_uint64;
	uint64 LastLocalBudgetWarningFrame = MAX_uint64;
	uint64 LastAudioBudgetWarningFrame = MAX_uint64;
	uint64 LocalEventBudgetFrame = MAX_uint64;
	uint64 ProcessedEventCount = 0;
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	uint64 SpawnedEffectCount = 0;
	TMap<FSoftObjectPath, uint64> SpawnedEffectCounts;
	uint64 SpawnedDecalCount = 0;
#endif
	uint64 RoutedDetonationCount = 0;
	int32 LocalEventsThisFrame = 0;
	int32 NextRoutedEventID = 1;
	uint64 NextRemoteDeliveryOrder = 1;
	FProjectileCosmeticEvent LastProcessedEvent;

	void QueueForController(
		APlayerController* Controller,
		const FProjectileCosmeticEvent* Event,
		const FLaunchSequenceCommand* Command);
	AProjectileCosmeticsRelay* GetOrCreateRelay(APlayerController* Controller);
	void FlushServerQueues();
	void TickPendingRemoteDeliveries();
	void QueueAcousticDelivery(
		APlayerController* Controller,
		const FProjectileCosmeticEvent* Event,
		const FLaunchSequenceCommand* Command,
		const FVector& ListenerLocation);
	void RouteEventChannel(
		const FProjectileCosmeticEvent& Event,
		EProjectileCosmeticPlayback Channel,
		APlayerController* ExcludedController);
	static uint64 MakeSequenceChannelKey(int32 SequenceID, uint8 PlaybackMask);
	void TickActiveSequences(float DeltaTime);
	void TickSequenceTombstones(float DeltaTime);
	void TickPendingAudio(float DeltaTime);
	void TickActiveCosmeticEffects(float DeltaTime);
	void TrackCosmeticEffect(UNiagaraComponent* Component, float LifeSpan);

	bool TryGetListenerLocation(APlayerController* Controller, FVector& OutLocation) const;
	float GetSynchronizedTime() const;
	int32 GetPerConnectionBudget() const;
	int32 GetSequenceBudget() const;
	int32 GetPendingAudioEventBudget() const;
	int32 GetActiveCosmeticEffectBudget() const;
	float GetRoutingRadiusLimit() const;
	bool TryConsumeLocalEventBudget();
	void ResolveLocalLaunchEffectTransforms(FProjectileCosmeticEvent& Event) const;

	void PlayEvent(const FProjectileCosmeticEvent& Event);
	void PlayLaunch(const FProjectileCosmeticEvent& Event);
	void PlayImpact(const FProjectileCosmeticEvent& Event);
	void PlayFlyBy(const FProjectileCosmeticEvent& Event);
	void PlayDetonation(const FProjectileCosmeticEvent& Event);
	void SpawnEffect(
		UNiagaraSystem* Effect,
		FVector Location,
		FRotator Rotation,
		float Scale,
		float LifeSpan,
		const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters = nullptr,
		const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters = nullptr,
		const FBallisticsLaunchEffect* LaunchEffectParameters = nullptr);

	void ScheduleSound(
		TSoftObjectPtr<USoundBase> Sound,
		const FProjectileCosmeticEvent& Event,
		float VolumeMultiplier,
		float PitchMultiplier,
		float PropagationSpeed,
		bool bUsePropagationDelay,
		const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters = nullptr,
		const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters = nullptr);
	void PlaySoundNow(
		TSoftObjectPtr<USoundBase> Sound,
		AActor* SourceActor,
		const FGuid& AudioSourceID,
		FVector Location,
		float VolumeMultiplier,
		float PitchMultiplier,
		bool bPassesRight,
		float MissDistance,
		float ProjectileSpeed,
		const TArray<FBallisticsCosmeticBoolParameter>* BoolParameters = nullptr,
		const TArray<FBallisticsCosmeticFloatParameter>* FloatParameters = nullptr);
	TSet<USoundConcurrency*> ResolveAudioConcurrency(USoundBase* Sound, const FGuid& SourceID);
};
