// Copyright Persidera Industries LLC. All Rights Reserved.
#include "BallisticsCosmeticWire.h"
#include "Serialization/BitWriter.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/SecureHash.h"
#include "HAL/ThreadSafeCounter64.h"

bool FProjectileCosmeticWireBatch::NetSerialize(FArchive& Ar, UPackageMap*, bool& bSuccess)
{
	Ar.SerializeBits(&BitCount, 16);
	if (BitCount > MaxBytes * 8)
	{
		Ar.SetError();
		bSuccess = false;
		return true;
	}
	if (Ar.IsLoading()) { FMemory::Memzero(Data, sizeof(Data)); }
	Ar.SerializeBits(Data, BitCount);
	bSuccess = !Ar.IsError();
	return true;
}

uint32 FProjectileCosmeticWireBatch::MeasureSerializedBits() const
{
	FBitWriter Writer(MaxBytes * 8 + 16, false);
	bool bSuccess = false;
	const_cast<FProjectileCosmeticWireBatch*>(this)->NetSerialize(Writer, nullptr, bSuccess);
	return bSuccess ? static_cast<uint32>(Writer.GetNumBits()) : MAX_uint32;
}

namespace BallisticsCosmeticWire
{
uint64 GetAudioNamespace()
{
	static const uint64 Namespace = []() { const FGuid ID = FGuid::NewGuid(); return (uint64(ID.A) << 32 | ID.B) | 1; }();
	return Namespace;
}
FGuid ResolveAudioToken(uint64 Namespace, uint64 Token)
{
	return FGuid(uint32(Namespace >> 32), uint32(Namespace), uint32(Token >> 32), uint32(Token));
}
FGuid NewAudioSourceID()
{
	static FThreadSafeCounter64 Next;
	const int64 Token = Next.Increment();
	checkf(Token > 0, TEXT("NPS audio identity token space exhausted"));
	return ResolveAudioToken(GetAudioNamespace(), uint64(Token));
}
uint64 GetAudioToken(const FGuid& Identity)
{
	return (uint64(Identity.A) << 32 | Identity.B) == GetAudioNamespace() ? (uint64(Identity.C) << 32 | Identity.D) : 0;
}
static uint64 TemplateBuildCount = 0;
uint64 GetTemplateBuildCount() { return TemplateBuildCount; }

static TSharedPtr<FProjectileCosmeticStaticTemplate> BuildTemplate(const FProjectileCosmeticEvent& Event)
{
	auto Result = MakeShared<FProjectileCosmeticStaticTemplate>();
	Result->Event = MakeProfile(Event);
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive Archive(Writer, false);
	FProjectileCosmeticEvent::StaticStruct()->SerializeItem(Archive, &Result->Event, nullptr);
	uint8 Digest[16];
	FMD5 Hash;
	Hash.Update(Bytes.GetData(), Bytes.Num());
	Hash.Final(Digest);
	FMemory::Memcpy(&Result->Key, Digest, sizeof(Digest));
	++TemplateBuildCount;
	return Result;
}

TSharedPtr<const FProjectileCosmeticStaticTemplate> Prepare(const FProjectileCosmeticEvent& Event)
{
	if (Event.WireTemplate) { return Event.WireTemplate; }
	auto Result = BuildTemplate(Event);
	FProjectileCosmeticEvent Visual = Result->Event;
	Visual.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Visual);
	Visual.Sounds.Reset();
	Result->Visual = BuildTemplate(Visual);
	FProjectileCosmeticEvent Audio = Result->Event;
	Audio.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
	Audio.LaunchEffects.Reset();
	Audio.Effect.Reset();
	Audio.DecalMaterial.Reset();
	Result->Audio = BuildTemplate(Audio);
	return Result;
}

TSharedPtr<const FProjectileCosmeticStaticTemplate> ForChannel(const FProjectileCosmeticEvent& Event, uint8 Mask)
{
	auto Template = Prepare(Event);
	if (Template->Event.PlaybackMask == Mask) { return Template; }
	return Mask == static_cast<uint8>(EProjectileCosmeticPlayback::Visual) ? Template->Visual : Template->Audio;
}

void CopyDynamic(FProjectileCosmeticEvent& Target, const FProjectileCosmeticEvent& Source)
{
	FProjectileCosmeticWireRecord Record;
	Record.Event.Type = Source.Type;
	Record.Event.EventID = Source.EventID;
	Record.Event.SequenceID = Source.SequenceID;
	Record.Event.Location = Source.Location;
	Record.Event.Direction = Source.Direction;
	Record.Event.SurfaceType = Source.SurfaceType;
	Record.Event.ImpactResponse = Source.ImpactResponse;
	Record.Event.EmissionServerTime = Source.EmissionServerTime;
	Record.Event.MissDistance = Source.MissDistance;
	Record.Event.ProjectileSpeed = Source.ProjectileSpeed;
	Record.Event.bPassesRight = Source.bPassesRight;
	Record.Event.PlaybackMask = Source.PlaybackMask;
	Record.Event.bServerDelayedAudio = Source.bServerDelayedAudio;
	Record.Event.LaunchEffectTransforms = Source.LaunchEffectTransforms;
	Record.Event.AudioSourceID = Source.AudioSourceID;
	ApplyRecord(Target, Record);
}

static void PackedSigned(FArchive& Ar, int32& Value)
{
	uint32 Packed = (static_cast<uint32>(Value) << 1) ^ static_cast<uint32>(Value >> 31);
	Ar.SerializeIntPacked(Packed);
	if (Ar.IsLoading()) { Value = static_cast<int32>((Packed >> 1) ^ (0U - (Packed & 1))); }
}

FProjectileCosmeticEvent MakeProfile(const FProjectileCosmeticEvent& Event)
{
	FProjectileCosmeticEvent Result = Event;
	Result.WireTemplate.Reset();
	Result.AudioSourceID.Invalidate();
	Result.bServerDelayedAudio = false;
	if (Result.Type == EProjectileCosmeticEventType::SequenceTransform)
	{
		Result.Type = EProjectileCosmeticEventType::Launch;
	}
	Result.EventID = INDEX_NONE;
	Result.SequenceID = INDEX_NONE;
	Result.Location = FVector::ZeroVector;
	Result.Direction = FVector::ForwardVector;
	Result.SourceActor = nullptr;
	Result.LaunchEffectTransforms.Reset();
	Result.SurfaceType = SurfaceType_Default;
	Result.ImpactResponse = 0;
	Result.EmissionServerTime = 0.f;
	Result.MissDistance = 0.f;
	Result.ProjectileSpeed = 0.f;
	Result.bPassesRight = false;
	return Result;
}

bool SerializeRecord(FArchive& Ar, FProjectileCosmeticWireRecord& Record)
{
	uint8 bCommand = Record.bSequenceCommand;
	Ar.SerializeBits(&bCommand, 1);
	Record.bSequenceCommand = bCommand != 0;
	if (Record.bSequenceCommand)
	{
		uint8 Command = static_cast<uint8>(Record.CommandType);
		Ar.SerializeBits(&Command, 1);
		Record.CommandType = static_cast<ELaunchSequenceCommandType>(Command);
		if (Record.CommandType == ELaunchSequenceCommandType::End)
		{
			PackedSigned(Ar, Record.Event.SequenceID);
			Ar.SerializeBits(&Record.Event.PlaybackMask, 2);
			return !Ar.IsError();
		}
		Ar << Record.LaunchesPerSecond;
	}

	uint8 Type = static_cast<uint8>(Record.Event.Type);
	Ar.SerializeBits(&Type, 3);
	if (Type > static_cast<uint8>(EProjectileCosmeticEventType::SequenceTransform))
	{
		Ar.SetError();
		return false;
	}
	Record.Event.Type = static_cast<EProjectileCosmeticEventType>(Type);
	Ar.SerializeBits(&Record.Event.PlaybackMask, 2);
	uint8 bHasSequence = Record.Event.SequenceID != INDEX_NONE;
	Ar.SerializeBits(&bHasSequence, 1);
	if (bHasSequence) { PackedSigned(Ar, Record.Event.SequenceID); }
	else { Record.Event.SequenceID = INDEX_NONE; }
	const bool bTransformOnly = Record.Event.Type == EProjectileCosmeticEventType::SequenceTransform;
	if (!bTransformOnly)
	{
		Ar.SerializeIntPacked(Record.ProfileID);
		uint8 AudioKind = Record.AudioSourceToken ? 1 : (Record.AudioSourceIndex ? 2 : 0);
		Ar.SerializeBits(&AudioKind, 2);
		if (AudioKind == 1) { Ar.SerializeIntPacked64(Record.AudioSourceToken); }
		else if (AudioKind == 2) { Ar.SerializeIntPacked(Record.AudioSourceIndex); }
		else if (AudioKind == 3) { Ar.SetError(); return false; }
		uint8 bIris = Record.bIrisSource;
		Ar.SerializeBits(&bIris, 1);
		Record.bIrisSource = bIris != 0;
#if UE_WITH_IRIS
		if (Record.bIrisSource) { Ar << Record.IrisSource; }
		else
#endif
		{
			uint8 bSource = Record.LegacySource.IsValid();
			Ar.SerializeBits(&bSource, 1);
			if (bSource) { Ar << Record.LegacySource; }
			else { Record.LegacySource.Reset(); }
		}
		PackedSigned(Ar, Record.Event.EventID);
		Ar << Record.Event.EmissionServerTime;
		uint8 bDelayed = Record.Event.bServerDelayedAudio;
		Ar.SerializeBits(&bDelayed, 1);
		Record.Event.bServerDelayedAudio = bDelayed != 0;
	}
	bool bSuccess = true;
	Record.Event.Location.NetSerialize(Ar, nullptr, bSuccess);
	Record.Event.Direction.NetSerialize(Ar, nullptr, bSuccess);
	if (!bSuccess) { Ar.SetError(); }

	if (Record.Event.Type == EProjectileCosmeticEventType::Impact)
	{
		uint8 Surface = Record.Event.SurfaceType.GetValue();
		Ar.SerializeBits(&Surface, 6);
		Ar.SerializeBits(&Record.Event.ImpactResponse, 3);
		Record.Event.SurfaceType = static_cast<EPhysicalSurface>(Surface);
	}
	if (Record.Event.Type == EProjectileCosmeticEventType::FlyBy)
	{
		Ar << Record.Event.MissDistance;
		Ar << Record.Event.ProjectileSpeed;
		uint8 bRight = Record.Event.bPassesRight;
		Ar.SerializeBits(&bRight, 1);
		Record.Event.bPassesRight = bRight != 0;
	}
	if ((Record.Event.Type == EProjectileCosmeticEventType::Launch || bTransformOnly) &&
		(Record.Event.PlaybackMask & static_cast<uint8>(EProjectileCosmeticPlayback::Visual)))
	{
		if (Ar.IsSaving() && Record.Event.LaunchEffectTransforms.Num() > 16) { Ar.SetError(); return false; }
		uint8 Count = static_cast<uint8>(Record.Event.LaunchEffectTransforms.Num());
		if (Count > 16) { Ar.SetError(); return false; }
		Ar.SerializeBits(&Count, 5);
		if (Count > 16) { Ar.SetError(); return false; }
		if (Ar.IsLoading()) { Record.Event.LaunchEffectTransforms.SetNum(Count); }
		for (FBallisticsLaunchEffectTransform& Transform : Record.Event.LaunchEffectTransforms)
		{
			uint8 bValid = Transform.bValid;
			Ar.SerializeBits(&bValid, 1);
			Transform.bValid = bValid != 0;
			if (Transform.bValid)
			{
				FVector_NetQuantize10 Relative = Transform.Location - Record.Event.Location;
				Relative.NetSerialize(Ar, nullptr, bSuccess);
				Transform.Rotation.SerializeCompressedShort(Ar);
				if (Ar.IsLoading()) { Transform.Location = Record.Event.Location + Relative; }
			}
		}
	}
	return bSuccess && !Ar.IsError();
}

void ApplyRecord(FProjectileCosmeticEvent& Event, const FProjectileCosmeticWireRecord& Record)
{
	Event.AudioSourceID = Record.Event.AudioSourceID;
	Event.Type = Record.Event.Type;
	Event.EventID = Record.Event.EventID;
	Event.SequenceID = Record.Event.SequenceID;
	Event.Location = Record.Event.Location;
	Event.Direction = Record.Event.Direction;
	Event.SurfaceType = Record.Event.SurfaceType;
	Event.ImpactResponse = Record.Event.ImpactResponse;
	Event.EmissionServerTime = Record.Event.EmissionServerTime;
	Event.MissDistance = Record.Event.MissDistance;
	Event.ProjectileSpeed = Record.Event.ProjectileSpeed;
	Event.bPassesRight = Record.Event.bPassesRight;
	Event.PlaybackMask = Record.Event.PlaybackMask;
	Event.bServerDelayedAudio = Record.Event.bServerDelayedAudio;
	Event.LaunchEffectTransforms = Record.Event.LaunchEffectTransforms;
}
}
