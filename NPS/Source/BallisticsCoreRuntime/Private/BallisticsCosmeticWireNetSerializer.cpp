// Copyright Persidera Industries LLC. All Rights Reserved.
#include "BallisticsCosmeticWire.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/NetSerializerDelegates.h"

namespace UE::Net
{
UE_NET_DECLARE_SERIALIZER(FProjectileCosmeticWireBatchNetSerializer, BALLISTICSCORERUNTIME_API);

struct FProjectileCosmeticWireBatchNetSerializerConfig : FNetSerializerConfig {};
struct FProjectileCosmeticWireBatchNetSerializer
{
	static constexpr uint32 Version = 1;
	static constexpr bool bHasDynamicState = true;
	using SourceType = FProjectileCosmeticWireBatch;
	struct QuantizedType { uint32 Data[48]; uint16 BitCount; FProjectileCosmeticSendQueue* SendQueue; };
	using ConfigType = FProjectileCosmeticWireBatchNetSerializerConfig;
	static const ConfigType DefaultConfig;
	static void Release(QuantizedType& State)
	{
		if (State.SendQueue) { State.SendQueue->Queued.Decrement(); State.SendQueue->Release(); }
		FMemory::Memzero(&State, sizeof(State));
	}
	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs& Args)
	{
		auto& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
		Target = *reinterpret_cast<const QuantizedType*>(Args.Source);
		if (Target.SendQueue) { Target.SendQueue->AddRef(); Target.SendQueue->Queued.Increment(); }
	}
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs& Args)
	{
		Release(*reinterpret_cast<QuantizedType*>(Args.Source));
	}
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args)
	{
		const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
		QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
		Release(Target);
		Target.BitCount = Source.BitCount;
		Target.SendQueue = Source.SendQueue.GetReference();
		if (Target.SendQueue) { Target.SendQueue->AddRef(); Target.SendQueue->Queued.Increment(); }
		if (Source.BitCount <= SourceType::MaxBytes * 8) { FMemory::Memcpy(Target.Data, Source.Data, (Source.BitCount + 7) / 8); }
	}
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args)
	{
		const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
		SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
		Target = SourceType();
		Target.BitCount = Source.BitCount;
		if (Source.BitCount <= SourceType::MaxBytes * 8) { FMemory::Memcpy(Target.Data, Source.Data, (Source.BitCount + 7) / 8); }
	}
	static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
	{
		const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
		if (Value.BitCount > SourceType::MaxBytes * 8) { Context.SetError(GNetError_BitStreamError); return; }
		Context.GetBitStreamWriter()->WriteBits(Value.BitCount, 16);
		Context.GetBitStreamWriter()->WriteBitStream(Value.Data, 0, Value.BitCount);
	}
	static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
	{
		QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);
		Release(Value);
		const uint32 Bits = Context.GetBitStreamReader()->ReadBits(16);
		if (Bits > SourceType::MaxBytes * 8)
		{
			Context.SetError(GNetError_BitStreamError);
			return;
		}
		Value.BitCount = static_cast<uint16>(Bits);
		Context.GetBitStreamReader()->ReadBitStream(Value.Data, Bits);
	}
	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args)
	{
		if (Args.bStateIsQuantized)
		{
			const QuantizedType& A = *reinterpret_cast<const QuantizedType*>(Args.Source0);
			const QuantizedType& B = *reinterpret_cast<const QuantizedType*>(Args.Source1);
			return A.BitCount <= SourceType::MaxBytes * 8 && A.BitCount == B.BitCount && FMemory::Memcmp(A.Data, B.Data, (A.BitCount + 7) / 8) == 0;
		}
		const SourceType& A = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& B = *reinterpret_cast<const SourceType*>(Args.Source1);
		return A.BitCount <= SourceType::MaxBytes * 8 && A.BitCount == B.BitCount && FMemory::Memcmp(A.Data, B.Data, (A.BitCount + 7) / 8) == 0;
	}
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args)
	{
		return reinterpret_cast<const SourceType*>(Args.Source)->BitCount <= SourceType::MaxBytes * 8;
	}
};
const FProjectileCosmeticWireBatchNetSerializer::ConfigType FProjectileCosmeticWireBatchNetSerializer::DefaultConfig;
UE_NET_IMPLEMENT_SERIALIZER(FProjectileCosmeticWireBatchNetSerializer);

static const FName NAME_ProjectileCosmeticWireBatch(TEXT("ProjectileCosmeticWireBatch"));
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_ProjectileCosmeticWireBatch, FProjectileCosmeticWireBatchNetSerializer);

class FCosmeticWireRegistry final : private FNetSerializerRegistryDelegates
{
public:
	FCosmeticWireRegistry() : FNetSerializerRegistryDelegates(EFlags::ShouldBindLoadedModulesUpdatedDelegate) {}
	~FCosmeticWireRegistry() { UE_NET_UNREGISTER_NETSERIALIZER_INFO(NAME_ProjectileCosmeticWireBatch); }
	void OnPreFreezeNetSerializerRegistry() override { UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_ProjectileCosmeticWireBatch); }
};
static FCosmeticWireRegistry CosmeticWireRegistry;
}
#endif
