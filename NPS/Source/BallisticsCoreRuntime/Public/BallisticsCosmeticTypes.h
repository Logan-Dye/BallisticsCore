// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "BallisticsCosmeticTypes.generated.h"

class UBallisticsProjectileDataAsset;
class UBallisticsExplosiveDataAsset;
class AActor;
class UMaterialInterface;
class UAudioComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UTexture;
struct FProjectileCosmeticStaticTemplate;

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsCosmeticBoolParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bApplyToAudio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bApplyToNiagara = true;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsCosmeticFloatParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bApplyToAudio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bApplyToNiagara = true;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraBoolParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bValue = false;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraIntParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	int32 Value = 0;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraFloatParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	float Value = 0.f;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraVector2Parameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FVector2D Value = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraVector3Parameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FVector Value = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraColorParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FLinearColor Value = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraStaticMeshParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	TSoftObjectPtr<UStaticMesh> Value;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraMaterialParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	TSoftObjectPtr<UMaterialInterface> Value;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsNiagaraTextureParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	TSoftObjectPtr<UTexture> Value;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchEffect
{
	GENERATED_BODY()

	/** Stable role used by modifiers to add, replace, or remove this effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName EffectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	/** Optional launcher binding that selects the component containing SocketName. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName SourceBinding = NAME_None;

	/** Socket on the launcher's effect source component. None uses the supplied launch transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0"))
	float Scale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch",
		meta = (ClampMin = "0.05", ClampMax = "60"))
	float LifeSpan = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraBoolParameter> BoolParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraIntParameter> IntParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraFloatParameter> FloatParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraVector2Parameter> Vector2Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraVector3Parameter> Vector3Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraColorParameter> ColorParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraStaticMeshParameter> StaticMeshParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraMaterialParameter> MaterialParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Niagara Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsNiagaraTextureParameter> TextureParameters;
};

UENUM(BlueprintType)
enum class EBallisticsLaunchEffectOperation : uint8
{
	Add,
	Replace,
	Remove
};

/** One deterministic edit applied to a named effect in a launch profile. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchEffectEdit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	EBallisticsLaunchEffectOperation Operation = EBallisticsLaunchEffectOperation::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName EffectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch",
		meta = (EditCondition = "Operation != EBallisticsLaunchEffectOperation::Remove"))
	FBallisticsLaunchEffect Effect;
};

/** A contributor-owned component source that can replace a profile binding. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchSourceOverride
{
	GENERATED_BODY()

	/** SourceBinding used by launch effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName BindingName = NAME_None;

	/** Component tag used to find the source on the contributor's owning actor. None uses its root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName ComponentTag = NAME_None;

	/** Optional replacement for the effect's socket name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	FName SocketNameOverride = NAME_None;

	/** Enable an explicit selection priority. Attachment depth breaks equal-priority ties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	bool bOverridePriority = false;

	/** Higher values win. Used only when bOverridePriority is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch",
		meta = (EditCondition = "bOverridePriority"))
	int32 Priority = 0;
};

/**
 * Built-in cosmetics for one physical-surface impact.
 *
 * Asset references are soft so a dedicated server can route cosmetic events
 * without loading audio, Niagara systems, or decal materials.
 */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsImpactCosmetic
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0"))
	float EffectScale = 1.f;

	/** Hard lifetime for the spawned Niagara component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact",
		meta = (ClampMin = "0.05", ClampMax = "60"))
	float EffectLifeSpan = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	FVector DecalSize = FVector(8.f, 4.f, 4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0"))
	float DecalLifeSpan = 10.f;

	bool HasAnyAsset() const
	{
		return !Effect.IsNull() || !Sound.IsNull() || !DecalMaterial.IsNull();
	}

	bool IsDefaultValue() const
	{
		return !HasAnyAsset() &&
			FMath::IsNearlyEqual(EffectScale, 1.f) &&
			FMath::IsNearlyEqual(EffectLifeSpan, 2.f) &&
			FMath::IsNearlyEqual(VolumeMultiplier, 1.f) &&
			FMath::IsNearlyEqual(PitchMultiplier, 1.f) &&
			DecalSize.Equals(FVector(8.f, 4.f, 4.f)) &&
			FMath::IsNearlyEqual(DecalLifeSpan, 10.f);
	}
};

/** Built-in cosmetics for an explosive detonation. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsDetonationCosmetic
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "0"))
	float EffectScale = 1.f;

	/** Hard lifetime for the spawned Niagara component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation",
		meta = (ClampMin = "0.05", ClampMax = "60"))
	float EffectLifeSpan = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	/** Maximum distance at which NPS dispatches this detonation to a listener. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "0"))
	float AudibleRadius = 100000.f;

	/** Maximum distance at which detonation visuals are routed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "0"))
	float VisualRadius = 100000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation", meta = (ClampMin = "1"))
	float PropagationSpeed = 34300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detonation")
	bool bUsePropagationDelay = true;

	bool HasAnyAsset() const
	{
		return !Effect.IsNull() || !Sound.IsNull();
	}
};

/**
 * Base launch signature for a projectile launcher.
 *
 * This belongs to the launcher, not its projectile. Different projectile
 * sources can launch the same projectile with different sound and visual
 * signatures.
 */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsLaunchCosmeticProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio")
	TSoftObjectPtr<USoundBase> LaunchSound;

	/** Optional distant report or environmental tail played with LaunchSound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio")
	TSoftObjectPtr<USoundBase> DistantSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual",
		meta = (TitleProperty = "EffectId"))
	TArray<FBallisticsLaunchEffect> Effects;

	/** Named bool values applied to launch audio and Niagara instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsCosmeticBoolParameter> BoolParameters;

	/** Named float values applied to launch audio and Niagara instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsCosmeticFloatParameter> FloatParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	/** Maximum listener distance for launch-event routing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0"))
	float AudibleRadius = 200000.f;

	/** Maximum listener distance for immediate launch visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual", meta = (ClampMin = "0"))
	float VisualRadius = 20000.f;

	/** Propagation speed in cm/s. Earth air at roughly 20 C is about 34300 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "1"))
	float PropagationSpeed = 34300.f;

	/** Delay audio by distance while still spawning visual effects immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio")
	bool bUsePropagationDelay = true;
};

/** Launch audio, effect, parameter, and source overrides from one contributor. */
UCLASS(BlueprintType)
class BALLISTICSCORERUNTIME_API UBallisticsLaunchCosmeticModifier : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Use Priority instead of attachment depth when contributors are ordered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch")
	bool bOverridePriority = false;

	/** Higher priorities apply later. Used only when bOverridePriority is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch",
		meta = (EditCondition = "bOverridePriority"))
	int32 Priority = 0;

	virtual void Serialize(FArchive& Ar) override;
	bool UsesLegacyPriorityOrdering() const
	{
		return bOverridePriority && bLegacyPriorityOrdering;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio")
	bool bOverrideLaunchSound = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio",
		meta = (EditCondition = "bOverrideLaunchSound"))
	TSoftObjectPtr<USoundBase> LaunchSoundOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio")
	bool bOverrideDistantSound = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio",
		meta = (EditCondition = "bOverrideDistantSound"))
	TSoftObjectPtr<USoundBase> DistantSoundOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual",
		meta = (TitleProperty = "EffectId"))
	TArray<FBallisticsLaunchEffectEdit> EffectOperations;

	/** Source bindings supplied by the contributor that owns this modifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual",
		meta = (TitleProperty = "BindingName"))
	TArray<FBallisticsLaunchSourceOverride> SourceOverrides;

	/** Values with matching names replace profile bool parameters. New names are added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsCosmeticBoolParameter> BoolParameterOverrides;

	/** Values with matching names replace profile float parameters. New names are added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Parameters",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsCosmeticFloatParameter> FloatParameterOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual", meta = (ClampMin = "0"))
	float EffectScaleMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Audio", meta = (ClampMin = "0"))
	float AudibleRadiusMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch|Visual", meta = (ClampMin = "0"))
	float VisualRadiusMultiplier = 1.f;

private:
	UPROPERTY()
	bool bLegacyPriorityOrdering = false;
};

/** Fully resolved launch signature used locally after applying a modifier. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FResolvedLaunchCosmetics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	TSoftObjectPtr<USoundBase> LaunchSound;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	TSoftObjectPtr<USoundBase> DistantSound;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	TArray<FBallisticsLaunchEffect> Effects;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	TArray<FBallisticsCosmeticBoolParameter> BoolParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	TArray<FBallisticsCosmeticFloatParameter> FloatParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	float VolumeMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	float PitchMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	float AudibleRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	float VisualRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	float PropagationSpeed = 34300.f;

	UPROPERTY(BlueprintReadOnly, Category = "Launch")
	bool bUsePropagationDelay = true;

	bool HasAnyAsset() const
	{
		if (!LaunchSound.IsNull() || !DistantSound.IsNull())
		{
			return true;
		}
		for (const FBallisticsLaunchEffect& LaunchEffect : Effects)
		{
			if (!LaunchEffect.Effect.IsNull())
			{
				return true;
			}
		}
		return false;
	}
};

UCLASS()
class BALLISTICSCORERUNTIME_API UBallisticsCosmeticLibrary : public UObject
{
	GENERATED_BODY()

public:
	static FResolvedLaunchCosmetics ResolveLaunchCosmetics(
		const UBallisticsLaunchCosmeticProfile* Profile,
		TConstArrayView<TObjectPtr<UBallisticsLaunchCosmeticModifier>> Modifiers,
		bool bPreserveModifierOrder = false);

	static void ApplyAudioParameters(
		UAudioComponent* Component,
		const TArray<FBallisticsCosmeticBoolParameter>& BoolParameters,
		const TArray<FBallisticsCosmeticFloatParameter>& FloatParameters);

	static void ApplyNiagaraParameters(
		UNiagaraComponent* Component,
		const TArray<FBallisticsCosmeticBoolParameter>& BoolParameters,
		const TArray<FBallisticsCosmeticFloatParameter>& FloatParameters);

	static void ApplyLaunchEffectNiagaraParameters(
		UNiagaraComponent* Component,
		const FBallisticsLaunchEffect& LaunchEffect);
};

UENUM(BlueprintType)
enum class EProjectileCosmeticEventType : uint8
{
	Launch,
	Impact,
	FlyBy,
	Detonation,
	SequenceTransform
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EProjectileCosmeticPlayback : uint8
{
	None = 0,
	Visual = 1 << 0,
	Audio = 1 << 1,
	All = (1 << 0) | (1 << 1)
};
ENUM_CLASS_FLAGS(EProjectileCosmeticPlayback);

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchEffectTransform
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bValid = true;
};

/** Local resolved event; the relay registers its static fields once per listener. */
USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FProjectileCosmeticEvent
{
	GENERATED_BODY()
	// Local immutable transport metadata, never reflected or replicated with the rich event.
	TSharedPtr<const FProjectileCosmeticStaticTemplate> WireTemplate;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	EProjectileCosmeticEventType Type = EProjectileCosmeticEventType::Launch;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	int32 EventID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	int32 SequenceID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	/** Firing actor used for receiver-local socket resolution when net relevant. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TObjectPtr<AActor> SourceActor = nullptr;

	/** Stable launch-audio owner, retained when remote audio strips actor references. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FGuid AudioSourceID;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TArray<FBallisticsLaunchEffectTransform> LaunchEffectTransforms;

	/** Resolved launch effects. Their transforms use matching array indices. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TArray<FBallisticsLaunchEffect> LaunchEffects;

	/** Resolved sound layers for this event. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TSoftObjectPtr<UNiagaraSystem> Effect;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TArray<FBallisticsCosmeticBoolParameter> BoolParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TArray<FBallisticsCosmeticFloatParameter> FloatParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	uint8 ImpactResponse = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float EffectScale = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float EffectLifeSpan = 2.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float VolumeMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float PitchMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	FVector DecalSize = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float DecalLifeSpan = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float VisualRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float AudibleRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float PropagationSpeed = 34300.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bUsePropagationDelay = true;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float EmissionServerTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float MissDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float ProjectileSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bPassesRight = false;

	/** Selects which built-in portions of this event may play. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics", meta = (Bitmask, BitmaskEnum = "/Script/BallisticsCoreRuntime.EProjectileCosmeticPlayback"))
	uint8 PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::All);

	/** True when acoustic propagation was already enforced before network delivery. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bServerDelayedAudio = false;
};

UENUM()
enum class ELaunchSequenceCommandType : uint8
{
	Begin,
	End
};

/** Reliable state transition for remotely reconstructed sustained launch effects. */
USTRUCT()
struct BALLISTICSCORERUNTIME_API FLaunchSequenceCommand
{
	GENERATED_BODY()

	UPROPERTY()
	ELaunchSequenceCommandType Type = ELaunchSequenceCommandType::Begin;

	UPROPERTY()
	int32 SequenceID = INDEX_NONE;

	UPROPERTY()
	FProjectileCosmeticEvent LaunchEvent;

	UPROPERTY()
	float LaunchesPerSecond = 1.f;
};
