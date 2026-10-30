// Copyright Persidera Industries LLC. All Rights Reserved.
#include "ProjectileCosmeticsRelay.h"
#include "BallisticsCoreSettings.h"
#include "BallisticsDebugDraw.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

AProjectileCosmeticsRelay::AProjectileCosmeticsRelay()
{
	bReplicates = true;
#if UE_WITH_IRIS
	bReplicateUsingRegisteredSubObjectList = true;
#endif
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
}

uint32 AProjectileCosmeticsRelay::RegisterProfile(const TSharedPtr<const FProjectileCosmeticStaticTemplate>& Template)
{
	++ProfileLookups;
	if (const uint32* ID = ProfileIDs.Find(Template->Key)) { return *ID; }
	if (Profiles.Num() >= MaxProfiles)
	{
		if (++RejectedProfiles == 1) { UE_LOG(LogNPS, Warning, TEXT("NPS cosmetic profile catalog is full; new content rejected.")); }
		return 0;
	}
	const uint32 ID = NextProfileID++;
	Profiles.Add(ID, Template);
	ProfileIDs.Add(Template->Key, ID);
	++ProfileDefinitions;
	FProjectileCosmeticWireProfile Profile;
	Profile.Event = Template->Event;
	ClientRegisterCosmeticProfile(ID, Profile, BallisticsCosmeticWire::GetAudioNamespace());
	return ID;
}

bool AProjectileCosmeticsRelay::RegisterAudioSource(FProjectileCosmeticWireRecord& Record, bool& bNew)
{
	bNew = false;
	if (!Record.Event.AudioSourceID.IsValid()) { return true; }
	Record.AudioSourceToken = BallisticsCosmeticWire::GetAudioToken(Record.Event.AudioSourceID);
	if (Record.AudioSourceToken) { return true; }
	if (const uint32* ID = AudioSourceIDs.Find(Record.Event.AudioSourceID)) { Record.AudioSourceIndex = *ID; return true; }
	if (AudioSources.Num() >= MaxAudioSources)
	{
		if (++RejectedAudioSources == 1) { UE_LOG(LogNPS, Warning, TEXT("NPS cosmetic audio source catalog is full; new identities rejected.")); }
		return false;
	}
	Record.AudioSourceIndex = NextAudioSourceID++;
	AudioSourceIDs.Add(Record.Event.AudioSourceID, Record.AudioSourceIndex);
	AudioSources.Add(Record.AudioSourceIndex, Record.Event.AudioSourceID);
	ClientRegisterAudioSource(Record.AudioSourceIndex, Record.Event.AudioSourceID);
	bNew = true;
	return true;
}

void AProjectileCosmeticsRelay::CaptureSource(FProjectileCosmeticWireRecord& Record, const FProjectileCosmeticEvent& Event)
{
	if (!IsValid(Event.SourceActor)) { return; }
#if UE_WITH_IRIS
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (Driver && Driver->IsUsingIrisReplication())
	{
		Record.bIrisSource = true;
		Record.IrisSource = Driver->GetReplicationSystem()->GetReplicationBridge()->GetReplicatedRefHandle(Event.SourceActor);
		return;
	}
#endif
	if (UNetConnection* Connection = GetNetConnection())
	{
		Record.LegacySource = Connection->PackageMap->GetNetGUIDFromObject(Event.SourceActor);
	}
}

AActor* AProjectileCosmeticsRelay::ResolveSource(const FProjectileCosmeticWireRecord& Record) const
{
	AActor* Actor = nullptr;
	UNetDriver* Driver = GetWorld()->GetNetDriver();
#if UE_WITH_IRIS
	if (Record.bIrisSource && Driver && Driver->IsUsingIrisReplication())
	{
		// The public bridge resolves the archived incomplete handle in this world's replication system.
		if (Record.IrisSource.IsValid()) { Actor = Cast<AActor>(Driver->GetReplicationSystem()->GetReplicationBridge()->GetReplicatedObject(Record.IrisSource)); }
	}
	else
#endif
	if (UNetConnection* Connection = Driver ? Driver->ServerConnection : nullptr)
	{
		if (Record.LegacySource.IsValid()) { Actor = Cast<AActor>(Connection->PackageMap->GetObjectFromNetGUID(Record.LegacySource, true)); }
	}
	return IsValid(Actor) && Actor->GetWorld() == GetWorld() ? Actor : nullptr;
}

void AProjectileCosmeticsRelay::SendCosmeticEvents(const TArray<FProjectileCosmeticEvent>& Events)
{
	if (!HasAuthority()) { return; }
	for (const auto& Event : Events)
	{
		if (Pending.Num() >= 1024) { NPS_LOG(Warning, TEXT("NPS cosmetic delivery backlog is full.")); break; }
		FProjectileCosmeticWireRecord Record;
		BallisticsCosmeticWire::CopyDynamic(Record.Event, Event);
		if (Event.Type != EProjectileCosmeticEventType::SequenceTransform) { Record.Template = BallisticsCosmeticWire::Prepare(Event); }
		CaptureSource(Record, Event);
		Pending.Add(MoveTemp(Record));
	}
	FlushCosmeticQueue();
}

void AProjectileCosmeticsRelay::SendSequenceCommands(const TArray<FLaunchSequenceCommand>& Commands)
{
	if (!HasAuthority()) { return; }
	for (const auto& Command : Commands)
	{
		if (Pending.Num() >= 1024) { NPS_LOG(Warning, TEXT("NPS cosmetic delivery backlog is full.")); break; }
		FProjectileCosmeticWireRecord Record;
		Record.bSequenceCommand = true;
		Record.CommandType = Command.Type;
		Record.LaunchesPerSecond = Command.LaunchesPerSecond;
		BallisticsCosmeticWire::CopyDynamic(Record.Event, Command.LaunchEvent);
		Record.Event.SequenceID = Command.SequenceID;
		if (Command.Type == ELaunchSequenceCommandType::Begin) { Record.Template = BallisticsCosmeticWire::Prepare(Command.LaunchEvent); }
		CaptureSource(Record, Command.LaunchEvent);
		Pending.Add(MoveTemp(Record));
	}
	FlushCosmeticQueue();
}

void AProjectileCosmeticsRelay::FlushCosmeticQueue()
{
	if (!HasAuthority() || bFlushing || Pending.IsEmpty()) { return; }
	TGuardValue<bool> Guard(bFlushing, true);
	if (BudgetFrame != GFrameCounter) { BudgetFrame = GFrameCounter; SentThisFrame = 0; }
	const auto* Settings = UBallisticsCoreSettings::Get();
	const int32 Budget = Settings ? FMath::Clamp(Settings->MaxCosmeticEventsPerConnectionPerFrame, 1, 256) : 64;
	FBitWriter Writer(FProjectileCosmeticWireBatch::MaxBytes * 8, false);
	uint8 Count = 0;
	Writer.SerializeBits(&Count, 8);
	auto Flush = [&]()
	{
		if (!Count) { return; }
		Writer.GetData()[0] = Count;
		FProjectileCosmeticWireBatch Batch;
		Batch.BitCount = static_cast<uint16>(Writer.GetNumBits());
		FMemory::Memcpy(Batch.Data, Writer.GetData(), Writer.GetNumBytes());
		SendWireBatch(Batch, false);
		Writer.Reset(); Count = 0;
		Writer.SerializeBits(&Count, 8);
	};
	for (int32 Index = 0; Index < Pending.Num() && SentThisFrame < Budget;)
	{
		auto& Record = Pending[Index];
		if (Record.Template)
		{
			Record.ProfileID = RegisterProfile(Record.Template);
			if (!Record.ProfileID) { Pending.RemoveAt(Index); continue; }
		}
		bool bNewAudio = false;
		const bool bHasIdentity = Record.Event.Type != EProjectileCosmeticEventType::SequenceTransform &&
			!(Record.bSequenceCommand && Record.CommandType == ELaunchSequenceCommandType::End);
		if (bHasIdentity && !RegisterAudioSource(Record, bNewAudio)) { Pending.RemoveAt(Index); continue; }
		FBitWriter Encoded(FProjectileCosmeticWireBatch::MaxRecordBytes * 8, false);
		if (!BallisticsCosmeticWire::SerializeRecord(Encoded, Record))
		{
			NPS_LOG(Error, TEXT("NPS rejected an invalid cosmetic wire record."));
			Pending.RemoveAt(Index); continue;
		}
		const bool bReliable = Record.bSequenceCommand || bNewAudio || (Record.ProfileID && !AcknowledgedProfiles.Contains(Record.ProfileID));
		const bool bLarge = 16 + 8 + 16 + Encoded.GetNumBits() > FProjectileCosmeticWireBatch::MaxUnreliableSerializedBits;
		if (Count && (bReliable || bLarge || Count == 64 || 16 + Writer.GetNumBits() + 16 + Encoded.GetNumBits() > FProjectileCosmeticWireBatch::MaxUnreliableSerializedBits)) { Flush(); }
		if (!bReliable && !bLarge && !CanQueueUnreliableBatch()) { ++ThrottleDeferrals; ForceNetUpdate(); break; }
		if (bLarge)
		{
			++ReliableRecords;
			TArray<uint8> Data; Data.Append(Encoded.GetData(), Encoded.GetNumBytes());
			ClientReceiveLargeCosmeticRecord(Data, static_cast<uint16>(Encoded.GetNumBits()));
		}
		else if (bReliable)
		{
			++ReliableRecords;
			FBitWriter Reliable(FProjectileCosmeticWireBatch::MaxBytes * 8, false);
			uint8 One = 1; uint16 Bits = static_cast<uint16>(Encoded.GetNumBits());
			Reliable.SerializeBits(&One, 8); Reliable.SerializeBits(&Bits, 16); Reliable.SerializeBits(Encoded.GetData(), Bits);
			FProjectileCosmeticWireBatch Batch;
			Batch.BitCount = static_cast<uint16>(Reliable.GetNumBits());
			FMemory::Memcpy(Batch.Data, Reliable.GetData(), Reliable.GetNumBytes());
			SendWireBatch(Batch, true);
		}
		else
		{
			uint16 Bits = static_cast<uint16>(Encoded.GetNumBits());
			Writer.SerializeBits(&Bits, 16); Writer.SerializeBits(Encoded.GetData(), Bits); ++Count;
		}
		++SentThisFrame;
		Pending.RemoveAt(Index);
	}
	Flush();
}

bool AProjectileCosmeticsRelay::CanQueueUnreliableBatch()
{
#if UE_WITH_IRIS
	const auto* Driver = GetWorld()->GetNetDriver();
	if (Driver && Driver->IsUsingIrisReplication())
	{
		static const auto* Capacity = IConsoleManager::Get().FindConsoleVariable(TEXT("net.UnreliableRPCQueueSize"));
		return SendQueue->Queued.GetValue() < (Capacity ? Capacity->GetInt() : 10);
	}
#endif
	UNetConnection* Connection = GetNetConnection();
	const UActorChannel* Channel = Connection ? Connection->FindActorChannelRef(this) : nullptr;
	if (!Channel) { return false; }
	if (Channel->LastUpdateTime != LegacyUpdateTime) { LegacyUpdateTime = Channel->LastUpdateTime; LegacyQueuedCalls = 0; }
	static const auto* Limit = IConsoleManager::Get().FindConsoleVariable(TEXT("net.MaxRPCPerNetUpdate"));
	return LegacyQueuedCalls < (Limit ? Limit->GetInt() : 2);
}

void AProjectileCosmeticsRelay::SendWireBatch(const FProjectileCosmeticWireBatch& Batch, bool bReliable)
{
	if (bReliable) { ClientReceiveSequenceBatch(Batch); }
	else
	{
		const uint32 Bits = Batch.MeasureSerializedBits();
		check(Bits <= FProjectileCosmeticWireBatch::MaxUnreliableSerializedBits);
		MaxUnreliableSerializedBits = FMath::Max(MaxUnreliableSerializedBits, Bits);
		FProjectileCosmeticWireBatch Out = Batch;
		Out.SendQueue = SendQueue;
		++UnreliableBatches; ++LegacyQueuedCalls;
		MulticastReceiveCosmeticBatch(Out);
		ForceNetUpdate();
	}
}

void AProjectileCosmeticsRelay::ClientRegisterCosmeticProfile_Implementation(uint32 ID, const FProjectileCosmeticWireProfile& Profile, uint64 AudioNamespace)
{
	if (!ID || ID > MaxProfiles || Profiles.Contains(ID)) { return; }
	RemoteAudioNamespace = AudioNamespace;
	Profiles.Add(ID, BallisticsCosmeticWire::Prepare(Profile.Event));
	ServerAcknowledgeCosmeticProfile(ID);
}

void AProjectileCosmeticsRelay::ClientRegisterAudioSource_Implementation(uint32 Index, FGuid Identity)
{
	if (!Index || Index > MaxAudioSources || !Identity.IsValid() || AudioSources.Contains(Index)) { return; }
	AudioSources.Add(Index, Identity);
	for (int32 I = 0; I < PendingAudioDefinitions.Num();)
	{
		if (PendingAudioDefinitions[I].AudioSourceIndex != Index) { ++I; continue; }
		FProjectileCosmeticWireRecord Record = MoveTemp(PendingAudioDefinitions[I]);
		PendingAudioDefinitions.RemoveAt(I);
		ReceiveRecord(Record);
	}
}

void AProjectileCosmeticsRelay::ServerAcknowledgeCosmeticProfile_Implementation(uint32 ID)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bHoldProfileAcknowledgementsForTesting) { return; }
#endif
	if (Profiles.Contains(ID)) { ++ProfileAcknowledgements; AcknowledgedProfiles.Add(ID); }
}

void AProjectileCosmeticsRelay::ReceiveRecord(FProjectileCosmeticWireRecord& Record)
{
	UProjectileBallisticsSubsystem* Subsystem = GetWorld()->GetSubsystem<UProjectileBallisticsSubsystem>();
	UProjectileCosmeticsManager* Manager = Subsystem ? Subsystem->GetCosmeticsManager() : nullptr;
	if (!Manager) { return; }
	FProjectileCosmeticEvent Event;
	if (Record.ProfileID)
	{
		const auto* Profile = Profiles.Find(Record.ProfileID);
		if (!Profile) { ++SkippedRecords; return; }
		Event = (*Profile)->Event;
		Event.SourceActor = ResolveSource(Record);
	}
	if (Record.AudioSourceIndex)
	{
		const auto* Identity = AudioSources.Find(Record.AudioSourceIndex);
		if (!Identity)
		{
			// A disposable record can overtake its reliable compatibility definition.
			if (Record.AudioSourceIndex <= MaxAudioSources && PendingAudioDefinitions.Num() < 1024) { PendingAudioDefinitions.Add(MoveTemp(Record)); }
			else { ++SkippedRecords; }
			return;
		}
		Record.Event.AudioSourceID = *Identity;
	}
	else if (Record.AudioSourceToken) { Record.Event.AudioSourceID = BallisticsCosmeticWire::ResolveAudioToken(RemoteAudioNamespace, Record.AudioSourceToken); }
	++ReceivedRecords;
	BallisticsCosmeticWire::ApplyRecord(Event, Record);
	if (Record.bSequenceCommand)
	{
		FLaunchSequenceCommand Command;
		Command.Type = Record.CommandType; Command.SequenceID = Event.SequenceID;
		Command.LaunchesPerSecond = Record.LaunchesPerSecond; Command.LaunchEvent = MoveTemp(Event);
		Manager->ReceiveSequenceBatch({Command});
	}
	else { Manager->ReceiveCosmeticBatch({Event}); }
}

void AProjectileCosmeticsRelay::ReceiveWireBatch(const FProjectileCosmeticWireBatch& Batch)
{
	if (HasAuthority() || !Batch.BitCount || Batch.BitCount > FProjectileCosmeticWireBatch::MaxBytes * 8) { return; }
	FBitReader Reader(reinterpret_cast<const uint8*>(Batch.Data), Batch.BitCount);
	uint8 Count = 0; Reader.SerializeBits(&Count, 8);
	if (Reader.IsError() || !Count || Count > 64) { return; }
	for (uint8 Index = 0; Index < Count; ++Index)
	{
		uint16 Bits = 0; Reader.SerializeBits(&Bits, 16);
		if (Reader.IsError() || Bits > Reader.GetBitsLeft()) { ++SkippedRecords; return; }
		if (!Bits) { ++SkippedRecords; continue; }
		TArray<uint8> Data; Data.SetNumZeroed((Bits + 7) / 8);
		Reader.SerializeBits(Data.GetData(), Bits);
		FBitReader RecordReader(Data.GetData(), Bits);
		FProjectileCosmeticWireRecord Record;
		if (!BallisticsCosmeticWire::SerializeRecord(RecordReader, Record) || RecordReader.GetBitsLeft()) { ++SkippedRecords; continue; }
		ReceiveRecord(Record);
	}
}

void AProjectileCosmeticsRelay::ClientReceiveLargeCosmeticRecord_Implementation(const TArray<uint8>& Data, uint16 BitCount)
{
	if (HasAuthority() || !BitCount || Data.Num() > FProjectileCosmeticWireBatch::MaxRecordBytes || Data.Num() != (BitCount + 7) / 8) { return; }
	FBitReader Reader(Data.GetData(), BitCount);
	FProjectileCosmeticWireRecord Record;
	if (BallisticsCosmeticWire::SerializeRecord(Reader, Record) && Reader.GetBitsLeft() == 0) { ReceiveRecord(Record); }
	else { ++SkippedRecords; }
}

void AProjectileCosmeticsRelay::MulticastReceiveCosmeticBatch_Implementation(const FProjectileCosmeticWireBatch& Batch) { ReceiveWireBatch(Batch); }
void AProjectileCosmeticsRelay::ClientReceiveSequenceBatch_Implementation(const FProjectileCosmeticWireBatch& Batch) { ReceiveWireBatch(Batch); }
