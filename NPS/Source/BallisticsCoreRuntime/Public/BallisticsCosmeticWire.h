// Copyright Persidera Industries LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BallisticsCosmeticTypes.h"
#include "Misc/NetworkGuid.h"
#include "Templates/RefCounting.h"
#include "HAL/ThreadSafeCounter.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/NetRefHandle.h"
#endif
#include "BallisticsCosmeticWire.generated.h"

/** Sender-local ownership of quantized unreliable RPCs. Never serialized. */
class BALLISTICSCORERUNTIME_API FProjectileCosmeticSendQueue : public FRefCountedObject
{
public:
	FThreadSafeCounter Queued;
};

USTRUCT()
struct BALLISTICSCORERUNTIME_API FProjectileCosmeticWireBatch
{
	GENERATED_BODY()
	static constexpr uint32 MaxBytes = 190;
	static constexpr uint32 MaxRecordBytes = 512;
	// Leave room for the RPC envelope, including the largest dynamic object handles.
	static constexpr uint32 MaxUnreliableSerializedBits = (256 - 64) * 8;
	UPROPERTY()
	uint32 Data[48] = {};
	UPROPERTY()
	uint16 BitCount = 0;
	TRefCountPtr<FProjectileCosmeticSendQueue> SendQueue;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
	uint32 MeasureSerializedBits() const;
};

template<> struct TStructOpsTypeTraits<FProjectileCosmeticWireBatch>
	: TStructOpsTypeTraitsBase2<FProjectileCosmeticWireBatch>
{
	enum { WithNetSerializer = true };
};

USTRUCT()
struct BALLISTICSCORERUNTIME_API FProjectileCosmeticWireProfile
{
	GENERATED_BODY()
	UPROPERTY()
	FProjectileCosmeticEvent Event;
};

struct BALLISTICSCORERUNTIME_API FProjectileCosmeticStaticTemplate
{
	FProjectileCosmeticEvent Event;
	FGuid Key;
	TSharedPtr<const FProjectileCosmeticStaticTemplate> Visual;
	TSharedPtr<const FProjectileCosmeticStaticTemplate> Audio;
};

struct BALLISTICSCORERUNTIME_API FProjectileCosmeticWireRecord
{
	uint32 ProfileID = 0;
	uint32 AudioSourceIndex = 0;
	uint64 AudioSourceToken = 0;
	FProjectileCosmeticEvent Event;
	TSharedPtr<const FProjectileCosmeticStaticTemplate> Template;
	FNetworkGUID LegacySource;
#if UE_WITH_IRIS
	UE::Net::FNetRefHandle IrisSource;
#endif
	bool bIrisSource = false;
	bool bSequenceCommand = false;
	ELaunchSequenceCommandType CommandType = ELaunchSequenceCommandType::Begin;
	float LaunchesPerSecond = 1.f;
};

namespace BallisticsCosmeticWire
{
	BALLISTICSCORERUNTIME_API FGuid NewAudioSourceID();
	BALLISTICSCORERUNTIME_API uint64 GetAudioNamespace();
	BALLISTICSCORERUNTIME_API FGuid ResolveAudioToken(uint64 Namespace, uint64 Token);
	BALLISTICSCORERUNTIME_API uint64 GetAudioToken(const FGuid& Identity);
	BALLISTICSCORERUNTIME_API FProjectileCosmeticEvent MakeProfile(const FProjectileCosmeticEvent& Event);
	BALLISTICSCORERUNTIME_API bool SerializeRecord(FArchive& Ar, FProjectileCosmeticWireRecord& Record);
	BALLISTICSCORERUNTIME_API void ApplyRecord(FProjectileCosmeticEvent& Event, const FProjectileCosmeticWireRecord& Record);
	BALLISTICSCORERUNTIME_API TSharedPtr<const FProjectileCosmeticStaticTemplate> Prepare(const FProjectileCosmeticEvent& Event);
	BALLISTICSCORERUNTIME_API TSharedPtr<const FProjectileCosmeticStaticTemplate> ForChannel(const FProjectileCosmeticEvent& Event, uint8 Mask);
	BALLISTICSCORERUNTIME_API void CopyDynamic(FProjectileCosmeticEvent& Target, const FProjectileCosmeticEvent& Source);
	BALLISTICSCORERUNTIME_API uint64 GetTemplateBuildCount();
}
