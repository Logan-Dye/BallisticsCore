// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsCosmeticWire.h"
#include "ProjectileCosmeticsRelay.generated.h"

/**
 * Owner-only network endpoint created automatically by the cosmetics manager.
 *
 * Games never place, subclass, or configure this actor. A relay exists only so
 * the server can send listener-filtered batches without depending on the
 * launched projectile or launcher actor being network relevant.
 */
UCLASS(NotPlaceable, Transient)
class BALLISTICSCORERUNTIME_API AProjectileCosmeticsRelay : public AActor
{
	GENERATED_BODY()

public:
	AProjectileCosmeticsRelay();

	void SendCosmeticEvents(const TArray<FProjectileCosmeticEvent>& Events);
	void SendSequenceCommands(const TArray<FLaunchSequenceCommand>& Commands);
	void FlushCosmeticQueue();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastReceiveCosmeticBatch(const FProjectileCosmeticWireBatch& Batch);

	UFUNCTION(Client, Reliable)
	void ClientReceiveSequenceBatch(const FProjectileCosmeticWireBatch& Batch);

	UFUNCTION(Client, Reliable)
	void ClientRegisterCosmeticProfile(uint32 ProfileID, const FProjectileCosmeticWireProfile& Profile, uint64 AudioNamespace);
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeCosmeticProfile(uint32 ProfileID);
	UFUNCTION(Client, Reliable)
	void ClientReceiveLargeCosmeticRecord(const TArray<uint8>& Data, uint16 BitCount);
	UFUNCTION(Client, Reliable)
	void ClientRegisterAudioSource(uint32 Index, FGuid Identity);

	static constexpr uint32 MaxProfiles = 1024;
	static constexpr uint32 MaxAudioSources = 1024;

	uint32 GetMaxUnreliableSerializedBits() const { return MaxUnreliableSerializedBits; }
	uint32 GetRegisteredProfileCount() const { return Profiles.Num(); }
	uint32 GetPendingRecordCount() const { return Pending.Num(); }
	uint64 GetProfileLookupCount() const { return ProfileLookups; }
	uint64 GetProfileDefinitionCount() const { return ProfileDefinitions; }
	uint64 GetSkippedRecordCount() const { return SkippedRecords; }
	uint32 GetAudioSourceCount() const { return AudioSources.Num(); }
	uint32 GetAcknowledgedProfileCount() const { return AcknowledgedProfiles.Num(); }
	uint32 GetQueuedUnreliableBatchCount() const { return SendQueue->Queued.GetValue(); }
	uint64 GetReceivedRecordCount() const { return ReceivedRecords; }
	uint64 GetReliableRecordCount() const { return ReliableRecords; }
	uint64 GetUnreliableBatchCount() const { return UnreliableBatches; }
	uint64 GetThrottleDeferralCount() const { return ThrottleDeferrals; }
	uint64 GetProfileAcknowledgementCount() const { return ProfileAcknowledgements; }
	uint64 GetRejectedProfileCount() const { return RejectedProfiles; }
	uint64 GetRejectedAudioSourceCount() const { return RejectedAudioSources; }
	const FProjectileCosmeticStaticTemplate* GetProfileTemplate(uint32 ID) const { const auto* Entry = Profiles.Find(ID); return Entry ? Entry->Get() : nullptr; }
#if WITH_DEV_AUTOMATION_TESTS
	bool bHoldProfileAcknowledgementsForTesting = false;
	TRefCountPtr<FProjectileCosmeticSendQueue> GetSendQueueForTesting() const { return SendQueue; }
#endif

private:
	TMap<uint32, TSharedPtr<const FProjectileCosmeticStaticTemplate>> Profiles;
	TMap<FGuid, uint32> ProfileIDs;
	TMap<FGuid, uint32> AudioSourceIDs;
	TMap<uint32, FGuid> AudioSources;
	TSet<uint32> AcknowledgedProfiles;
	TArray<FProjectileCosmeticWireRecord> Pending;
	uint32 NextProfileID = 1;
	uint32 NextAudioSourceID = 1;
	TRefCountPtr<FProjectileCosmeticSendQueue> SendQueue = new FProjectileCosmeticSendQueue();
	uint32 MaxUnreliableSerializedBits = 0;
	uint64 BudgetFrame = MAX_uint64;
	int32 SentThisFrame = 0;
	uint64 RemoteAudioNamespace = 0;
	TArray<FProjectileCosmeticWireRecord> PendingAudioDefinitions;
	bool bFlushing = false;
	uint64 ProfileLookups = 0;
	uint64 ProfileDefinitions = 0;
	uint64 SkippedRecords = 0;
	double LegacyUpdateTime = -DBL_MAX;
	int32 LegacyQueuedCalls = 0;
	uint64 ReliableRecords = 0, UnreliableBatches = 0, ThrottleDeferrals = 0, ReceivedRecords = 0;
	uint64 ProfileAcknowledgements = 0, RejectedProfiles = 0, RejectedAudioSources = 0;
	uint32 RegisterProfile(const TSharedPtr<const FProjectileCosmeticStaticTemplate>& Template);
	bool RegisterAudioSource(FProjectileCosmeticWireRecord& Record, bool& bNew);
	void CaptureSource(FProjectileCosmeticWireRecord& Record, const FProjectileCosmeticEvent& Event);
	AActor* ResolveSource(const FProjectileCosmeticWireRecord& Record) const;
	void ReceiveRecord(FProjectileCosmeticWireRecord& Record);
	void ReceiveWireBatch(const FProjectileCosmeticWireBatch& Batch);
	void SendWireBatch(const FProjectileCosmeticWireBatch& Batch, bool bReliable);
	bool CanQueueUnreliableBatch();
};
