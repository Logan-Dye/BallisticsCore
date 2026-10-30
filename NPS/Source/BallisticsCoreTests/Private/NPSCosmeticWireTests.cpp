// Copyright Persidera Industries LLC. All Rights Reserved.
#include "BallisticsCosmeticWire.h"
#include "ProjectileCosmeticsRelay.h"
#include "ProjectileLauncherComponent.h"
#include "BallisticsLaunchContributorComponent.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/Serialization/NetSerializer.h"
#include "UObject/UnrealType.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPSCosmeticWireRoundTrip, "NPS.CosmeticWire.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPSCosmeticWireRoundTrip::RunTest(const FString&)
{
	for (uint8 Type = 0; Type <= static_cast<uint8>(EProjectileCosmeticEventType::SequenceTransform); ++Type)
	{
		FProjectileCosmeticWireRecord Source;
		Source.ProfileID = 712;
		Source.Event.Type = static_cast<EProjectileCosmeticEventType>(Type);
		Source.Event.Location = FVector(31234.5f, -1234.5f, 123.4f);
		Source.Event.Direction = FVector(1.f, 2.f, 3.f).GetSafeNormal();
		Source.Event.EventID = -37;
		Source.Event.SequenceID = 195;
		Source.Event.EmissionServerTime = 17.5f;
		Source.Event.SurfaceType = SurfaceType3;
		Source.Event.ImpactResponse = 2;
		Source.Event.MissDistance = 51.75f;
		Source.Event.ProjectileSpeed = 91000.f;
		Source.Event.bPassesRight = true;
		Source.Event.AudioSourceID = BallisticsCosmeticWire::NewAudioSourceID();
		Source.AudioSourceToken = BallisticsCosmeticWire::GetAudioToken(Source.Event.AudioSourceID);
		Source.Event.VolumeMultiplier = 0.75f;
		FBallisticsCosmeticFloatParameter Parameter;
		Parameter.Name = TEXT("Suppressor");
		Parameter.Value = 0.25f;
		Source.Event.FloatParameters.Add(Parameter);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FBallisticsLaunchEffectTransform Transform;
			Transform.Location = Source.Event.Location + FVector(12.3f, -4.5f, 6.7f);
			Transform.Rotation = FRotator(10.f, 20.f, 30.f);
			Transform.bValid = Index == 0;
			Source.Event.LaunchEffectTransforms.Add(Transform);
		}
		FBitWriter Writer(4096, false);
		TestTrue(TEXT("Encode"), BallisticsCosmeticWire::SerializeRecord(Writer, Source));
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		FProjectileCosmeticWireRecord Decoded;
		TestTrue(TEXT("Decode"), BallisticsCosmeticWire::SerializeRecord(Reader, Decoded));
		if (Decoded.AudioSourceToken) { Decoded.Event.AudioSourceID = BallisticsCosmeticWire::ResolveAudioToken(BallisticsCosmeticWire::GetAudioNamespace(), Decoded.AudioSourceToken); }
		TestEqual(TEXT("Consumed exact record length"), Reader.GetPosBits(), Writer.GetNumBits());
		FProjectileCosmeticEvent Result = BallisticsCosmeticWire::MakeProfile(Source.Event);
		BallisticsCosmeticWire::ApplyRecord(Result, Decoded);
		TestTrue(TEXT("Location quantization"), FVector(Result.Location).Equals(Source.Event.Location, 0.11));
		TestTrue(TEXT("Direction quantization"), FVector(Result.Direction).Equals(Source.Event.Direction, 0.0001));
		TestEqual(TEXT("Sequence ID"), Result.SequenceID, Source.Event.SequenceID);
		if (Source.Event.Type != EProjectileCosmeticEventType::SequenceTransform)
		{
			TestEqual(TEXT("Per-event audio source"), Result.AudioSourceID, Source.Event.AudioSourceID);
		}
		TestEqual(TEXT("Cached parameters"), Result.FloatParameters[0].Value, 0.25f);
		if (Source.Event.Type != EProjectileCosmeticEventType::SequenceTransform)
		{
			TestEqual(TEXT("Event ID"), Result.EventID, Source.Event.EventID);
			TestEqual(TEXT("Emission time"), Result.EmissionServerTime, 17.5f);
		}
		if (Source.Event.Type == EProjectileCosmeticEventType::Launch || Source.Event.Type == EProjectileCosmeticEventType::SequenceTransform)
		{
			TestEqual(TEXT("Fallback transforms"), Result.LaunchEffectTransforms.Num(), 2);
			TestTrue(TEXT("Fallback location"), FVector(Result.LaunchEffectTransforms[0].Location).Equals(Source.Event.LaunchEffectTransforms[0].Location, 0.21));
			TestTrue(TEXT("Fallback rotation"), Result.LaunchEffectTransforms[0].Rotation.Equals(Source.Event.LaunchEffectTransforms[0].Rotation, 0.006));
			TestFalse(TEXT("Invalid socket remains invalid"), Result.LaunchEffectTransforms[1].bValid);
		}
		if (Source.Event.Type == EProjectileCosmeticEventType::Impact)
		{
			TestEqual(TEXT("Surface"), Result.SurfaceType.GetValue(), SurfaceType3);
			TestEqual(TEXT("Response"), Result.ImpactResponse, uint8(2));
		}
		if (Source.Event.Type == EProjectileCosmeticEventType::FlyBy)
		{
			TestEqual(TEXT("Speed"), Result.ProjectileSpeed, 91000.f);
			TestEqual(TEXT("Miss distance"), Result.MissDistance, 51.75f);
			TestTrue(TEXT("Pass side"), Result.bPassesRight);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPSCosmeticPreparedTemplates, "NPS.CosmeticWire.PreparedTemplateCacheAndInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPSCosmeticPreparedTemplates::RunTest(const FString&)
{
	auto* Launcher = NewObject<UProjectileLauncherComponent>();
	auto* Profile = NewObject<UBallisticsLaunchCosmeticProfile>();
	Launcher->LaunchCosmeticProfile = Profile;
	auto* Contributor = NewObject<UBallisticsLaunchContributorComponent>();
	auto* Modifier = NewObject<UBallisticsLaunchCosmeticModifier>();
	FBallisticsCosmeticFloatParameter Parameter;
	Parameter.Name = TEXT("Heat"); Parameter.Value = 0.5f;
	Modifier->FloatParameterOverrides.Add(Parameter);
	Contributor->Modifier = Modifier;
	Launcher->RegisterLaunchContributor(Contributor);
	const uint64 Before = BallisticsCosmeticWire::GetTemplateBuildCount();
	const FGuid First = Launcher->GetPreparedLaunchCosmeticKey();
	TestEqual(TEXT("One rebuild prepares three channel templates"), BallisticsCosmeticWire::GetTemplateBuildCount() - Before, uint64(3));
	for (int32 Index = 0; Index < 1000; ++Index) { TestEqual(TEXT("Stable key"), Launcher->GetPreparedLaunchCosmeticKey(), First); }
	TestEqual(TEXT("No per-shot static serialization/hash"), BallisticsCosmeticWire::GetTemplateBuildCount() - Before, uint64(3));
	Modifier->FloatParameterOverrides[0].Value = 0.75f;
	Launcher->NotifyLaunchContributorChanged(Contributor);
	TestNotEqual(TEXT("Mutable modifier invalidates prepared key"), Launcher->GetPreparedLaunchCosmeticKey(), First);
	TestEqual(TEXT("Only invalidation rebuilt three templates"), BallisticsCosmeticWire::GetTemplateBuildCount() - Before, uint64(6));
	FProjectileCosmeticEvent A, B;
	A.AudioSourceID = FGuid::NewGuid(); B.AudioSourceID = FGuid::NewGuid();
	TestEqual(TEXT("Weapon GUID excluded from static identity"), BallisticsCosmeticWire::Prepare(A)->Key, BallisticsCosmeticWire::Prepare(B)->Key);
	B.FloatParameters.Add(Parameter);
	TestNotEqual(TEXT("Deep static payload remains part of identity"), BallisticsCosmeticWire::Prepare(A)->Key, BallisticsCosmeticWire::Prepare(B)->Key);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPSCosmeticWireNativeSerializer, "NPS.CosmeticWire.NativeSerializerMatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPSCosmeticWireNativeSerializer::RunTest(const FString&)
{
#if UE_WITH_IRIS
	using namespace UE::Net;
	// Initialize the native serializer registry even when testing the legacy driver.
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("IrisCore"));
	UFunction* Function = AProjectileCosmeticsRelay::StaticClass()->FindFunctionByName(TEXT("MulticastReceiveCosmeticBatch"));
	if (!TestNotNull(TEXT("Wire RPC"), Function)) { return false; }
	FStructProperty* Property = FindFProperty<FStructProperty>(Function, TEXT("Batch"));
	if (!TestNotNull(TEXT("Wire parameter"), Property)) { return false; }
	const FPropertyNetSerializerInfo* Info = FPropertyNetSerializerInfoRegistry::FindSerializerInfo(Property);
	if (!TestNotNull(TEXT("Registered Iris serializer"), Info)) { return false; }
	const FNetSerializer* Serializer = Info->GetNetSerializer(Property);
	if (!TestNotNull(TEXT("Native serializer"), Serializer)) { return false; }
	TestTrue(TEXT("Native quantized state stays below256bytes"), Serializer->QuantizedTypeSize <= 256);
	TestEqual(TEXT("Native binding, not reflection or LastResort"), FString(Serializer->Name), FString(TEXT("FProjectileCosmeticWireBatchNetSerializer")));
	FProjectileCosmeticWireBatch Source;
	Source.SendQueue = new FProjectileCosmeticSendQueue();
	Source.BitCount = 153;
	for (uint32 Index = 0; Index < 4; ++Index) { Source.Data[Index] = 0x5a72b19dU + Index; }
	FBitWriter Legacy(4096, false);
	bool bSuccess = false;
	Source.NetSerialize(Legacy, nullptr, bSuccess);
	TestTrue(TEXT("Legacy serialize"), bSuccess);
	TArray<uint32> Quantized;
	Quantized.SetNumZeroed((Serializer->QuantizedTypeSize + 3) / 4);
	uint32 Buffer[64] = {};
	FNetBitStreamWriter Writer;
	Writer.InitBytes(Buffer, sizeof(Buffer));
	FNetSerializationContext Context(&Writer);
	FNetQuantizeArgs QuantizeArgs;
	QuantizeArgs.NetSerializerConfig = Serializer->DefaultConfig;
	QuantizeArgs.Source = reinterpret_cast<UPTRINT>(&Source);
	QuantizeArgs.Target = reinterpret_cast<UPTRINT>(Quantized.GetData());
	Serializer->Quantize(Context, QuantizeArgs);
	TestEqual(TEXT("One queued native owner"), Source.SendQueue->Queued.GetValue(), 1);
	Serializer->Quantize(Context, QuantizeArgs);
	TestEqual(TEXT("Requantize replaces ownership"), Source.SendQueue->Queued.GetValue(), 1);
	TArray<uint32> Clone; Clone.SetNumUninitialized(Quantized.Num());
	FMemory::Memset(Clone.GetData(), 0xcd, Clone.Num() * sizeof(uint32));
	FNetCloneDynamicStateArgs CloneArgs;
	CloneArgs.NetSerializerConfig = Serializer->DefaultConfig;
	CloneArgs.Source = reinterpret_cast<UPTRINT>(Quantized.GetData());
	CloneArgs.Target = reinterpret_cast<UPTRINT>(Clone.GetData());
	Serializer->CloneDynamicState(Context, CloneArgs);
	TestEqual(TEXT("Clone retains local ownership"), Source.SendQueue->Queued.GetValue(), 2);
	FNetFreeDynamicStateArgs FreeArgs;
	FreeArgs.NetSerializerConfig = Serializer->DefaultConfig;
	FreeArgs.Source = reinterpret_cast<UPTRINT>(Clone.GetData());
	Serializer->FreeDynamicState(Context, FreeArgs);
	Serializer->FreeDynamicState(Context, FreeArgs);
	TestEqual(TEXT("Cancellation and repeated free balance"), Source.SendQueue->Queued.GetValue(), 1);
	FNetSerializeArgs Args;
	Args.NetSerializerConfig = Serializer->DefaultConfig;
	Args.Source = reinterpret_cast<UPTRINT>(Quantized.GetData());
	Serializer->Serialize(Context, Args);
	Writer.CommitWrites();
	TestFalse(TEXT("No native serialization error"), Context.HasErrorOrOverflow());
	TestEqual(TEXT("Measured native bit count"), Writer.GetPosBits(), Source.MeasureSerializedBits());
	TestEqual(TEXT("Measured legacy bit count"), Legacy.GetNumBits(), int64(Writer.GetPosBits()));
	TestTrue(TEXT("Identical packed bits"), FMemory::Memcmp(Buffer, Legacy.GetData(), Legacy.GetNumBytes()) == 0);
	FNetBitStreamReader NativeReader;
	NativeReader.InitBits(Buffer, Writer.GetPosBits());
	FNetSerializationContext ReadContext(&NativeReader);
	TArray<uint32> ReadState;
	ReadState.SetNumZeroed(Quantized.Num());
	FNetDeserializeArgs ReadArgs;
	ReadArgs.NetSerializerConfig = Serializer->DefaultConfig;
	ReadArgs.Target = reinterpret_cast<UPTRINT>(ReadState.GetData());
	Serializer->Deserialize(ReadContext, ReadArgs);
	FProjectileCosmeticWireBatch Result;
	FNetDequantizeArgs DequantizeArgs;
	DequantizeArgs.NetSerializerConfig = Serializer->DefaultConfig;
	DequantizeArgs.Source = reinterpret_cast<UPTRINT>(ReadState.GetData());
	DequantizeArgs.Target = reinterpret_cast<UPTRINT>(&Result);
	Serializer->Dequantize(ReadContext, DequantizeArgs);
	TestFalse(TEXT("Native roundtrip no error"), ReadContext.HasErrorOrOverflow());
	TestEqual(TEXT("Native roundtrip length"), Result.BitCount, Source.BitCount);
	TestTrue(TEXT("Native roundtrip bits"), FMemory::Memcmp(Result.Data, Source.Data, (Source.BitCount + 7) / 8) == 0);
	TestFalse(TEXT("Local queue ownership never reaches receiver"), Result.SendQueue.IsValid());
	FreeArgs.Source = reinterpret_cast<UPTRINT>(Quantized.GetData());
	Serializer->FreeDynamicState(Context, FreeArgs);
	TestEqual(TEXT("Local send release is independent of delivery ACK"), Source.SendQueue->Queued.GetValue(), 0);
	Serializer->Quantize(Context, QuantizeArgs);
	ReadArgs.Target = reinterpret_cast<UPTRINT>(Quantized.GetData());
	NativeReader.InitBits(Buffer, Writer.GetPosBits());
	Serializer->Deserialize(ReadContext, ReadArgs);
	TestEqual(TEXT("Deserialize releases replaced local owner"), Source.SendQueue->Queued.GetValue(), 0);
	AddInfo(FString::Printf(TEXT("NPS native quantized_bytes=%u ownership lifecycle balanced"), Serializer->QuantizedTypeSize));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPSCosmeticAudioTokens, "NPS.CosmeticWire.AudioTokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPSCosmeticAudioTokens::RunTest(const FString&)
{
	TSet<FGuid> Seen;
	for (int32 I = 0; I < 4096; ++I)
	{
		const FGuid ID = BallisticsCosmeticWire::NewAudioSourceID();
		TestFalse(TEXT("Never reuse a launcher lifetime"), Seen.Contains(ID)); Seen.Add(ID);
		TestEqual(TEXT("Exact identity reconstruction without source actor"),
			BallisticsCosmeticWire::ResolveAudioToken(BallisticsCosmeticWire::GetAudioNamespace(), BallisticsCosmeticWire::GetAudioToken(ID)), ID);
	}
	FProjectileCosmeticWireRecord Token, Indexed;
	Token.AudioSourceToken = 4097; Indexed.AudioSourceIndex = 1024;
	FBitWriter A(4096, false), B(4096, false), Empty(4096, false);
	FProjectileCosmeticWireRecord None;
	BallisticsCosmeticWire::SerializeRecord(A, Token); BallisticsCosmeticWire::SerializeRecord(B, Indexed); BallisticsCosmeticWire::SerializeRecord(Empty, None);
	TestEqual(TEXT("Packed token identity uses16bits at4097"), A.GetNumBits()-Empty.GetNumBits(), int64(16));
	TestEqual(TEXT("Compatibility identity uses16bits at1024"), B.GetNumBits()-Empty.GetNumBits(), int64(16));
	AddInfo(TEXT("NPS audio identity:2kind bits +8-16packed bits, replaces129GUID bits"));
	return true;
}
