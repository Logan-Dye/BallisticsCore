// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsCosmeticTypes.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "Serialization/CustomVersion.h"

namespace FBallisticsCosmeticObjectVersion
{
	const FGuid GUID(0x9A3A8F22, 0x8B664763, 0xB1695ECB, 0x970E3081);

	enum Type
	{
		BeforeCustomVersionWasAdded = 0,
		DepthOrderedModifiers = 1,
		LatestVersion = DepthOrderedModifiers
	};
}

FCustomVersionRegistration GRegisterBallisticsCosmeticObjectVersion(
	FBallisticsCosmeticObjectVersion::GUID,
	FBallisticsCosmeticObjectVersion::LatestVersion,
	TEXT("BallisticsCosmeticObjectVersion"));

namespace
{
	constexpr int32 MaxResolvedLaunchEffects = 16;
	constexpr int32 MaxResolvedParameters = 16;
	constexpr int32 MaxParametersPerLaunchEffect = 16;
	constexpr int32 MaxEffectEditsPerModifier = 32;

	template <typename ParameterType, typename ValidatorType>
	void CopyEffectParameters(
		const TArray<ParameterType>& Source,
		TArray<ParameterType>& Target,
		int32& Remaining,
		ValidatorType IsValid)
	{
		const TArray<ParameterType> SourceCopy = Source;
		Target.Reset(FMath::Min(Source.Num(), Remaining));
		for (const ParameterType& Parameter : SourceCopy)
		{
			if (Remaining <= 0)
			{
				break;
			}
			if (!Parameter.Name.IsNone() && IsValid(Parameter))
			{
				Target.Add(Parameter);
				--Remaining;
			}
		}
	}

	void SanitizeEffectParameters(FBallisticsLaunchEffect& Effect)
	{
		int32 Remaining = MaxParametersPerLaunchEffect;
		CopyEffectParameters(
			Effect.BoolParameters,
			Effect.BoolParameters,
			Remaining,
			[](const FBallisticsNiagaraBoolParameter&) { return true; });
		CopyEffectParameters(
			Effect.IntParameters,
			Effect.IntParameters,
			Remaining,
			[](const FBallisticsNiagaraIntParameter&) { return true; });
		CopyEffectParameters(
			Effect.FloatParameters,
			Effect.FloatParameters,
			Remaining,
			[](const FBallisticsNiagaraFloatParameter& Parameter)
			{
				return FMath::IsFinite(Parameter.Value);
			});
		CopyEffectParameters(
			Effect.Vector2Parameters,
			Effect.Vector2Parameters,
			Remaining,
			[](const FBallisticsNiagaraVector2Parameter& Parameter)
			{
				return FMath::IsFinite(Parameter.Value.X) &&
					FMath::IsFinite(Parameter.Value.Y);
			});
		CopyEffectParameters(
			Effect.Vector3Parameters,
			Effect.Vector3Parameters,
			Remaining,
			[](const FBallisticsNiagaraVector3Parameter& Parameter)
			{
				return FMath::IsFinite(Parameter.Value.X) &&
					FMath::IsFinite(Parameter.Value.Y) &&
					FMath::IsFinite(Parameter.Value.Z);
			});
		CopyEffectParameters(
			Effect.ColorParameters,
			Effect.ColorParameters,
			Remaining,
			[](const FBallisticsNiagaraColorParameter& Parameter)
			{
				return FMath::IsFinite(Parameter.Value.R) &&
					FMath::IsFinite(Parameter.Value.G) &&
					FMath::IsFinite(Parameter.Value.B) &&
					FMath::IsFinite(Parameter.Value.A);
			});
		CopyEffectParameters(
			Effect.StaticMeshParameters,
			Effect.StaticMeshParameters,
			Remaining,
			[](const FBallisticsNiagaraStaticMeshParameter&) { return true; });
		CopyEffectParameters(
			Effect.MaterialParameters,
			Effect.MaterialParameters,
			Remaining,
			[](const FBallisticsNiagaraMaterialParameter&) { return true; });
		CopyEffectParameters(
			Effect.TextureParameters,
			Effect.TextureParameters,
			Remaining,
			[](const FBallisticsNiagaraTextureParameter&) { return true; });
	}

	void SanitizeEffects(
		const TArray<FBallisticsLaunchEffect>& Source,
		TArray<FBallisticsLaunchEffect>& Target)
	{
		Target.Reset(FMath::Min(Source.Num(), MaxResolvedLaunchEffects));
		for (int32 Index = 0;
			Index < FMath::Min(Source.Num(), MaxResolvedLaunchEffects);
			++Index)
		{
			Target.Add(Source[Index]);
		}
		for (FBallisticsLaunchEffect& Effect : Target)
		{
			Effect.Scale = FMath::Max(0.f, Effect.Scale);
			Effect.LifeSpan = FMath::Clamp(Effect.LifeSpan, 0.05f, 60.f);
			SanitizeEffectParameters(Effect);
		}
	}

	void ApplyEffectOperations(
		const TArray<FBallisticsLaunchEffectEdit>& Operations,
		TArray<FBallisticsLaunchEffect>& Effects)
	{
		for (int32 OperationIndex = 0;
			OperationIndex < FMath::Min(Operations.Num(), MaxEffectEditsPerModifier);
			++OperationIndex)
		{
			const FBallisticsLaunchEffectEdit& Edit = Operations[OperationIndex];
			if (Edit.EffectId.IsNone())
			{
				continue;
			}

			const int32 ExistingIndex = Effects.IndexOfByPredicate(
				[&Edit](const FBallisticsLaunchEffect& Existing)
				{
					return Existing.EffectId == Edit.EffectId;
				});
			switch (Edit.Operation)
			{
			case EBallisticsLaunchEffectOperation::Add:
				if (ExistingIndex == INDEX_NONE &&
					Effects.Num() < MaxResolvedLaunchEffects)
				{
					FBallisticsLaunchEffect Added = Edit.Effect;
					Added.EffectId = Edit.EffectId;
					Effects.Add(MoveTemp(Added));
				}
				break;
			case EBallisticsLaunchEffectOperation::Replace:
				if (ExistingIndex != INDEX_NONE)
				{
					FBallisticsLaunchEffect Replacement = Edit.Effect;
					Replacement.EffectId = Edit.EffectId;
					Effects[ExistingIndex] = MoveTemp(Replacement);
				}
				break;
			case EBallisticsLaunchEffectOperation::Remove:
				if (ExistingIndex != INDEX_NONE)
				{
					Effects.RemoveAt(ExistingIndex);
				}
				break;
			}
		}
	}

	template <typename ParameterType>
	void MergeParameters(
		const TArray<ParameterType>& Source,
		TArray<ParameterType>& Target)
	{
		for (const ParameterType& Parameter : Source)
		{
			if (Parameter.Name.IsNone())
			{
				continue;
			}

			const int32 ExistingIndex = Target.IndexOfByPredicate(
				[&Parameter](const ParameterType& Existing)
				{
					return Existing.Name == Parameter.Name;
				});
			if (ExistingIndex == INDEX_NONE)
			{
				if (Target.Num() < MaxResolvedParameters)
				{
					Target.Add(Parameter);
				}
			}
			else
			{
				Target[ExistingIndex] = Parameter;
			}
		}
	}
}

void UBallisticsLaunchCosmeticModifier::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FBallisticsCosmeticObjectVersion::GUID);
	Super::Serialize(Ar);
	if (Ar.IsLoading() && !HasAnyFlags(RF_ClassDefaultObject) &&
		Ar.CustomVer(FBallisticsCosmeticObjectVersion::GUID) <
			FBallisticsCosmeticObjectVersion::DepthOrderedModifiers)
	{
		bOverridePriority = true;
		bLegacyPriorityOrdering = true;
	}
}

FResolvedLaunchCosmetics UBallisticsCosmeticLibrary::ResolveLaunchCosmetics(
	const UBallisticsLaunchCosmeticProfile* Profile,
	TConstArrayView<TObjectPtr<UBallisticsLaunchCosmeticModifier>> Modifiers,
	bool bPreserveModifierOrder)
{
	FResolvedLaunchCosmetics Result;
	if (!Profile)
	{
		return Result;
	}

	Result.LaunchSound = Profile->LaunchSound;
	Result.DistantSound = Profile->DistantSound;
	SanitizeEffects(Profile->Effects, Result.Effects);
	MergeParameters(Profile->BoolParameters, Result.BoolParameters);
	MergeParameters(Profile->FloatParameters, Result.FloatParameters);
	Result.VolumeMultiplier = FMath::Max(0.f, Profile->VolumeMultiplier);
	Result.PitchMultiplier = FMath::Max(0.01f, Profile->PitchMultiplier);
	Result.AudibleRadius = FMath::Max(0.f, Profile->AudibleRadius);
	Result.VisualRadius = FMath::Max(0.f, Profile->VisualRadius);
	Result.PropagationSpeed = FMath::Max(1.f, Profile->PropagationSpeed);
	Result.bUsePropagationDelay = Profile->bUsePropagationDelay;

	TArray<const UBallisticsLaunchCosmeticModifier*> SortedModifiers;
	SortedModifiers.Reserve(Modifiers.Num());
	for (const UBallisticsLaunchCosmeticModifier* Modifier : Modifiers)
	{
		if (Modifier)
		{
			SortedModifiers.Add(Modifier);
		}
	}
	if (!bPreserveModifierOrder)
	{
		SortedModifiers.Sort(
			[](const UBallisticsLaunchCosmeticModifier& Left,
				const UBallisticsLaunchCosmeticModifier& Right)
			{
				if (Left.Priority != Right.Priority)
				{
					return Left.Priority < Right.Priority;
				}
				return Left.GetPathName() < Right.GetPathName();
			});
	}

	float EffectScaleMultiplier = 1.f;
	for (const UBallisticsLaunchCosmeticModifier* Modifier : SortedModifiers)
	{
		if (!Modifier)
		{
			continue;
		}
		if (Modifier->bOverrideLaunchSound)
		{
			Result.LaunchSound = Modifier->LaunchSoundOverride;
		}
		if (Modifier->bOverrideDistantSound)
		{
			Result.DistantSound = Modifier->DistantSoundOverride;
		}
		ApplyEffectOperations(Modifier->EffectOperations, Result.Effects);
		MergeParameters(Modifier->BoolParameterOverrides, Result.BoolParameters);
		MergeParameters(Modifier->FloatParameterOverrides, Result.FloatParameters);
		EffectScaleMultiplier *= FMath::Max(0.f, Modifier->EffectScaleMultiplier);
		Result.VolumeMultiplier *= FMath::Max(0.f, Modifier->VolumeMultiplier);
		Result.PitchMultiplier *= FMath::Max(0.01f, Modifier->PitchMultiplier);
		Result.AudibleRadius *= FMath::Max(0.f, Modifier->AudibleRadiusMultiplier);
		Result.VisualRadius *= FMath::Max(0.f, Modifier->VisualRadiusMultiplier);
	}

	for (FBallisticsLaunchEffect& Effect : Result.Effects)
	{
		Effect.Scale *= EffectScaleMultiplier;
		Effect.Scale = FMath::Max(0.f, Effect.Scale);
		Effect.LifeSpan = FMath::Clamp(Effect.LifeSpan, 0.05f, 60.f);
		SanitizeEffectParameters(Effect);
	}
	return Result;
}

void UBallisticsCosmeticLibrary::ApplyAudioParameters(
	UAudioComponent* Component,
	const TArray<FBallisticsCosmeticBoolParameter>& BoolParameters,
	const TArray<FBallisticsCosmeticFloatParameter>& FloatParameters)
{
	if (!Component)
	{
		return;
	}

	for (const FBallisticsCosmeticBoolParameter& Parameter : BoolParameters)
	{
		if (Parameter.bApplyToAudio && !Parameter.Name.IsNone())
		{
			Component->SetBoolParameter(Parameter.Name, Parameter.bValue);
		}
	}
	for (const FBallisticsCosmeticFloatParameter& Parameter : FloatParameters)
	{
		if (Parameter.bApplyToAudio && !Parameter.Name.IsNone() &&
			FMath::IsFinite(Parameter.Value))
		{
			Component->SetFloatParameter(Parameter.Name, Parameter.Value);
		}
	}
}

void UBallisticsCosmeticLibrary::ApplyNiagaraParameters(
	UNiagaraComponent* Component,
	const TArray<FBallisticsCosmeticBoolParameter>& BoolParameters,
	const TArray<FBallisticsCosmeticFloatParameter>& FloatParameters)
{
	if (!Component)
	{
		return;
	}

	for (const FBallisticsCosmeticBoolParameter& Parameter : BoolParameters)
	{
		if (Parameter.bApplyToNiagara && !Parameter.Name.IsNone())
		{
			Component->SetVariableBool(Parameter.Name, Parameter.bValue);
		}
	}
	for (const FBallisticsCosmeticFloatParameter& Parameter : FloatParameters)
	{
		if (Parameter.bApplyToNiagara && !Parameter.Name.IsNone() &&
			FMath::IsFinite(Parameter.Value))
		{
			Component->SetVariableFloat(Parameter.Name, Parameter.Value);
		}
	}
}

void UBallisticsCosmeticLibrary::ApplyLaunchEffectNiagaraParameters(
	UNiagaraComponent* Component,
	const FBallisticsLaunchEffect& LaunchEffect)
{
	if (!Component)
	{
		return;
	}

	for (const FBallisticsNiagaraBoolParameter& Parameter : LaunchEffect.BoolParameters)
	{
		if (!Parameter.Name.IsNone())
		{
			Component->SetVariableBool(Parameter.Name, Parameter.bValue);
		}
	}
	for (const FBallisticsNiagaraIntParameter& Parameter : LaunchEffect.IntParameters)
	{
		if (!Parameter.Name.IsNone())
		{
			Component->SetVariableInt(Parameter.Name, Parameter.Value);
		}
	}
	for (const FBallisticsNiagaraFloatParameter& Parameter : LaunchEffect.FloatParameters)
	{
		if (!Parameter.Name.IsNone() && FMath::IsFinite(Parameter.Value))
		{
			Component->SetVariableFloat(Parameter.Name, Parameter.Value);
		}
	}
	for (const FBallisticsNiagaraVector2Parameter& Parameter : LaunchEffect.Vector2Parameters)
	{
		if (!Parameter.Name.IsNone() && FMath::IsFinite(Parameter.Value.X) &&
			FMath::IsFinite(Parameter.Value.Y))
		{
			Component->SetVariableVec2(Parameter.Name, Parameter.Value);
		}
	}
	for (const FBallisticsNiagaraVector3Parameter& Parameter : LaunchEffect.Vector3Parameters)
	{
		if (!Parameter.Name.IsNone() && FMath::IsFinite(Parameter.Value.X) &&
			FMath::IsFinite(Parameter.Value.Y) && FMath::IsFinite(Parameter.Value.Z))
		{
			Component->SetVariableVec3(Parameter.Name, Parameter.Value);
		}
	}
	for (const FBallisticsNiagaraColorParameter& Parameter : LaunchEffect.ColorParameters)
	{
		if (!Parameter.Name.IsNone() && FMath::IsFinite(Parameter.Value.R) &&
			FMath::IsFinite(Parameter.Value.G) && FMath::IsFinite(Parameter.Value.B) &&
			FMath::IsFinite(Parameter.Value.A))
		{
			Component->SetVariableLinearColor(Parameter.Name, Parameter.Value);
		}
	}
	for (const FBallisticsNiagaraStaticMeshParameter& Parameter :
		LaunchEffect.StaticMeshParameters)
	{
		if (!Parameter.Name.IsNone())
		{
			Component->SetVariableStaticMesh(Parameter.Name, Parameter.Value.LoadSynchronous());
		}
	}
	for (const FBallisticsNiagaraMaterialParameter& Parameter :
		LaunchEffect.MaterialParameters)
	{
		if (!Parameter.Name.IsNone())
		{
			Component->SetVariableMaterial(Parameter.Name, Parameter.Value.LoadSynchronous());
		}
	}
	for (const FBallisticsNiagaraTextureParameter& Parameter :
		LaunchEffect.TextureParameters)
	{
		if (!Parameter.Name.IsNone())
		{
			Component->SetVariableTexture(Parameter.Name, Parameter.Value.LoadSynchronous());
		}
	}
}
