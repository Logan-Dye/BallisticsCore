// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileLauncherComponent.h"
#include "BallisticsLaunchContributorComponent.h"
#include "BallisticsLaunchValidation.h"
#include "ProjectileBallisticsComponent.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileCosmeticsManager.h"
#include "BallisticsCosmeticWire.h"
#include "BallisticsCoreSettings.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "BallisticsDamageInterface.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsDetonationComponent.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsDebugDraw.h"
#include "BallisticsSpreadDataAsset.h"
#include "BallisticsTrajectoryLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"

namespace
{
	void PopulateResolvedWireEvent(FProjectileCosmeticEvent& Event, const FResolvedLaunchCosmetics& Resolved, bool bRouteCosmeticEventsWithoutAssets, float CustomCosmeticRoutingRadius)
	{
	const int32 EffectCount = FMath::Min(Resolved.Effects.Num(), 16);
	Event.LaunchEffects.Append(Resolved.Effects.GetData(), EffectCount);
	if (!Resolved.LaunchSound.IsNull())
	{
		Event.Sounds.Add(Resolved.LaunchSound);
	}
	if (!Resolved.DistantSound.IsNull())
	{
		Event.Sounds.Add(Resolved.DistantSound);
	}
	Event.BoolParameters = Resolved.BoolParameters;
	Event.FloatParameters = Resolved.FloatParameters;
	Event.VolumeMultiplier = Resolved.VolumeMultiplier;
	Event.PitchMultiplier = Resolved.PitchMultiplier;
	Event.VisualRadius = Resolved.VisualRadius;
	Event.AudibleRadius = Resolved.AudibleRadius;
	if (bRouteCosmeticEventsWithoutAssets)
	{
		Event.VisualRadius = FMath::Max(Event.VisualRadius, CustomCosmeticRoutingRadius);
		Event.AudibleRadius = FMath::Max(Event.AudibleRadius, CustomCosmeticRoutingRadius);
	}
	Event.PropagationSpeed = Resolved.PropagationSpeed;
	Event.bUsePropagationDelay = Resolved.bUsePropagationDelay;
	}

	void PopulateImpactCosmeticEvent(
		FProjectileCosmeticEvent& Event,
		const UBallisticsProjectileDataAsset* ProjectileData,
		EPhysicalSurface SurfaceType)
	{
		if (!ProjectileData)
		{
			return;
		}
		const FBallisticsImpactCosmetic& Cosmetic =
			ProjectileData->GetImpactCosmetic(SurfaceType);
		Event.Effect = Cosmetic.Effect;
		Event.DecalMaterial = Cosmetic.DecalMaterial;
		if (!Cosmetic.Sound.IsNull())
		{
			Event.Sounds.Add(Cosmetic.Sound);
		}
		Event.EffectScale = Cosmetic.EffectScale;
		Event.EffectLifeSpan = Cosmetic.EffectLifeSpan;
		Event.VolumeMultiplier = Cosmetic.VolumeMultiplier;
		Event.PitchMultiplier = Cosmetic.PitchMultiplier;
		Event.DecalSize = Cosmetic.DecalSize;
		Event.DecalLifeSpan = Cosmetic.DecalLifeSpan;
		Event.VisualRadius = ProjectileData->ImpactVisualRadius;
		Event.AudibleRadius = ProjectileData->ImpactAudibleRadius;
	}

	void PopulateFlyByCosmeticEvent(
		FProjectileCosmeticEvent& Event,
		const UBallisticsProjectileDataAsset* ProjectileData)
	{
		if (ProjectileData && !ProjectileData->FlyBySound.IsNull())
		{
			Event.Sounds.Add(ProjectileData->FlyBySound);
		}
		Event.PlaybackMask = static_cast<uint8>(EProjectileCosmeticPlayback::Audio);
		Event.bUsePropagationDelay = false;
	}

	void PopulateDetonationCosmeticEvent(
		FProjectileCosmeticEvent& Event,
		const UBallisticsExplosiveDataAsset* ExplosiveData)
	{
		if (!ExplosiveData)
		{
			return;
		}
		const FBallisticsDetonationCosmetic& Cosmetic =
			ExplosiveData->DetonationCosmetic;
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
	}

	bool NearlyEqualRelative(float Left, float Right, float Tolerance = 0.0001f)
	{
		const float Scale = FMath::Max3(1.f, FMath::Abs(Left), FMath::Abs(Right));
		return FMath::Abs(Left - Right) <= Scale * Tolerance;
	}

	bool MatchesCanonicalConditions(
		const FProjectileInitialConditions& Reported,
		const FProjectileInitialConditions& Canonical)
	{
		return !Reported.Origin.ContainsNaN() &&
			!Reported.Direction.ContainsNaN() &&
			!Reported.WindAcceleration.ContainsNaN() &&
			FMath::IsFinite(Reported.InitialSpeed) &&
			FMath::IsFinite(Reported.Mass) &&
			FMath::IsFinite(Reported.Diameter) &&
			FMath::IsFinite(Reported.DragCoefficient) &&
			Reported.Origin.Equals(Canonical.Origin, 1.f) &&
			FVector::DotProduct(
				Reported.Direction.GetSafeNormal(),
				Canonical.Direction.GetSafeNormal()) >= 0.9999f &&
			NearlyEqualRelative(Reported.InitialSpeed, Canonical.InitialSpeed) &&
			NearlyEqualRelative(Reported.Mass, Canonical.Mass) &&
			NearlyEqualRelative(Reported.Diameter, Canonical.Diameter) &&
			NearlyEqualRelative(
				Reported.DragCoefficient,
				Canonical.DragCoefficient) &&
			Reported.WindAcceleration.Equals(Canonical.WindAcceleration, 0.1f) &&
			Reported.RandomSeed == Canonical.RandomSeed;
	}

	bool FlightSegmentsMatch(
		const FProjectileFlightSegment& Left,
		const FProjectileFlightSegment& Right)
	{
		return Left.StartLocation.Equals(Right.StartLocation, 0.01f) &&
			Left.EndLocation.Equals(Right.EndLocation, 0.01f) &&
			Left.ImpactResponse == Right.ImpactResponse &&
			Left.SurfaceType.GetValue() == Right.SurfaceType.GetValue() &&
			Left.ImpactNormal.Equals(Right.ImpactNormal, 0.0001f) &&
			Left.HitActor.Get() == Right.HitActor.Get() &&
			Left.HitBoneName == Right.HitBoneName &&
			NearlyEqualRelative(Left.SpeedAtStart, Right.SpeedAtStart) &&
			NearlyEqualRelative(Left.TimeAtImpact, Right.TimeAtImpact);
	}

	bool MatchesFlightPrefix(
		const FProjectileFlightLog& Report,
		const TArray<FProjectileFlightSegment>& AcceptedSegments,
		int32 SegmentCount)
	{
		if (SegmentCount < 0 || Report.Segments.Num() < SegmentCount ||
			AcceptedSegments.Num() < SegmentCount)
		{
			return false;
		}
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			if (!FlightSegmentsMatch(Report.Segments[Index], AcceptedSegments[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool PreservesAcceptedFlightPrefix(
		const FProjectileFlightLog& Report,
		const TArray<FProjectileFlightSegment>& AcceptedSegments)
	{
		return MatchesFlightPrefix(
			Report,
			AcceptedSegments,
			AcceptedSegments.Num());
	}
}

UProjectileLauncherComponent::UProjectileLauncherComponent()
{
	AudioSourceID = BallisticsCosmeticWire::NewAudioSourceID();
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
#if UE_WITH_IRIS
	// Iris uses registered subobjects; legacy replication also supports this list.
	bReplicateUsingRegisteredSubObjectList = true;
#endif
	LaunchCounter = 0;
	NextProjectileID = 0;
	NextLaunchRequestID = 0;
}

void UProjectileLauncherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndLaunchSequence();
	LaunchCreditBatches.Reset();
	RemoteLaunchAdmissions.Reset();
	RecentRemoteProjectileIDs.Reset();
	Super::EndPlay(EndPlayReason);
}

void UProjectileLauncherComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProjectileLauncherComponent, ProjectileDataAsset);
	DOREPLIFETIME(UProjectileLauncherComponent, LaunchCosmeticProfile);
	DOREPLIFETIME(UProjectileLauncherComponent, MinLaunchInterval);
	DOREPLIFETIME(UProjectileLauncherComponent, bRequireLaunchCredits);
}

void UProjectileLauncherComponent::GrantLaunchCredits(int32 Count, float ExpirySeconds)
{
	if (Count <= 0 || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	RefreshLaunchAdmissionOwner();
	const double Now = GetLaunchAdmissionTime();
	PruneLaunchCredits(Now);
	const float Lifetime = ExpirySeconds > 0.f
		? ExpirySeconds
		: UBallisticsCoreSettings::Get()->DefaultLaunchCreditExpirySeconds;
	if (!FMath::IsFinite(Lifetime) || Lifetime <= 0.f)
	{
		return;
	}

	FBallisticsLaunchCreditBatch& Batch = LaunchCreditBatches.AddDefaulted_GetRef();
	Batch.Remaining = Count;
	Batch.ExpiryTime = Now + Lifetime;
}

int32 UProjectileLauncherComponent::GetAvailableLaunchCredits() const
{
	UProjectileLauncherComponent* MutableThis =
		const_cast<UProjectileLauncherComponent*>(this);
	MutableThis->RefreshLaunchAdmissionOwner();
	const double Now = GetLaunchAdmissionTime();
	MutableThis->PruneLaunchCredits(Now);
	int64 Total = 0;
	for (const FBallisticsLaunchCreditBatch& Batch : LaunchCreditBatches)
	{
		if (Batch.Remaining > 0 && Batch.ExpiryTime > Now)
		{
			Total += Batch.Remaining;
		}
	}
	return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
}

void UProjectileLauncherComponent::ClearLaunchCredits()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		LaunchCreditBatches.Reset();
	}
}

bool UProjectileLauncherComponent::SetProjectileDataAsset(
	UBallisticsProjectileDataAsset* NewProjectileData)
{
	UWorld* World = GetWorld();
	if (!World || (World->GetNetMode() != NM_Standalone &&
		(!GetOwner() || !GetOwner()->HasAuthority())))
	{
		NPS_LOG(Warning,
			TEXT("NPS rejected a client-side projectile data change on %s. "
			     "Set replicated launcher configuration on the server."),
			*GetPathName());
		return false;
	}

	ProjectileDataAsset = NewProjectileData;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
	return true;
}

ELaunchAuthorityMode UProjectileLauncherComponent::GetActiveAuthorityMode() const
{
	if (ProjectileDataAsset && ProjectileDataAsset->bOverrideAuthorityMode)
	{
		return ProjectileDataAsset->AuthorityModeOverride;
	}
	return UBallisticsCoreSettings::Get()->LaunchAuthorityMode;
}

bool UProjectileLauncherComponent::SetLaunchCosmeticProfile(
	UBallisticsLaunchCosmeticProfile* NewProfile)
{
	UWorld* World = GetWorld();
	if (!World || (World->GetNetMode() != NM_Standalone &&
		(!GetOwner() || !GetOwner()->HasAuthority())))
	{
		NPS_LOG(Warning,
			TEXT("NPS rejected a client-side launch cosmetic profile change on %s. "
			     "Set replicated cosmetic configuration on the server."),
			*GetPathName());
		return false;
	}

	LaunchCosmeticProfile = NewProfile;
	bLaunchContributorCacheDirty = true;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
	return true;
}

void UProjectileLauncherComponent::OnRep_LaunchCosmeticProfile()
{
	bLaunchContributorCacheDirty = true;
}

void UProjectileLauncherComponent::SetLaunchEffectSourceComponent(
	USceneComponent* NewSourceComponent)
{
	LaunchEffectSourceComponent.OverrideComponent = NewSourceComponent;
}

bool UProjectileLauncherComponent::SetLaunchEffectSourceBinding(
	FName Name,
	USceneComponent* NewSourceComponent)
{
	if (Name.IsNone())
	{
		return false;
	}

	const int32 ExistingIndex = LaunchEffectSourceBindings.IndexOfByPredicate(
		[Name](const FBallisticsLaunchEffectSourceBinding& Binding)
		{
			return Binding.Name == Name;
		});
	if (!NewSourceComponent)
	{
		if (ExistingIndex != INDEX_NONE)
		{
			LaunchEffectSourceBindings.RemoveAt(ExistingIndex);
		}
		return true;
	}

	FBallisticsLaunchEffectSourceBinding* Binding =
		ExistingIndex != INDEX_NONE
			? &LaunchEffectSourceBindings[ExistingIndex]
			: &LaunchEffectSourceBindings.AddDefaulted_GetRef();
	Binding->Name = Name;
	Binding->SourceComponent.OverrideComponent = NewSourceComponent;
	return true;
}

USceneComponent* UProjectileLauncherComponent::GetLaunchEffectSourceComponent(
	FName Name) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (Name.IsNone())
	{
		return Cast<USceneComponent>(
			LaunchEffectSourceComponent.GetComponent(OwnerActor));
	}

	const FBallisticsLaunchEffectSourceBinding* Binding =
		LaunchEffectSourceBindings.FindByPredicate(
			[Name](const FBallisticsLaunchEffectSourceBinding& Candidate)
			{
				return Candidate.Name == Name;
			});
	return Binding
		? Cast<USceneComponent>(Binding->SourceComponent.GetComponent(OwnerActor))
		: nullptr;
}

void UProjectileLauncherComponent::RegisterLaunchContributor(
	UBallisticsLaunchContributorComponent* Contributor)
{
	if (Contributor)
	{
		LaunchContributors.AddUnique(Contributor);
		bLaunchContributorCacheDirty = true;
	}
}

void UProjectileLauncherComponent::UnregisterLaunchContributor(
	UBallisticsLaunchContributorComponent* Contributor)
{
	LaunchContributors.RemoveAll(
		[Contributor](const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak)
		{
			return !Weak.IsValid() || Weak.Get() == Contributor;
		});
	bLaunchContributorCacheDirty = true;
}

void UProjectileLauncherComponent::NotifyLaunchContributorChanged(
	UBallisticsLaunchContributorComponent* Contributor)
{
	if (Contributor && LaunchContributors.Contains(Contributor))
	{
		bLaunchContributorCacheDirty = true;
	}
}

void UProjectileLauncherComponent::RebuildLaunchContributorCache() const
{
	if (!bLaunchContributorCacheDirty)
	{
		return;
	}

	UProjectileLauncherComponent* MutableThis =
		const_cast<UProjectileLauncherComponent*>(this);
	MutableThis->LaunchContributors.RemoveAll(
		[](const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak)
		{
			return !Weak.IsValid();
		});
	SortedLaunchContributors.Reset(LaunchContributors.Num());
	for (const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak :
		LaunchContributors)
	{
		if (const UBallisticsLaunchContributorComponent* Contributor = Weak.Get();
			Contributor && Contributor->GetModifier())
		{
			SortedLaunchContributors.Add(Weak);
		}
	}
	SortedLaunchContributors.Sort(
		[](const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Left,
			const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Right)
		{
			const UBallisticsLaunchCosmeticModifier* LeftModifier =
				Left.IsValid() ? Left->GetModifier() : nullptr;
			const UBallisticsLaunchCosmeticModifier* RightModifier =
				Right.IsValid() ? Right->GetModifier() : nullptr;
			const int32 LeftPriority = LeftModifier && LeftModifier->bOverridePriority
				? LeftModifier->Priority
				: 0;
			const int32 RightPriority = RightModifier && RightModifier->bOverridePriority
				? RightModifier->Priority
				: 0;
			if (LeftPriority != RightPriority)
			{
				return LeftPriority < RightPriority;
			}
			if (LeftModifier && RightModifier &&
				LeftModifier->UsesLegacyPriorityOrdering() &&
				RightModifier->UsesLegacyPriorityOrdering())
			{
				return LeftModifier->GetPathName() < RightModifier->GetPathName();
			}
			const int32 LeftDepth = Left.IsValid() ? Left->GetAttachmentDepth() : INDEX_NONE;
			const int32 RightDepth = Right.IsValid() ? Right->GetAttachmentDepth() : INDEX_NONE;
			if (LeftDepth != RightDepth)
			{
				return LeftDepth < RightDepth;
			}
			return Left.IsValid() && Right.IsValid()
				? Left->GetPathName() < Right->GetPathName()
				: Left.IsValid();
		});
	constexpr int32 MaxLaunchContributors = 8;
	if (SortedLaunchContributors.Num() > MaxLaunchContributors)
	{
		if (!bWarnedLaunchContributorBudget)
		{
			bWarnedLaunchContributorBudget = true;
			NPS_LOG(Warning,
				TEXT("NPS launcher %s has more than %d cosmetic contributors. Lowest-priority contributors were ignored."),
				*GetPathName(),
				MaxLaunchContributors);
		}
		SortedLaunchContributors.RemoveAt(
			0,
			SortedLaunchContributors.Num() - MaxLaunchContributors,
			EAllowShrinking::No);
	}

	CachedLaunchModifiers.Reset(SortedLaunchContributors.Num());
	for (const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak :
		SortedLaunchContributors)
	{
		if (UBallisticsLaunchContributorComponent* Contributor = Weak.Get())
		{
			if (UBallisticsLaunchCosmeticModifier* Modifier = Contributor->GetModifier())
			{
				CachedLaunchModifiers.Add(Modifier);
			}
		}
	}
	CachedResolvedLaunchCosmetics = UBallisticsCosmeticLibrary::ResolveLaunchCosmetics(
		LaunchCosmeticProfile,
		CachedLaunchModifiers,
		true);
	FProjectileCosmeticEvent WireEvent;
	PopulateResolvedWireEvent(WireEvent, CachedResolvedLaunchCosmetics, bRouteCosmeticEventsWithoutAssets, CustomCosmeticRoutingRadius);
	CachedLaunchWireTemplate = BallisticsCosmeticWire::Prepare(WireEvent);
	bLaunchContributorCacheDirty = false;
}

const FResolvedLaunchCosmetics& UProjectileLauncherComponent::GetResolvedLaunchCosmetics() const
{
	RebuildLaunchContributorCache();
	return CachedResolvedLaunchCosmetics;
}

FGuid UProjectileLauncherComponent::GetPreparedLaunchCosmeticKey() const
{
	RebuildLaunchContributorCache();
	return CachedLaunchWireTemplate->Key;
}

const TArray<TObjectPtr<UBallisticsLaunchCosmeticModifier>>&
UProjectileLauncherComponent::GetActiveLaunchModifiers() const
{
	RebuildLaunchContributorCache();
	return CachedLaunchModifiers;
}

void UProjectileLauncherComponent::BeginLaunchSequence(
	const FVector& Origin,
	const FVector& Direction,
	float LaunchesPerSecond)
{
	if (!bEnableCosmeticRouting || !HasConfiguredLaunchCosmetics() ||
		!ValidateCosmeticVector(Origin) || !Direction.GetSafeNormal().IsNormalized())
	{
		return;
	}

	if (bLaunchSequenceActive)
	{
		EndLaunchSequence();
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float MaxRate = Settings
		? FMath::Clamp(Settings->MaxLaunchCosmeticRate, 1.f, 120.f)
		: 60.f;

	bLaunchSequenceActive = true;
	ActiveLaunchSequenceID = GenerateSequenceID();
	ActiveLaunchesPerSecond = FMath::Clamp(LaunchesPerSecond, 0.1f, MaxRate);
	LastSequenceTransformSendTime = -BIG_NUMBER;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector SafeDirection = Direction.GetSafeNormal();
	if (World->GetNetMode() == NM_Client)
	{
		if (IsLocallyControlledLauncher())
		{
			ServerBeginLaunchSequence(
				Origin,
				SafeDirection,
				ActiveLaunchesPerSecond,
				ActiveLaunchSequenceID);
		}
		return;
	}

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		FLaunchSequenceCommand Command;
		Command.Type = ELaunchSequenceCommandType::Begin;
		Command.SequenceID = ActiveLaunchSequenceID;
		Command.LaunchEvent = BuildLaunchCosmeticEvent(Origin, SafeDirection);
		Command.LaunchEvent.SequenceID = ActiveLaunchSequenceID;
		Command.LaunchesPerSecond = ActiveLaunchesPerSecond;

		APlayerController* Excluded = IsLocallyControlledLauncher()
			? GetOwningPlayerController()
			: nullptr;
		Manager->QueueSequenceCommand(
			Command,
			Excluded);
	}
}

void UProjectileLauncherComponent::EndLaunchSequence()
{
	if (!bLaunchSequenceActive)
	{
		return;
	}

	const int32 SequenceID = ActiveLaunchSequenceID;
	bLaunchSequenceActive = false;
	ActiveLaunchSequenceID = INDEX_NONE;
	ActiveLaunchesPerSecond = 0.f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		if (IsLocallyControlledLauncher())
		{
			ServerEndLaunchSequence(SequenceID);
		}
		return;
	}

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		FLaunchSequenceCommand Command;
		Command.Type = ELaunchSequenceCommandType::End;
		Command.SequenceID = SequenceID;
		Command.LaunchEvent = BuildLaunchCosmeticEvent(
			GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector,
			GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector);

		APlayerController* Excluded = IsLocallyControlledLauncher()
			? GetOwningPlayerController()
			: nullptr;
		Manager->QueueSequenceCommand(
			Command,
			Excluded);
	}
}

void UProjectileLauncherComponent::DispatchLaunchCosmetic(
	const FVector& Origin,
	const FVector& Direction)
{
	if (!bEnableCosmeticRouting || !HasConfiguredLaunchCosmetics())
	{
		return;
	}

	UWorld* World = GetWorld();
	UProjectileCosmeticsManager* Manager = GetCosmeticsManager();
	if (!World || !Manager)
	{
		return;
	}

	const bool bLocalLauncher = IsLocallyControlledLauncher();
	if (World->GetNetMode() == NM_Standalone)
	{
		Manager->PlayLocalEvent(BuildLaunchCosmeticEvent(Origin, Direction));
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		if (!bLocalLauncher)
		{
			return;
		}

		// The initiating client always gets exact, immediate feedback. Remote
		// delivery is independent of the launcher actor's relevancy.
		Manager->PlayLocalEvent(BuildLaunchCosmeticEvent(Origin, Direction));

		if (bLaunchSequenceActive)
		{
			const float Now = World->GetTimeSeconds();
			if (Now - LastSequenceTransformSendTime >= 0.1f)
			{
				LastSequenceTransformSendTime = Now;
				ServerUpdateLaunchSequence(Origin, Direction, ActiveLaunchSequenceID);
			}
		}
		else
		{
			ServerNotifyLaunchCosmetic(Origin, Direction);
		}
		return;
	}

	APlayerController* ExcludedController = nullptr;
	if (bLocalLauncher)
	{
		Manager->PlayLocalEvent(BuildLaunchCosmeticEvent(Origin, Direction));
		ExcludedController = GetOwningPlayerController();
	}

	if (bLaunchSequenceActive)
	{
		const float Now = World->GetTimeSeconds();
		if (Now - LastSequenceTransformSendTime >= 0.1f)
		{
			LastSequenceTransformSendTime = Now;
			QueueSequenceTransform(Origin, Direction, ExcludedController);
		}
	}
	else
	{
		QueueLaunchCosmetic(Origin, Direction, ExcludedController);
	}
}

void UProjectileLauncherComponent::QueueLaunchCosmetic(
	const FVector& Origin,
	const FVector& Direction,
	APlayerController* ExcludedController)
{
	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueBroadcastEvent(
			BuildLaunchCosmeticEvent(Origin, Direction),
			ExcludedController);
	}
}

void UProjectileLauncherComponent::QueueSequenceTransform(
	const FVector& Origin,
	const FVector& Direction,
	APlayerController* ExcludedController)
{
	if (!bLaunchSequenceActive)
	{
		return;
	}

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		FProjectileCosmeticEvent Event = BuildLaunchCosmeticEvent(Origin, Direction);
		Event.Type = EProjectileCosmeticEventType::SequenceTransform;
		Event.SequenceID = ActiveLaunchSequenceID;

		Manager->QueueBroadcastEvent(
			Event,
			ExcludedController);
	}
}

FProjectileCosmeticEvent UProjectileLauncherComponent::BuildLaunchCosmeticEvent(
	const FVector& Origin,
	const FVector& Direction)
{
	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Launch;
	Event.EventID = GenerateCosmeticEventID();
	Event.Location = Origin;
	Event.Direction = Direction.GetSafeNormal();
	Event.SourceActor = GetOwner();
	Event.AudioSourceID = AudioSourceID;
	Event.EmissionServerTime = GetSynchronizedTime();

	const FResolvedLaunchCosmetics& Resolved = GetResolvedLaunchCosmetics();
	PopulateResolvedWireEvent(Event, Resolved, bRouteCosmeticEventsWithoutAssets, CustomCosmeticRoutingRadius);
	if (!CachedLaunchWireTemplate || CachedLaunchWireTemplate->Event.VisualRadius != Event.VisualRadius || CachedLaunchWireTemplate->Event.AudibleRadius != Event.AudibleRadius)
	{
		CachedLaunchWireTemplate = BallisticsCosmeticWire::Prepare(Event);
	}
	Event.WireTemplate = CachedLaunchWireTemplate;
	const int32 EffectCount = Event.LaunchEffects.Num();
	Event.LaunchEffectTransforms.Reserve(EffectCount);
	for (int32 EffectIndex = 0; EffectIndex < EffectCount; ++EffectIndex)
	{
		FBallisticsLaunchEffectTransform& EffectTransform =
			Event.LaunchEffectTransforms.AddDefaulted_GetRef();
		const FBallisticsLaunchEffect& Effect = Event.LaunchEffects[EffectIndex];
		const FTransform BaseTransform(Direction.Rotation(), Origin);
		const FVector Location = BaseTransform.TransformPosition(Effect.LocationOffset);
		const FQuat Rotation =
			BaseTransform.GetRotation() * Effect.RotationOffset.Quaternion();
		const FTransform Transform(Rotation, Location);
		EffectTransform.bValid = true;
		EffectTransform.Location = Transform.GetLocation();
		EffectTransform.Rotation = Transform.Rotator();
	}
	return Event;
}

bool UProjectileLauncherComponent::ServerNotifyLaunchCosmetic_Validate(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction)
{
	return ValidateCosmeticVector(Origin) && Direction.IsNormalized();
}

void UProjectileLauncherComponent::ServerNotifyLaunchCosmetic_Implementation(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("launch")))
	{
		return;
	}

	if (bEnableCosmeticRouting && HasConfiguredLaunchCosmetics())
	{
		QueueLaunchCosmetic(Origin, Direction, GetOwningPlayerController());
	}
}

bool UProjectileLauncherComponent::ServerBeginLaunchSequence_Validate(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	float LaunchesPerSecond,
	int32 SequenceID)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float MaxRate = Settings
		? FMath::Clamp(Settings->MaxLaunchCosmeticRate, 1.f, 120.f)
		: 60.f;
	return ValidateCosmeticVector(Origin) &&
		Direction.IsNormalized() &&
		FMath::IsFinite(LaunchesPerSecond) &&
		LaunchesPerSecond > 0.f &&
		LaunchesPerSecond <= MaxRate &&
		SequenceID != INDEX_NONE;
}

void UProjectileLauncherComponent::ServerBeginLaunchSequence_Implementation(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	float LaunchesPerSecond,
	int32 SequenceID)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("sequence begin")))
	{
		return;
	}

	if (!bEnableCosmeticRouting || !HasConfiguredLaunchCosmetics())
	{
		return;
	}

	if (bLaunchSequenceActive && ActiveLaunchSequenceID != SequenceID)
	{
		FLaunchSequenceCommand EndCommand;
		EndCommand.Type = ELaunchSequenceCommandType::End;
		EndCommand.SequenceID = ActiveLaunchSequenceID;
		EndCommand.LaunchEvent = BuildLaunchCosmeticEvent(Origin, Direction);

		if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
		{
			Manager->QueueSequenceCommand(
				EndCommand,
				GetOwningPlayerController());
		}
	}

	bLaunchSequenceActive = true;
	ActiveLaunchSequenceID = SequenceID;
	ActiveLaunchesPerSecond = LaunchesPerSecond;
	LastSequenceTransformSendTime = -BIG_NUMBER;

	FLaunchSequenceCommand BeginCommand;
	BeginCommand.Type = ELaunchSequenceCommandType::Begin;
	BeginCommand.SequenceID = SequenceID;
	BeginCommand.LaunchEvent = BuildLaunchCosmeticEvent(Origin, Direction);
	BeginCommand.LaunchEvent.SequenceID = SequenceID;
	BeginCommand.LaunchesPerSecond = LaunchesPerSecond;

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueSequenceCommand(
			BeginCommand,
			GetOwningPlayerController());
	}
}

bool UProjectileLauncherComponent::ServerEndLaunchSequence_Validate(int32 SequenceID)
{
	return SequenceID != INDEX_NONE;
}

void UProjectileLauncherComponent::ServerEndLaunchSequence_Implementation(int32 SequenceID)
{
	if (!bLaunchSequenceActive || ActiveLaunchSequenceID != SequenceID)
	{
		return;
	}

	bLaunchSequenceActive = false;
	ActiveLaunchSequenceID = INDEX_NONE;
	ActiveLaunchesPerSecond = 0.f;

	FLaunchSequenceCommand Command;
	Command.Type = ELaunchSequenceCommandType::End;
	Command.SequenceID = SequenceID;
	Command.LaunchEvent = BuildLaunchCosmeticEvent(
		GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector,
		GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector);

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueSequenceCommand(
			Command,
			GetOwningPlayerController());
	}
}

bool UProjectileLauncherComponent::ServerUpdateLaunchSequence_Validate(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 SequenceID)
{
	return ValidateCosmeticVector(Origin) &&
		Direction.IsNormalized() &&
		SequenceID != INDEX_NONE;
}

void UProjectileLauncherComponent::ServerUpdateLaunchSequence_Implementation(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 SequenceID)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("sequence update")))
	{
		return;
	}

	if (bLaunchSequenceActive && ActiveLaunchSequenceID == SequenceID)
	{
		QueueSequenceTransform(Origin, Direction, GetOwningPlayerController());
	}
}

void UProjectileLauncherComponent::Launch(const FVector& Origin, const FVector& Direction)
{
	if (!TryConsumeLocalLaunchCadence())
	{
		return;
	}

	const int32 LaunchRequestID = GenerateLaunchRequestID();
	DispatchLaunchCosmetic(Origin, Direction.GetSafeNormal());

	if (ProjectileDataAsset && ProjectileDataAsset->bUseHitscan)
	{
		LaunchHitscan(Origin, Direction);
		return;
	}

	if (ProjectileDataAsset && ProjectileDataAsset->UsesSpread() && ProjectileDataAsset->SpreadConfig)
	{
		LaunchSpread(Origin, Direction, LaunchRequestID);
		return;
	}

	LaunchSingle(Origin, Direction, LaunchRequestID, 0, 1);
}

void UProjectileLauncherComponent::LaunchSingle(
	const FVector& Origin,
	const FVector& Direction,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	switch (GetActiveAuthorityMode())
	{
	case ELaunchAuthorityMode::ServerAuthoritative:
		Launch_ServerAuthoritative(
			Origin,
			Direction,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
		break;

	case ELaunchAuthorityMode::ClientPredicted:
		Launch_ClientPredicted(
			Origin,
			Direction,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
		break;

	case ELaunchAuthorityMode::ClientAuthoritative:
		Launch_ClientAuthoritative(
			Origin,
			Direction,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
		break;
	}
}

void UProjectileLauncherComponent::LaunchSpread(
	const FVector& Origin,
	const FVector& Direction,
	int32 LaunchRequestID)
{
	if (!ProjectileDataAsset || !ProjectileDataAsset->SpreadConfig)
	{
		return;
	}

	FProjectileInitialConditions BaseConditions_Projectile = ProjectileDataAsset->BuildInitialConditions();
	BaseConditions_Projectile.Origin = Origin;
	BaseConditions_Projectile.Direction = Direction.GetSafeNormal();
	BaseConditions_Projectile.RandomSeed = GenerateSeed();

	FRandomStream SpreadStream(BaseConditions_Projectile.RandomSeed);
	TArray<FProjectileInitialConditions> ProjectileConditions;
	ProjectileDataAsset->SpreadConfig->GenerateProjectileConditions(
		BaseConditions_Projectile, Direction.GetSafeNormal(), SpreadStream, ProjectileConditions
	);
	if (ProjectileConditions.Num() > 64)
	{
		ProjectileConditions.SetNum(64, EAllowShrinking::No);
	}

	// Launch each generated projectile through the normal authority flow without rebuilding it.
	for (int32 Index = 0; Index < ProjectileConditions.Num(); ++Index)
	{
		const FProjectileInitialConditions& Projectile = ProjectileConditions[Index];
		ProjectileConditionsOverride = Projectile;
		bUseProjectileConditions = true;
		LaunchSingle(
			Projectile.Origin,
			Projectile.Direction,
			LaunchRequestID,
			Index,
			ProjectileConditions.Num());
		bUseProjectileConditions = false;
	}
}

void UProjectileLauncherComponent::LaunchHitscan(const FVector& Origin, const FVector& Direction)
{
	FHitscanConfig Config = ProjectileDataAsset ? ProjectileDataAsset->HitscanConfig : FHitscanConfig();
	Config.RandomSeed = GenerateSeed();

	if (ProjectileDataAsset)
	{
		Config.SimulatedMass = ProjectileDataAsset->Mass;
		Config.SimulatedInitialSpeed = ProjectileDataAsset->InitialSpeed;

		if (ProjectileDataAsset->MaterialOverride)
		{
			Config.MaterialDataAsset = ProjectileDataAsset->MaterialOverride;
		}
	}

	if (AActor* Owner = GetOwner())
	{
		Config.IgnoredActors.AddUnique(Owner);

		// Also ignore the instigator (the pawn controlling the launcher)
		if (APawn* Instigator = Owner->GetInstigator())
		{
			Config.IgnoredActors.AddUnique(Instigator);
		}
	}

	// If this is a spread hitscan, launch multiple traces
	if (ProjectileDataAsset && ProjectileDataAsset->UsesSpread() && ProjectileDataAsset->SpreadConfig)
	{
		FRandomStream SpreadStream(Config.RandomSeed);
		TArray<FVector> ProjectileDirections;
		ProjectileDataAsset->SpreadConfig->GenerateProjectileDirections(
			Direction.GetSafeNormal(), SpreadStream, ProjectileDirections
		);

		for (int32 i = 0; i < ProjectileDirections.Num(); ++i)
		{
			FHitscanConfig ProjectileConfig = Config;
			ProjectileConfig.RandomSeed = Config.RandomSeed + i + 1;

			FHitscanResult Result = UBallisticsHitscanLibrary::PerformHitscan(
				this, Origin, ProjectileDirections[i], ProjectileConfig
			);
			for (const FHitscanHit& CosmeticHit : Result.Hits)
			{
				HandleProjectileImpactCosmetic(
					CosmeticHit.HitResult,
					CosmeticHit.ImpactResponse,
					ProjectileConfig.RandomSeed,
					ProjectileDataAsset);
			}
			OnHitscanComplete.Broadcast(Result);
		}
	}
	else
	{
		FHitscanResult Result = UBallisticsHitscanLibrary::PerformHitscan(
			this, Origin, Direction.GetSafeNormal(), Config
		);
		for (const FHitscanHit& CosmeticHit : Result.Hits)
		{
			HandleProjectileImpactCosmetic(
				CosmeticHit.HitResult,
				CosmeticHit.ImpactResponse,
				Config.RandomSeed,
				ProjectileDataAsset);
		}
		OnHitscanComplete.Broadcast(Result);
	}

	LaunchCounter++;
}

FProjectileInitialConditions UProjectileLauncherComponent::BuildConditions(const FVector& Origin, const FVector& Direction)
{
	FProjectileInitialConditions Conditions = bUseProjectileConditions
		? ProjectileConditionsOverride
		: (ProjectileDataAsset ? ProjectileDataAsset->BuildInitialConditions() : FProjectileInitialConditions());

	Conditions.Origin = Origin;
	Conditions.Direction = Direction.GetSafeNormal();
	Conditions.RandomSeed = GenerateSeed();

	return Conditions;
}

FProjectileInitialConditions UProjectileLauncherComponent::BuildCanonicalServerConditions(
	const FVector& Origin,
	const FVector& Direction,
	int32 RandomSeed) const
{
	FProjectileInitialConditions Conditions = ProjectileDataAsset
		? ProjectileDataAsset->BuildInitialConditions()
		: FProjectileInitialConditions();
	Conditions.Origin = Origin;
	Conditions.Direction = Direction.GetSafeNormal();
	Conditions.RandomSeed = RandomSeed;

	const UBallisticsSpreadDataAsset* Spread = ProjectileDataAsset
		? ProjectileDataAsset->SpreadConfig
		: nullptr;
	if (Spread)
	{
		FRandomStream VariationStream(RandomSeed);
		if (Spread->bVaryInitialSpeed &&
			Spread->InitialSpeedVariationPercent > KINDA_SMALL_NUMBER)
		{
			Conditions.InitialSpeed *= 1.f + VariationStream.FRandRange(
				-Spread->InitialSpeedVariationPercent,
				Spread->InitialSpeedVariationPercent);
		}
		if (Spread->bVaryProjectileMass &&
			Spread->MassVariationPercent > KINDA_SMALL_NUMBER)
		{
			Conditions.Mass *= 1.f + VariationStream.FRandRange(
				-Spread->MassVariationPercent,
				Spread->MassVariationPercent);
		}
	}

	return Conditions;
}

bool UProjectileLauncherComponent::TryAcceptRemoteLaunchRequest()
{
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || Controller->IsLocalController())
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();
	if (!FMath::IsFinite(RemoteLaunchRequestWindowStart) ||
		Now < RemoteLaunchRequestWindowStart ||
		Now - RemoteLaunchRequestWindowStart >= 1.f)
	{
		RemoteLaunchRequestWindowStart = Now;
		RemoteLaunchRequestsThisWindow = 0;
	}

	const int32 Limit = FMath::Max(
		1,
		UBallisticsCoreSettings::Get()->MaxClientLaunchRequestsPerSecond);
	if (RemoteLaunchRequestsThisWindow >= Limit)
	{
		if (Now - LastRemoteLaunchRequestWarningTime >= 1.f)
		{
			LastRemoteLaunchRequestWarningTime = Now;
			UE_LOG(
				LogNPS,
				Warning,
				TEXT("NPS rejected remote launch requests from %s at the hard limit (%d/s)."),
				*GetNameSafe(GetOwner()),
				Limit);
		}
		return false;
	}

	++RemoteLaunchRequestsThisWindow;
	return true;
}

double UProjectileLauncherComponent::GetLaunchAdmissionTime() const
{
	return GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
}

bool UProjectileLauncherComponent::IsRemoteClientLaunch() const
{
	const APlayerController* Controller = GetOwningPlayerController();
	return Controller && !Controller->IsLocalController();
}

void UProjectileLauncherComponent::RefreshLaunchAdmissionOwner()
{
	AActor* DirectOwner = GetOwner() ? GetOwner()->GetOwner() : nullptr;
	APlayerController* Controller = GetOwningPlayerController();
	if (bLaunchAdmissionOwnerInitialized &&
		(CachedLaunchAdmissionOwner.Get() != DirectOwner ||
		 CachedLaunchAdmissionController.Get() != Controller))
	{
		LaunchCreditBatches.Reset();
		RemoteLaunchAdmissions.Reset();
		RecentRemoteProjectileIDs.Reset();
		ServerCadenceLastRefillTime = -1.0;
		ServerCadenceTokens = 3.f;
		ClientCadenceLastRefillTime = -1.0;
		ClientCadenceTokens = 3.f;
	}
	CachedLaunchAdmissionOwner = DirectOwner;
	CachedLaunchAdmissionController = Controller;
	bLaunchAdmissionOwnerInitialized = true;
}

void UProjectileLauncherComponent::PruneLaunchCredits(double Now)
{
	LaunchCreditBatches.RemoveAll(
		[Now](const FBallisticsLaunchCreditBatch& Batch)
		{
			return Batch.Remaining <= 0 || Batch.ExpiryTime <= Now;
		});
}

bool UProjectileLauncherComponent::HasLaunchCredit(double Now) const
{
	return LaunchCreditBatches.ContainsByPredicate(
		[Now](const FBallisticsLaunchCreditBatch& Batch)
		{
			return Batch.Remaining > 0 && Batch.ExpiryTime > Now;
		});
}

bool UProjectileLauncherComponent::ConsumeLaunchCredit(double Now)
{
	PruneLaunchCredits(Now);
	int32 BestIndex = INDEX_NONE;
	double EarliestExpiry = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < LaunchCreditBatches.Num(); ++Index)
	{
		const FBallisticsLaunchCreditBatch& Batch = LaunchCreditBatches[Index];
		if (Batch.Remaining > 0 && Batch.ExpiryTime < EarliestExpiry)
		{
			BestIndex = Index;
			EarliestExpiry = Batch.ExpiryTime;
		}
	}
	if (BestIndex == INDEX_NONE)
	{
		return false;
	}
	--LaunchCreditBatches[BestIndex].Remaining;
	PruneLaunchCredits(Now);
	return true;
}

void UProjectileLauncherComponent::RefillCadenceTokens(
	double Now,
	double& LastRefillTime,
	float& Tokens) const
{
	constexpr float BucketCapacity = 3.f;
	if (MinLaunchInterval <= 0.f)
	{
		Tokens = BucketCapacity;
		LastRefillTime = Now;
		return;
	}
	if (LastRefillTime < 0.0 || Now < LastRefillTime)
	{
		Tokens = BucketCapacity;
		LastRefillTime = Now;
		return;
	}
	const double Elapsed = Now - LastRefillTime;
	Tokens = FMath::Min(
		BucketCapacity,
		Tokens + static_cast<float>(Elapsed / MinLaunchInterval));
	LastRefillTime = Now;
}

void UProjectileLauncherComponent::PruneRemoteLaunchAdmissions(double Now)
{
	constexpr double RetentionSeconds = 5.0;
	for (auto It = RemoteLaunchAdmissions.CreateIterator(); It; ++It)
	{
		if (Now < It.Value().CommitTime ||
			Now - It.Value().CommitTime > RetentionSeconds)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = RecentRemoteProjectileIDs.CreateIterator(); It; ++It)
	{
		if (Now < It.Value() || Now - It.Value() > RetentionSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

int32 UProjectileLauncherComponent::GetExpectedLaunchMemberCount() const
{
	if (!ProjectileDataAsset || !ProjectileDataAsset->UsesSpread() ||
		!ProjectileDataAsset->SpreadConfig)
	{
		return 1;
	}
	const UBallisticsSpreadDataAsset* Spread = ProjectileDataAsset->SpreadConfig;
	return Spread->SpreadMode == ESpreadMode::RandomCone
		? FMath::Clamp(Spread->ProjectileCount, 1, 64)
		: FMath::Clamp(Spread->Pattern.Num(), 0, 64);
}

bool UProjectileLauncherComponent::CanAcceptRemoteLaunchAdmission(
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	RefreshLaunchAdmissionOwner();
	if (!IsRemoteClientLaunch())
	{
		return true;
	}
	const double Now = GetLaunchAdmissionTime();
	PruneLaunchCredits(Now);
	PruneRemoteLaunchAdmissions(Now);
	if (ProjectileID < 0 || LaunchRequestID < 0 ||
		LaunchMemberCount != GetExpectedLaunchMemberCount() ||
		LaunchMemberIndex < 0 || LaunchMemberIndex >= LaunchMemberCount ||
		RecentRemoteProjectileIDs.Contains(ProjectileID))
	{
		return false;
	}

	if (const FBallisticsRemoteLaunchAdmission* Existing =
		RemoteLaunchAdmissions.Find(LaunchRequestID))
	{
		return Existing->MemberCount == LaunchMemberCount &&
			Existing->AcceptedMembers.IsValidIndex(LaunchMemberIndex) &&
			!Existing->AcceptedMembers[LaunchMemberIndex];
	}

	if (LaunchMemberIndex != 0)
	{
		return false;
	}
	RefillCadenceTokens(Now, ServerCadenceLastRefillTime, ServerCadenceTokens);
	if (MinLaunchInterval > 0.f && ServerCadenceTokens < 1.f)
	{
		return false;
	}
	return !bRequireLaunchCredits || HasLaunchCredit(Now);
}

bool UProjectileLauncherComponent::CommitRemoteLaunchAdmission(
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	RefreshLaunchAdmissionOwner();
	if (!IsRemoteClientLaunch())
	{
		return true;
	}
	const double Now = GetLaunchAdmissionTime();
	PruneLaunchCredits(Now);
	PruneRemoteLaunchAdmissions(Now);
	if (RecentRemoteProjectileIDs.Contains(ProjectileID))
	{
		return false;
	}

	FBallisticsRemoteLaunchAdmission* Admission =
		RemoteLaunchAdmissions.Find(LaunchRequestID);
	if (!Admission)
	{
		if (LaunchMemberIndex != 0 ||
			LaunchMemberCount != GetExpectedLaunchMemberCount())
		{
			return false;
		}
		RefillCadenceTokens(Now, ServerCadenceLastRefillTime, ServerCadenceTokens);
		if (MinLaunchInterval > 0.f && ServerCadenceTokens < 1.f)
		{
			return false;
		}
		if (bRequireLaunchCredits && !ConsumeLaunchCredit(Now))
		{
			return false;
		}
		if (MinLaunchInterval > 0.f)
		{
			ServerCadenceTokens -= 1.f;
		}

		FBallisticsRemoteLaunchAdmission& NewAdmission =
			RemoteLaunchAdmissions.Add(LaunchRequestID);
		NewAdmission.MemberCount = LaunchMemberCount;
		NewAdmission.AcceptedMembers.Init(false, LaunchMemberCount);
		NewAdmission.CommitTime = Now;
		Admission = &NewAdmission;
	}

	if (Admission->MemberCount != LaunchMemberCount ||
		!Admission->AcceptedMembers.IsValidIndex(LaunchMemberIndex) ||
		Admission->AcceptedMembers[LaunchMemberIndex])
	{
		return false;
	}
	Admission->AcceptedMembers[LaunchMemberIndex] = true;
	Admission->CommitTime = Now;
	RecentRemoteProjectileIDs.Add(ProjectileID, Now);
	return true;
}

bool UProjectileLauncherComponent::TryConsumeLocalLaunchCadence()
{
	RefreshLaunchAdmissionOwner();
	if (!GetOwner() || GetOwner()->HasAuthority() ||
		!IsLocallyControlledLauncher() || MinLaunchInterval <= 0.f)
	{
		return true;
	}
	const double Now = GetLaunchAdmissionTime();
	RefillCadenceTokens(Now, ClientCadenceLastRefillTime, ClientCadenceTokens);
	if (ClientCadenceTokens < 1.f)
	{
		return false;
	}
	ClientCadenceTokens -= 1.f;
	return true;
}

int32 UProjectileLauncherComponent::GenerateLaunchRequestID()
{
	const int32 Result = NextLaunchRequestID;
	NextLaunchRequestID = NextLaunchRequestID == MAX_int32
		? 0
		: NextLaunchRequestID + 1;
	return Result;
}

bool UProjectileLauncherComponent::ValidateRemoteLaunchRequest(
	ELaunchAuthorityMode AuthorityMode,
	const FProjectileInitialConditions& CanonicalConditions,
	int32 ProjectileID,
	float ClientTimestamp)
{
	if (CanonicalConditions.Origin.ContainsNaN() ||
		CanonicalConditions.Direction.ContainsNaN() ||
		!CanonicalConditions.Direction.IsNormalized() ||
		!FMath::IsFinite(CanonicalConditions.InitialSpeed) ||
		!FMath::IsFinite(CanonicalConditions.Mass) ||
		!FMath::IsFinite(CanonicalConditions.Diameter) ||
		!FMath::IsFinite(CanonicalConditions.DragCoefficient) ||
		CanonicalConditions.WindAcceleration.ContainsNaN() ||
		CanonicalConditions.InitialSpeed < 0.f ||
		CanonicalConditions.Mass <= 0.f ||
		CanonicalConditions.Diameter <= 0.f ||
		CanonicalConditions.DragCoefficient < 0.f)
	{
		return false;
	}

	FBallisticsLaunchValidationContext Context;
	Context.AuthorityMode = AuthorityMode;
	Context.Origin = CanonicalConditions.Origin;
	Context.Direction = CanonicalConditions.Direction;
	Context.ProjectileID = ProjectileID;
	Context.ClientTimestamp = ClientTimestamp;
	Context.ProjectileDataAsset = ProjectileDataAsset;

	TSet<const AActor*> CheckedActors;
	for (AActor* Current = GetOwner(); Current; Current = Current->GetOwner())
	{
		if (CheckedActors.Contains(Current))
		{
			break;
		}
		CheckedActors.Add(Current);
		if (Current->GetClass()->ImplementsInterface(
			UBallisticsLaunchValidator::StaticClass()) &&
			!IBallisticsLaunchValidator::Execute_ValidateBallisticsLaunch(
				Current,
				Context))
		{
			return false;
		}
	}

	if (APawn* LaunchingPawn = ResolveLaunchingPawn();
		LaunchingPawn && !CheckedActors.Contains(LaunchingPawn) &&
		LaunchingPawn->GetClass()->ImplementsInterface(
			UBallisticsLaunchValidator::StaticClass()) &&
		!IBallisticsLaunchValidator::Execute_ValidateBallisticsLaunch(
			LaunchingPawn,
			Context))
	{
		return false;
	}

	return true;
}

void UProjectileLauncherComponent::Launch_ServerAuthoritative(
	const FVector& Origin,
	const FVector& Direction,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	FProjectileInitialConditions Conditions = BuildConditions(Origin, Direction);
	const int32 ProjectileID = NextProjectileID++;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		AActor* Projectile = SpawnProjectile(Conditions);
		if (Projectile && Projectile->GetIsReplicated())
		{
			return;
		}

		// Non-replicating projectiles need a cosmetic instance on each client.
		MulticastOnProjectileLaunched(Conditions, false);
	}
	else
	{
		const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

		if (Settings->ServerAuthCosmeticMode == EServerAuthCosmeticMode::ImmediateLocalCosmetic)
		{
			AActor* CosmeticProjectile = SpawnProjectile(Conditions);
			if (CosmeticProjectile)
			{
				if (UProjectileBallisticsComponent* Comp = CosmeticProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
				{
					Comp->SetIsLocallyPredicted(true);
				}
			}
		}
		// Fast-forwarded cosmetics are handled by the multicast.

		// Submit the authoritative launch request.
		ServerRequestLaunch(
			Conditions.Origin,
			Conditions.Direction,
			Conditions.RandomSeed,
			ProjectileID,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
	}
}

bool UProjectileLauncherComponent::ServerRequestLaunch_Validate(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	return !Origin.ContainsNaN() && !Direction.ContainsNaN() &&
		Direction.IsNormalized() && ProjectileID >= 0 && LaunchRequestID >= 0 &&
		LaunchMemberCount > 0 && LaunchMemberCount <= 64 &&
		LaunchMemberIndex >= 0 && LaunchMemberIndex < LaunchMemberCount;
}

void UProjectileLauncherComponent::ServerRequestLaunch_Implementation(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	if (!TryAcceptRemoteLaunchRequest() ||
		!CanAcceptRemoteLaunchAdmission(
			ProjectileID,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount))
	{
		return;
	}

	const FProjectileInitialConditions Conditions = BuildCanonicalServerConditions(
		Origin,
		Direction,
		RandomSeed);
	if (!ValidateRemoteLaunchRequest(
		ELaunchAuthorityMode::ServerAuthoritative,
		Conditions,
		ProjectileID,
		GetSynchronizedTime()))
	{
		return;
	}

	if (!CommitRemoteLaunchAdmission(
		ProjectileID,
		LaunchRequestID,
		LaunchMemberIndex,
		LaunchMemberCount))
	{
		return;
	}
	if (!SpawnProjectile(Conditions))
	{
		return;
	}

	// Client-initiated launches may already have a local cosmetic.
	MulticastOnProjectileLaunched(Conditions, true);
}

void UProjectileLauncherComponent::MulticastOnProjectileLaunched_Implementation(
	FProjectileInitialConditions Conditions,
	bool bClientInitiated)
{
	// Skip client cosmetics on the server.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

	// Skip duplicate cosmetics on the locally controlled launching owner.
	const bool bIsLocalLaunchingClient = GetOwner() && GetOwner()->GetInstigator() && GetOwner()->GetInstigator()->IsLocallyControlled();

	if (bClientInitiated && bIsLocalLaunchingClient &&
		Settings->ServerAuthCosmeticMode == EServerAuthCosmeticMode::ImmediateLocalCosmetic)
	{
		return;
	}

	AActor* CosmeticProjectile = SpawnProjectile(Conditions);
	if (CosmeticProjectile)
	{
		if (UProjectileBallisticsComponent* Comp = CosmeticProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetIsLocallyPredicted(true);

			// If fast-forwarded mode, advance the cosmetic to approximate server position
			if (Settings->ServerAuthCosmeticMode == EServerAuthCosmeticMode::FastForwardedCosmetic)
			{
				// Estimate half-RTT - use player controller's ping
				float HalfRTT = 0.f;
				if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()->GetInstigator()))
				{
					if (const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
					{
						if (const APlayerState* PS = PC->PlayerState)
						{
							// GetPingInMilliseconds returns ms; convert to seconds and take half for one-way.
							HalfRTT = PS->GetPingInMilliseconds() * 0.001f * 0.5f;
						}
					}
				}

				if (HalfRTT > KINDA_SMALL_NUMBER)
				{
					Comp->FastForward(HalfRTT);
				}
			}
		}
	}
}

void UProjectileLauncherComponent::Launch_ClientPredicted(
	const FVector& Origin,
	const FVector& Direction,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	FProjectileInitialConditions Conditions = BuildConditions(Origin, Direction);

	const int32 ProjectileID = NextProjectileID++;

	AActor* PredictedProjectile = SpawnProjectile(Conditions);
	if (PredictedProjectile)
	{
		if (UProjectileBallisticsComponent* Comp = PredictedProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetIsLocallyPredicted(true);
			Comp->SetProjectileID(ProjectileID);
		}
		PredictedProjectiles.Add(ProjectileID, PredictedProjectile);
	}

	// Get client timestamp for lag compensation. Use the GameState's server clock so
	// client and server timestamps are in the same frame of reference (World->GetTimeSeconds
	// is local-world-since-load, which diverges between client and server).
	float ClientTimestamp = 0.f;
	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GS = World->GetGameState())
		{
			ClientTimestamp = GS->GetServerWorldTimeSeconds();
		}
		else
		{
			ClientTimestamp = World->GetTimeSeconds();
		}
	}

	// Tell the server
	ServerNotifyLaunch(
		Conditions.Origin,
		Conditions.Direction,
		Conditions.RandomSeed,
		ClientTimestamp,
		ProjectileID,
		LaunchRequestID,
		LaunchMemberIndex,
		LaunchMemberCount);
}

bool UProjectileLauncherComponent::ServerNotifyLaunch_Validate(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	float ClientTimestamp,
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	return !Origin.ContainsNaN() && !Direction.ContainsNaN() &&
		Direction.IsNormalized() &&
		FMath::IsFinite(ClientTimestamp) && ProjectileID >= 0 &&
		LaunchRequestID >= 0 && LaunchMemberCount > 0 &&
		LaunchMemberCount <= 64 && LaunchMemberIndex >= 0 &&
		LaunchMemberIndex < LaunchMemberCount;
}

void UProjectileLauncherComponent::ServerNotifyLaunch_Implementation(
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	float ClientTimestamp,
	int32 ProjectileID,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	if (!TryAcceptRemoteLaunchRequest() ||
		!CanAcceptRemoteLaunchAdmission(
			ProjectileID,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount))
	{
		return;
	}

	const FProjectileInitialConditions Conditions = BuildCanonicalServerConditions(
		Origin,
		Direction,
		RandomSeed);
	if (!ValidateRemoteLaunchRequest(
		ELaunchAuthorityMode::ClientPredicted,
		Conditions,
		ProjectileID,
		ClientTimestamp))
	{
		return;
	}

	if (!CommitRemoteLaunchAdmission(
		ProjectileID,
		LaunchRequestID,
		LaunchMemberIndex,
		LaunchMemberCount))
	{
		return;
	}
	AActor* AuthProjectile = SpawnProjectile(Conditions);
	if (!AuthProjectile)
	{
		return;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

	// Fast-forward to compensate for client latency
	const float ServerTime = GetWorld()->GetTimeSeconds();
	const float Lag = FMath::Clamp(ServerTime - ClientTimestamp, 0.f, Settings->MaxLagCompensationWindow);

	if (Lag > KINDA_SMALL_NUMBER)
	{
		if (UProjectileBallisticsComponent* Comp = AuthProjectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetProjectileID(ProjectileID);
			Comp->FastForward(Lag);

			// Reconcile predictions outside the configured tolerance.
			const FProjectileSnapshot ServerSnap = Comp->GetCurrentSnapshot();
			const FProjectileSnapshot ClientExpected = Comp->EvaluateAtTime(Lag);

			const float Divergence = FVector::Dist(ServerSnap.Position, ClientExpected.Position);

			if (Divergence > Settings->PredictionReconciliationTolerance)
			{
				ClientReconcileProjectile(
					ProjectileID,
					ServerSnap.Position,
					ServerSnap.Velocity,
					Comp->GetElapsedTime()
				);
			}
		}
	}
}

// Client Reconciliation

void UProjectileLauncherComponent::ClientReconcileProjectile_Implementation(
	int32 ProjectileID,
	FVector CorrectedPosition,
	FVector CorrectedVelocity,
	float ServerElapsedTime)
{
	TWeakObjectPtr<AActor>* Found = PredictedProjectiles.Find(ProjectileID);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	AActor* PredictedActor = Found->Get();
	UProjectileBallisticsComponent* Comp = PredictedActor->FindComponentByClass<UProjectileBallisticsComponent>();
	if (!Comp)
	{
		return;
	}

	Comp->ForceCorrection(CorrectedPosition, CorrectedVelocity, ServerElapsedTime);
}


void UProjectileLauncherComponent::Launch_ClientAuthoritative(
	const FVector& Origin,
	const FVector& Direction,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	FProjectileInitialConditions Conditions = BuildConditions(Origin, Direction);

	const int32 ProjectileID = NextProjectileID++;
	const float ClientTimestamp = GetSynchronizedTime();
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerRegisterClientAuthoritativeLaunch(
			ProjectileID,
			Conditions.Origin,
			Conditions.Direction,
			Conditions.RandomSeed,
			ClientTimestamp,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
	}
	else
	{
		ServerRegisterClientAuthoritativeLaunch_Implementation(
			ProjectileID,
			Conditions.Origin,
			Conditions.Direction,
			Conditions.RandomSeed,
			ClientTimestamp,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount);
	}

	AActor* Projectile = SpawnProjectile(Conditions);
	if (Projectile)
	{
		if (UProjectileBallisticsComponent* Comp = Projectile->FindComponentByClass<UProjectileBallisticsComponent>())
		{
			Comp->SetIsLocallyPredicted(!(GetOwner() && GetOwner()->HasAuthority()));
			Comp->SetProjectileID(ProjectileID);
		}
		PredictedProjectiles.Add(ProjectileID, Projectile);
	}

	// Submit the accumulated flight log on terminal impact.
}

void UProjectileLauncherComponent::SubmitFlightLog(const FProjectileFlightLog& FlightLog)
{
	ServerSubmitFlightLog(FlightLog);
}

#if WITH_DEV_AUTOMATION_TESTS
void UProjectileLauncherComponent::SubmitFlightLogForTests(
	const FProjectileFlightLog& FlightLog)
{
	ServerSubmitFlightLog_Implementation(FlightLog);
}

void UProjectileLauncherComponent::RegisterClientAuthoritativeLaunchForTests(
	int32 ProjectileID,
	const FProjectileInitialConditions& Conditions,
	float ClientTimestamp)
{
	ServerRegisterClientAuthoritativeLaunch_Implementation(
		ProjectileID,
		Conditions.Origin,
		Conditions.Direction,
		Conditions.RandomSeed,
		ClientTimestamp,
		ProjectileID,
		0,
		1);
}
#endif

bool UProjectileLauncherComponent::ServerRegisterClientAuthoritativeLaunch_Validate(
	int32 ProjectileID,
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	float ClientTimestamp,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	return ProjectileID >= 0 && !Origin.ContainsNaN() &&
		!Direction.ContainsNaN() &&
		Direction.IsNormalized() && FMath::IsFinite(ClientTimestamp) &&
		LaunchRequestID >= 0 && LaunchMemberCount > 0 &&
		LaunchMemberCount <= 64 && LaunchMemberIndex >= 0 &&
		LaunchMemberIndex < LaunchMemberCount;
}

void UProjectileLauncherComponent::ServerRegisterClientAuthoritativeLaunch_Implementation(
	int32 ProjectileID,
	FVector_NetQuantize10 Origin,
	FVector_NetQuantizeNormal Direction,
	int32 RandomSeed,
	float ClientTimestamp,
	int32 LaunchRequestID,
	int32 LaunchMemberIndex,
	int32 LaunchMemberCount)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (ClientAuthoritativeLaunchSnapshots.Contains(ProjectileID) ||
		!TryAcceptRemoteLaunchRequest() ||
		!CanAcceptRemoteLaunchAdmission(
			ProjectileID,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount))
	{
		return;
	}

	const FProjectileInitialConditions CanonicalConditions =
		BuildCanonicalServerConditions(Origin, Direction, RandomSeed);
	if (!ValidateRemoteLaunchRequest(
		ELaunchAuthorityMode::ClientAuthoritative,
		CanonicalConditions,
		ProjectileID,
		ClientTimestamp))
	{
		return;
	}

	const float ServerTime = World->GetTimeSeconds();
	PruneClientAuthoritativeLaunchSnapshots(ServerTime);
	if (!ClientAuthoritativeLaunchSnapshots.Contains(ProjectileID))
	{
		if (!CommitRemoteLaunchAdmission(
			ProjectileID,
			LaunchRequestID,
			LaunchMemberIndex,
			LaunchMemberCount))
		{
			return;
		}
		FBallisticsClientAuthoritativeLaunchSnapshot Snapshot;
		Snapshot.ProjectileDataAsset = ProjectileDataAsset;
		Snapshot.CanonicalConditions = CanonicalConditions;
		Snapshot.ServerReceiveTime = ServerTime;
		ClientAuthoritativeLaunchSnapshots.Add(ProjectileID, Snapshot);
		PruneClientAuthoritativeLaunchSnapshots(ServerTime);
	}
}

void UProjectileLauncherComponent::PruneClientAuthoritativeLaunchSnapshots(float ServerTime)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const float MaxAge = FMath::Max(0.f, Settings->MaxFlightLogAge);
	for (auto It = ClientAuthoritativeLaunchSnapshots.CreateIterator(); It; ++It)
	{
		if (ServerTime - It.Value().ServerReceiveTime > MaxAge)
		{
			It.RemoveCurrent();
		}
	}

	const int32 MaxSnapshots = FMath::Max(1, Settings->MaxSavedFlightLogs);
	while (ClientAuthoritativeLaunchSnapshots.Num() > MaxSnapshots)
	{
		int32 OldestID = INDEX_NONE;
		float OldestTime = TNumericLimits<float>::Max();
		for (const TPair<int32, FBallisticsClientAuthoritativeLaunchSnapshot>& Pair : ClientAuthoritativeLaunchSnapshots)
		{
			if (Pair.Value.ServerReceiveTime < OldestTime)
			{
				OldestTime = Pair.Value.ServerReceiveTime;
				OldestID = Pair.Key;
			}
		}
		if (OldestID == INDEX_NONE)
		{
			break;
		}
		ClientAuthoritativeLaunchSnapshots.Remove(OldestID);
	}
}

FBallisticsClientAuthoritativeLaunchSnapshot*
UProjectileLauncherComponent::FindClientAuthoritativeLaunchSnapshot(
	int32 ProjectileID)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	PruneClientAuthoritativeLaunchSnapshots(World->GetTimeSeconds());
	return ClientAuthoritativeLaunchSnapshots.Find(ProjectileID);
}


bool UProjectileLauncherComponent::ServerSubmitFlightLog_Validate(FProjectileFlightLog FlightLog)
{
	// Basic sanity: must have at least one segment
	if (FlightLog.Segments.Num() == 0 || FlightLog.ReportSequence < 0)
	{
		return false;
	}

	// Bound validation work before parsing client-provided segments.
	if (FlightLog.Segments.Num() > 64)
	{
		return false;
	}

	return true;
}

void UProjectileLauncherComponent::ServerSubmitFlightLog_Implementation(FProjectileFlightLog FlightLog)
{
	auto ReportResult = [this, &FlightLog](
		EProjectileFlightValidationResult Result,
		int32 FailedSegmentIndex)
	{
		NPS_LOG(Log,
			TEXT("[NPS] FlightLog #%d report=%d final=%d segments=%d result=%d failedSeg=%d"),
			FlightLog.LaunchID,
			FlightLog.ReportSequence,
			FlightLog.bFinalReport ? 1 : 0,
			FlightLog.Segments.Num(),
			static_cast<int32>(Result),
			FailedSegmentIndex);
		ClientFlightLogResult(FlightLog.LaunchID, Result, FailedSegmentIndex);
		OnFlightLogValidated.Broadcast(FlightLog, Result, FailedSegmentIndex);
	};

	FBallisticsClientAuthoritativeLaunchSnapshot* StoredSnapshot =
		FindClientAuthoritativeLaunchSnapshot(FlightLog.LaunchID);
	if (!StoredSnapshot)
	{
		ReportResult(EProjectileFlightValidationResult::UnknownLaunch, -1);
		return;
	}

	AddFlightLog(FlightLog);
	const bool bCanonicalConditionsMatch = MatchesCanonicalConditions(
		FlightLog.InitialConditions,
		StoredSnapshot->CanonicalConditions);

	if (FlightLog.ReportSequence < StoredSnapshot->NextExpectedReportSequence)
	{
		const bool bValidReplay = bCanonicalConditionsMatch &&
			FlightLog.Segments.Num() <= StoredSnapshot->AcceptedSegments.Num() &&
			MatchesFlightPrefix(
				FlightLog,
				StoredSnapshot->AcceptedSegments,
				FlightLog.Segments.Num());
		if (!bValidReplay)
		{
			ClientAuthoritativeLaunchSnapshots.Remove(FlightLog.LaunchID);
		}
		ReportResult(
			bValidReplay
				? EProjectileFlightValidationResult::Valid
				: EProjectileFlightValidationResult::InvalidSegment,
			bValidReplay ? -1 : 0);
		return;
	}

	const int32 FirstNewSegmentIndex = StoredSnapshot->AcceptedSegments.Num();
	const bool bExpectedSequence =
		FlightLog.ReportSequence == StoredSnapshot->NextExpectedReportSequence;
	const bool bPreservesPrefix = PreservesAcceptedFlightPrefix(
		FlightLog,
		StoredSnapshot->AcceptedSegments);
	const bool bHasNewSegments = FlightLog.Segments.Num() > FirstNewSegmentIndex;
	if (!bCanonicalConditionsMatch || !bExpectedSequence || !bPreservesPrefix ||
		(!bHasNewSegments && !FlightLog.bFinalReport))
	{
		ClientAuthoritativeLaunchSnapshots.Remove(FlightLog.LaunchID);
		ReportResult(EProjectileFlightValidationResult::InvalidSegment,
			FMath::Max(0, FirstNewSegmentIndex));
		return;
	}

	if (!bHasNewSegments)
	{
		ClientAuthoritativeLaunchSnapshots.Remove(FlightLog.LaunchID);
		ReportResult(EProjectileFlightValidationResult::Valid, -1);
		return;
	}

	int32 FailedSegmentIndex = -1;
	const EProjectileFlightValidationResult Result = ValidateFlightLog(
		FlightLog,
		*StoredSnapshot,
		FirstNewSegmentIndex,
		FailedSegmentIndex);
	if (Result != EProjectileFlightValidationResult::Valid)
	{
		ClientAuthoritativeLaunchSnapshots.Remove(FlightLog.LaunchID);
		ReportResult(Result, FailedSegmentIndex);
		return;
	}

	const FBallisticsClientAuthoritativeLaunchSnapshot LaunchSnapshot = *StoredSnapshot;
	StoredSnapshot->AcceptedSegments = FlightLog.Segments;
	++StoredSnapshot->NextExpectedReportSequence;
	if (const UWorld* World = GetWorld())
	{
		StoredSnapshot->ServerReceiveTime = World->GetTimeSeconds();
	}
	if (FlightLog.bFinalReport)
	{
		ClientAuthoritativeLaunchSnapshots.Remove(FlightLog.LaunchID);
	}

	ReportResult(Result, FailedSegmentIndex);

	// If valid, dispatch damage to hit actors and spawn shatter fragments at the server.
	if (Result == EProjectileFlightValidationResult::Valid)
	{
		UBallisticsProjectileDataAsset* LaunchProjectileData =
			LaunchSnapshot.ProjectileDataAsset;
		const FProjectileInitialConditions& CanonicalConditions =
			LaunchSnapshot.CanonicalConditions;
		AActor* ResolvedInstigator = ResolveLaunchingPawn();

		for (int32 SegIdx = FirstNewSegmentIndex; SegIdx < FlightLog.Segments.Num(); ++SegIdx)
		{
			const FProjectileFlightSegment& Segment = FlightLog.Segments[SegIdx];
			FBallisticsAuthoritativeImpactEvent ImpactEvent;
			ImpactEvent.HitResult.ImpactPoint = Segment.EndLocation;
			ImpactEvent.HitResult.ImpactNormal = Segment.ImpactNormal;
			ImpactEvent.HitResult.BoneName = Segment.HitBoneName;
			ImpactEvent.ImpactResponse = Segment.ImpactResponse;
			ImpactEvent.ProjectileID = FlightLog.LaunchID;
			ImpactEvent.ProjectileDataAsset = LaunchProjectileData;
			ImpactEvent.Instigator = ResolvedInstigator;
			ImpactEvent.HitActor = Segment.HitActor.Get();
			ImpactEvent.bValidatedClientReport = true;
			OnAuthoritativeImpact.Broadcast(ImpactEvent);

			if (!Segment.HitActor.IsValid())
			{
				NPS_LOG(Verbose,
					TEXT("[NPS] FlightLog #%d seg %d - no HitActor (response=%d, surface=%d). Static-world hit."),
					FlightLog.LaunchID, SegIdx, static_cast<int32>(Segment.ImpactResponse), Segment.SurfaceType.GetValue());
			}
			else if (AActor* DiagActor = Segment.HitActor.Get())
			{
				const bool bHasInterface = DiagActor->GetClass()->ImplementsInterface(UBallisticsDamageInterface::StaticClass());
				if (!bHasInterface)
				{
					NPS_LOG(Warning,
						TEXT("[NPS] FlightLog #%d seg %d - hit actor %s does NOT implement IBallisticsDamageInterface; damage will NOT apply. response=%d bone=%s"),
						FlightLog.LaunchID, SegIdx, *DiagActor->GetName(),
						static_cast<int32>(Segment.ImpactResponse),
						Segment.HitBoneName.IsNone() ? TEXT("<none>") : *Segment.HitBoneName.ToString());
				}
			}

			if (Segment.HitActor.IsValid())
			{
				AActor* HitActor = Segment.HitActor.Get();
				if (HitActor && HitActor->GetClass()->ImplementsInterface(UBallisticsDamageInterface::StaticClass()))
				{
					FBallisticDamageContext DamageContext;
					DamageContext.HitResult.ImpactPoint = Segment.EndLocation;
					DamageContext.HitResult.ImpactNormal = Segment.ImpactNormal;
					DamageContext.HitResult.BoneName = Segment.HitBoneName;
					DamageContext.InitialConditions = CanonicalConditions;
					DamageContext.ImpactResponse = Segment.ImpactResponse;
					DamageContext.ImpactSnapshot.Position = Segment.EndLocation;
					DamageContext.ImpactSnapshot.Speed = Segment.SpeedAtStart;
					DamageContext.ImpactSnapshot.ElapsedTime = Segment.TimeAtImpact;
					DamageContext.KineticEnergy = 0.5f * CanonicalConditions.Mass *
						Segment.SpeedAtStart * Segment.SpeedAtStart * KE_RAW_TO_JOULES;
					DamageContext.DistanceTraveled = FVector::Dist(
						CanonicalConditions.Origin,
						Segment.EndLocation);
					DamageContext.Instigator = ResolvedInstigator;
					DamageContext.ProjectileDataAsset = LaunchProjectileData;
					DamageContext.ProjectileActor = nullptr; // Client auth - no server-side projectile actor

					IBallisticsDamageInterface::Execute_OnBallisticDamage(HitActor, DamageContext);
				}
			}

			// Static surfaces can produce shatter fragments without a primary damage target.
			if (Segment.ImpactResponse == EImpactResponse::Shatter &&
				LaunchProjectileData && LaunchProjectileData->MaterialOverride)
			{
				const FBallisticMaterialResponse FragMaterial =
					LaunchProjectileData->MaterialOverride->GetResponseForSurface(Segment.SurfaceType);
				const FVector SegmentDir = (Segment.EndLocation - Segment.StartLocation).GetSafeNormal();
				const FVector IncomingVel = SegmentDir * Segment.SpeedAtStart;

				FHitResult ShatterHit;
				ShatterHit.ImpactPoint = Segment.EndLocation;
				ShatterHit.ImpactNormal = Segment.ImpactNormal;
				ShatterHit.Location = Segment.EndLocation;

				const float FragKEJoules = 0.5f * CanonicalConditions.Mass *
					Segment.SpeedAtStart * Segment.SpeedAtStart * KE_RAW_TO_JOULES;

				UBallisticsHitscanLibrary::DispatchShatterFragments(
					this, ShatterHit, IncomingVel, CanonicalConditions, FragMaterial,
					FragKEJoules, ResolvedInstigator, nullptr,
					LaunchProjectileData, FlightLog.LaunchID, ECC_Visibility);
			}
		}
	}
}

void UProjectileLauncherComponent::ClientFlightLogResult_Implementation(int32 LaunchID, EProjectileFlightValidationResult Result, int32 FailedSegmentIndex)
{
	FProjectileFlightLog DummyLog;
	DummyLog.LaunchID = LaunchID;
	OnFlightLogValidated.Broadcast(DummyLog, Result, FailedSegmentIndex);
}

void UProjectileLauncherComponent::AddFlightLog(const FProjectileFlightLog& FlightLog)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const int32 MaxLogs = Settings->MaxSavedFlightLogs;

	if (SavedFlightLogs.Num() >= MaxLogs)
	{
		SavedFlightLogs.RemoveAt(0);
	}

	SavedFlightLogs.Add(FlightLog);
}

EProjectileFlightValidationResult UProjectileLauncherComponent::ValidateFlightLog(
	const FProjectileFlightLog& FlightLog,
	const FBallisticsClientAuthoritativeLaunchSnapshot& LaunchSnapshot,
	int32 FirstSegmentIndex,
	int32& OutFailedSegmentIndex)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	OutFailedSegmentIndex = -1;
	if (FirstSegmentIndex < 0 || FirstSegmentIndex >= FlightLog.Segments.Num())
	{
		OutFailedSegmentIndex = FirstSegmentIndex;
		return EProjectileFlightValidationResult::InvalidSegment;
	}
	if (!LaunchSnapshot.ProjectileDataAsset ||
		!MatchesCanonicalConditions(
			FlightLog.InitialConditions,
			LaunchSnapshot.CanonicalConditions))
	{
		return EProjectileFlightValidationResult::InvalidInitialConditions;
	}

	const float ServerTime = GetWorld()->GetTimeSeconds();
	if (!FMath::IsFinite(FlightLog.ClientTimestamp) ||
		FlightLog.ClientTimestamp >
			ServerTime + Settings->ClientAuthFutureTimestampTolerance)
	{
		return EProjectileFlightValidationResult::InvalidTimestamp;
	}

	const float FlightLogAge = ServerTime - FlightLog.ClientTimestamp;
	if (FlightLogAge > Settings->MaxFlightLogAge)
	{
		return EProjectileFlightValidationResult::Expired;
	}

	const FProjectileInitialConditions& Canonical =
		LaunchSnapshot.CanonicalConditions;
	const bool bHasAcceptedPrefix = FirstSegmentIndex > 0;
	float PreviousImpactTime = bHasAcceptedPrefix
		? FlightLog.Segments[FirstSegmentIndex - 1].TimeAtImpact
		: 0.f;
	FVector PreviousImpactLocation = bHasAcceptedPrefix
		? FlightLog.Segments[FirstSegmentIndex - 1].EndLocation
		: Canonical.Origin;

	if (bHasAcceptedPrefix &&
		FlightLog.Segments[FirstSegmentIndex - 1].ImpactResponse == EImpactResponse::Ricochet &&
		!ValidateRicochetAngle(
			FlightLog.Segments[FirstSegmentIndex - 1],
			FlightLog.Segments[FirstSegmentIndex]))
	{
		OutFailedSegmentIndex = FirstSegmentIndex - 1;
		return EProjectileFlightValidationResult::InvalidRicochetAngle;
	}

	for (int32 i = FirstSegmentIndex; i < FlightLog.Segments.Num(); ++i)
	{
		const FProjectileFlightSegment& Segment = FlightLog.Segments[i];
		if (Segment.StartLocation.ContainsNaN() ||
			Segment.EndLocation.ContainsNaN() ||
			Segment.ImpactNormal.ContainsNaN() ||
			!FMath::IsFinite(Segment.SpeedAtStart) ||
			!FMath::IsFinite(Segment.TimeAtImpact) ||
			Segment.SpeedAtStart < 0.f ||
			Segment.TimeAtImpact <= PreviousImpactTime ||
			Segment.TimeAtImpact > Settings->MaxFlightLogAge)
		{
			OutFailedSegmentIndex = i;
			return EProjectileFlightValidationResult::InvalidSegment;
		}

		const FProjectileSnapshot ExpectedSnapshot =
			UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(
				Canonical,
				Segment.TimeAtImpact);
		const float SpeedTolerance = FMath::Max(
			100.f,
			ExpectedSnapshot.Speed * Settings->ClientAuthSpeedTolerance);
		const float AbsoluteSpeedCeiling =
			FMath::Max(Canonical.InitialSpeed, ExpectedSnapshot.Speed) *
				(1.f + Settings->ClientAuthSpeedTolerance) + 100.f;
		if (Segment.SpeedAtStart > AbsoluteSpeedCeiling ||
			(i == 0 && FMath::Abs(
				Segment.SpeedAtStart - ExpectedSnapshot.Speed) > SpeedTolerance))
		{
			OutFailedSegmentIndex = i;
			return EProjectileFlightValidationResult::PhysicallyImplausible;
		}

		if (i == 0 && FVector::Dist(
			ExpectedSnapshot.Position,
			Segment.EndLocation) > Settings->ClientAuthTrajectoryTolerance)
		{
			OutFailedSegmentIndex = i;
			return EProjectileFlightValidationResult::PhysicallyImplausible;
		}

		const float SegmentDuration = Segment.TimeAtImpact - PreviousImpactTime;
		const float MaxTravelDistance = AbsoluteSpeedCeiling * SegmentDuration +
			Settings->ClientAuthTrajectoryTolerance;
		if (FVector::Dist(PreviousImpactLocation, Segment.StartLocation) >
				MaxTravelDistance ||
			FVector::Dist(PreviousImpactLocation, Segment.EndLocation) >
			MaxTravelDistance ||
			FVector::Dist(Segment.StartLocation, Segment.EndLocation) >
				MaxTravelDistance)
		{
			OutFailedSegmentIndex = i;
			return EProjectileFlightValidationResult::PhysicallyImplausible;
		}

		if (!ValidateSegmentVisibility(Segment))
		{
			NPS_LOG(Warning,
				TEXT("[NPS] Validate #%d seg %d -> VisibilityFailed: trace from %s to %s blocked by geometry"),
				FlightLog.LaunchID, i, *Segment.StartLocation.ToString(), *Segment.EndLocation.ToString());
			OutFailedSegmentIndex = i;
			return EProjectileFlightValidationResult::VisibilityFailed;
		}

		if (Segment.ImpactResponse == EImpactResponse::Ricochet)
		{
			// Need the next segment to check outgoing direction
			if (i + 1 < FlightLog.Segments.Num())
			{
				if (!ValidateRicochetAngle(Segment, FlightLog.Segments[i + 1]))
				{
					NPS_LOG(Warning,
						TEXT("[NPS] Validate #%d seg %d -> InvalidRicochetAngle"),
						FlightLog.LaunchID, i);
					OutFailedSegmentIndex = i;
					return EProjectileFlightValidationResult::InvalidRicochetAngle;
				}
			}
		}

		if (Segment.ImpactResponse == EImpactResponse::Penetrate)
		{
			if (!ValidatePenetration(
				Segment,
				LaunchSnapshot.ProjectileDataAsset,
				LaunchSnapshot.CanonicalConditions))
			{
				NPS_LOG(Warning,
					TEXT("[NPS] Validate #%d seg %d -> InvalidPenetration: surface=%d at speed=%.1f cm/s"),
					FlightLog.LaunchID, i, Segment.SurfaceType.GetValue(), Segment.SpeedAtStart);
				OutFailedSegmentIndex = i;
				return EProjectileFlightValidationResult::InvalidPenetration;
			}
		}

		// If this segment claims a hit on an actor, validate the actor is near the claimed position.
		// GetActorLocation() returns the root (capsule bottom for Characters) - a head hit is ~180cm
		// above that. For skeletal-mesh targets, validate against the named bone's world location so
		// the tolerance is meaningful. For other actors, validate against the closest point on the
		// actor's bounding box.
		if (Segment.HitActor.IsValid())
		{
			const AActor* Target = Segment.HitActor.Get();
			if (!Target)
			{
				OutFailedSegmentIndex = i;
				return EProjectileFlightValidationResult::InvalidTarget;
			}

			FVector ReferenceLoc = Target->GetActorLocation();
			bool bResolvedReference = false;

			if (!Segment.HitBoneName.IsNone())
			{
				TArray<USkeletalMeshComponent*> SkelComps;
				Target->GetComponents<USkeletalMeshComponent>(SkelComps);
				for (USkeletalMeshComponent* SkelComp : SkelComps)
				{
					if (SkelComp && SkelComp->DoesSocketExist(Segment.HitBoneName))
					{
						if (!SkelComp->IsComponentTickEnabled())
						{
							continue;
						}
						ReferenceLoc = SkelComp->GetBoneLocation(Segment.HitBoneName);
						bResolvedReference = true;
						break;
					}
				}
			}

			if (!bResolvedReference)
			{
				FVector BoxOrigin = FVector::ZeroVector;
				FVector BoxExtent = FVector::ZeroVector;
				Target->GetActorBounds(/*bOnlyCollidingComponents*/ true, BoxOrigin, BoxExtent);
				if (!BoxExtent.IsNearlyZero())
				{
					ReferenceLoc = FVector(
						FMath::Clamp(Segment.EndLocation.X, BoxOrigin.X - BoxExtent.X, BoxOrigin.X + BoxExtent.X),
						FMath::Clamp(Segment.EndLocation.Y, BoxOrigin.Y - BoxExtent.Y, BoxOrigin.Y + BoxExtent.Y),
						FMath::Clamp(Segment.EndLocation.Z, BoxOrigin.Z - BoxExtent.Z, BoxOrigin.Z + BoxExtent.Z));
				}
			}

			float DistToActor = FVector::Dist(ReferenceLoc, Segment.EndLocation);

			// Motion-aware tolerance: the shooter aims at the client's smoothed/interpolated
			// pose, which trails the server actor in proportion to target speed plus network
			// delay. A flat tolerance passes walkers and rejects sprinters on the same honest
			// aim; a stationary target keeps the tight base value.
			const float EffectiveTolerance = Settings->ClientAuthHitLocationTolerance
				+ Target->GetVelocity().Size() * Settings->ClientAuthTargetVelocityLagSeconds;

			// Independently animated client/server limbs can legitimately exceed the bone
			// tolerance. Keep the tight bone check first, then accept impacts that still lie
			// within the server actor's colliding bounds.
			if (bResolvedReference && DistToActor > EffectiveTolerance)
			{
				FVector BoxOrigin = FVector::ZeroVector;
				FVector BoxExtent = FVector::ZeroVector;
				Target->GetActorBounds(true, BoxOrigin, BoxExtent);
				if (!BoxExtent.IsNearlyZero())
				{
					const FVector ClampedOnBounds(
						FMath::Clamp(Segment.EndLocation.X, BoxOrigin.X - BoxExtent.X, BoxOrigin.X + BoxExtent.X),
						FMath::Clamp(Segment.EndLocation.Y, BoxOrigin.Y - BoxExtent.Y, BoxOrigin.Y + BoxExtent.Y),
						FMath::Clamp(Segment.EndLocation.Z, BoxOrigin.Z - BoxExtent.Z, BoxOrigin.Z + BoxExtent.Z));
					const float DistToBounds = FVector::Dist(ClampedOnBounds, Segment.EndLocation);
					if (DistToBounds < DistToActor)
					{
						ReferenceLoc = ClampedOnBounds;
						DistToActor = DistToBounds;
					}
				}
			}

			if (DistToActor > EffectiveTolerance)
			{
				NPS_LOG(Warning,
					TEXT("[NPS] InvalidTarget: %s claimed bone='%s' impact=%s reference=%s distance=%.1fcm > tolerance=%.1fcm (base %.0f + motion)"),
					*Target->GetName(),
					Segment.HitBoneName.IsNone() ? TEXT("<none>") : *Segment.HitBoneName.ToString(),
					*Segment.EndLocation.ToString(), *ReferenceLoc.ToString(),
					DistToActor, EffectiveTolerance, Settings->ClientAuthHitLocationTolerance);
				OutFailedSegmentIndex = i;
				return EProjectileFlightValidationResult::InvalidTarget;
			}
		}

		PreviousImpactTime = Segment.TimeAtImpact;
		PreviousImpactLocation = Segment.EndLocation;
	}

	return EProjectileFlightValidationResult::Valid;
}

bool UProjectileLauncherComponent::ValidateSegmentVisibility(const FProjectileFlightSegment& Segment)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Ignore whole ATTACHMENT FAMILIES, not single actors. A pawn and its worn gear are
	// separate actors: a claim on the plate left the pawn's own server-posed body as an
	// obstruction (and vice versa), so honest hits on armored targets failed whenever the
	// server pose sat into the aimed line — the dominant "shot did nothing" case on moving
	// AI. Same on the shooter side: GetOwner() is the WEAPON, so the shooting pawn itself
	// could obstruct its own shot.
	FHitResult Hit;
	FCollisionQueryParams Params;
	auto IgnoreFamily = [&Params](AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		AActor* Root = Actor;
		while (AActor* Parent = Root->GetAttachParentActor())
		{
			Root = Parent;
		}
		Params.AddIgnoredActor(Root);
		TArray<AActor*> Attached;
		Root->GetAttachedActors(Attached, /*bResetArray*/true, /*bRecursivelyIncludeAttachedActors*/true);
		for (AActor* Child : Attached)
		{
			Params.AddIgnoredActor(Child);
		}
	};
	IgnoreFamily(GetOwner());
	IgnoreFamily(ResolveLaunchingPawn());
	if (Segment.HitActor.IsValid())
	{
		IgnoreFamily(Segment.HitActor.Get());
	}

	// Reject obstructions outside the endpoint tolerance. The budget grows with the claimed
	// target's server velocity — client-side smoothing/interpolation means the aimed line and
	// the server's world diverge in proportion to how fast the target moves.
	if (World->LineTraceSingleByChannel(Hit, Segment.StartLocation, Segment.EndLocation, ECC_Visibility, Params))
	{
		const float DistToClaimedEnd = FVector::Dist(Hit.ImpactPoint, Segment.EndLocation);
		float Tolerance = 50.f; // base allowance for minor position differences
		if (const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get())
		{
			if (const AActor* Target = Segment.HitActor.Get())
			{
				Tolerance += Target->GetVelocity().Size() * Settings->ClientAuthTargetVelocityLagSeconds;
			}
		}

		return DistToClaimedEnd < Tolerance;
	}

	// No blocking geometry.
	return true;
}

bool UProjectileLauncherComponent::ValidateRicochetAngle(const FProjectileFlightSegment& CurrentSegment, const FProjectileFlightSegment& NextSegment)
{
	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();

	// Incoming direction: from start to end of current segment
	const FVector IncomingDir = (CurrentSegment.EndLocation - CurrentSegment.StartLocation).GetSafeNormal();

	// Outgoing direction: from start of next segment (should be ~same as end of current) to end of next
	const FVector OutgoingDir = (NextSegment.EndLocation - NextSegment.StartLocation).GetSafeNormal();

	// Expected reflection: reflect incoming around the impact normal
	const FVector ExpectedReflection = FMath::GetReflectionVector(IncomingDir, CurrentSegment.ImpactNormal);

	// Compare the angle between expected and actual outgoing direction
	const float DotProduct = FVector::DotProduct(ExpectedReflection, OutgoingDir);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.f, 1.f)));

	return AngleDeg <= Settings->RicochetAngleValidationTolerance;
}

bool UProjectileLauncherComponent::ValidatePenetration(
	const FProjectileFlightSegment& Segment,
	const UBallisticsProjectileDataAsset* LaunchProjectileData,
	const FProjectileInitialConditions& CanonicalConditions) const
{
	if (!LaunchProjectileData || !LaunchProjectileData->MaterialOverride)
	{
		return true;
	}

	const FBallisticMaterialResponse Response =
		LaunchProjectileData->MaterialOverride->GetResponseForSurface(
			Segment.SurfaceType);

	const float KineticEnergy =
		0.5f * CanonicalConditions.Mass * Segment.SpeedAtStart *
		Segment.SpeedAtStart * KE_RAW_TO_JOULES;

	return KineticEnergy > Response.Toughness;
}
// Hit Validation (Client Predicted mode)

bool UProjectileLauncherComponent::ServerCheckClientHit_Validate(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	return HitActor != nullptr;
}

void UProjectileLauncherComponent::ServerCheckClientHit_Implementation(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition)
{
	if (!HitActor)
	{
		return;
	}

	bool bConfirmed = false;

	if (OnValidateHit.IsBound())
	{
		bConfirmed = OnValidateHit.Execute(HitActor, ClaimTimestamp, ClaimPosition);
	}
	else
	{
		// Reject hits when the game has not bound a validation policy.
		NPS_LOG(Warning,
			TEXT("BallisticsCore: ServerCheckClientHit called but no OnValidateHit delegate is bound. "
			     "Hit rejected. Bind your validation logic to OnValidateHit on the launcher component."));
	}

	OnHitValidationResult.Broadcast(HitActor, ClaimPosition, bConfirmed);
}
// Shared Internals

void UProjectileLauncherComponent::HandleProjectileImpactCosmetic(
	const FHitResult& Hit,
	EImpactResponse Response,
	int32 ProjectileID,
	UBallisticsProjectileDataAsset* SourceProjectileData)
{
	const TEnumAsByte<EPhysicalSurface> Surface =
		Hit.PhysMaterial.IsValid()
			? Hit.PhysMaterial->SurfaceType
			: TEnumAsByte<EPhysicalSurface>(SurfaceType_Default);
	if (UWorld* EventWorld = GetWorld(); EventWorld && EventWorld->GetNetMode() != NM_Client)
	{
		FBallisticsAuthoritativeImpactEvent ImpactEvent;
		ImpactEvent.HitResult = Hit;
		ImpactEvent.ImpactResponse = Response;
		ImpactEvent.ProjectileID = ProjectileID;
		ImpactEvent.ProjectileDataAsset = SourceProjectileData;
		ImpactEvent.Instigator = ResolveLaunchingPawn();
		ImpactEvent.HitActor = Hit.GetActor();
		OnAuthoritativeImpact.Broadcast(ImpactEvent);
	}

	if (!bEnableCosmeticRouting || !SourceProjectileData ||
		!SourceProjectileData->GetImpactCosmetic(Surface).HasAnyAsset())
	{
		return;
	}

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Impact;
	Event.EventID = ProjectileID;
	Event.Location = Hit.ImpactPoint;
	Event.Direction = Hit.ImpactNormal.GetSafeNormal();
	Event.SurfaceType = Surface;
	Event.ImpactResponse = static_cast<uint8>(Response);
	Event.EmissionServerTime = GetSynchronizedTime();
	PopulateImpactCosmeticEvent(Event, SourceProjectileData, Surface);
	PrepareAuxiliaryWireTemplate(Event, SourceProjectileData, (uint32(EProjectileCosmeticEventType::Impact) << 8) | uint32(Surface));

	UWorld* World = GetWorld();
	UProjectileCosmeticsManager* Manager = GetCosmeticsManager();
	if (!World || !Manager)
	{
		return;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		Manager->PlayLocalEvent(Event);
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		if (!IsLocallyControlledLauncher())
		{
			return;
		}

		Manager->PlayLocalEvent(Event);
		const ELaunchAuthorityMode Mode = SourceProjectileData->bOverrideAuthorityMode
			? SourceProjectileData->AuthorityModeOverride
			: UBallisticsCoreSettings::Get()->LaunchAuthorityMode;
		if (Mode == ELaunchAuthorityMode::ClientAuthoritative || SourceProjectileData->bUseHitscan)
		{
			ServerNotifyImpactCosmetic(
				Hit.ImpactPoint,
				Hit.ImpactNormal.GetSafeNormal(),
				Surface,
				static_cast<uint8>(Response),
				ProjectileID);
		}
		return;
	}

	APlayerController* Excluded = ShouldExcludeOwningClientFromImpactRelay(SourceProjectileData)
		? GetOwningPlayerController()
		: nullptr;
	Manager->QueueBroadcastEvent(Event, Excluded);
}

void UProjectileLauncherComponent::HandleProjectileFlyByCosmetic(
	APawn* NearbyPawn,
	const FVector& ClosestPoint,
	float MissDistance,
	float ProjectileSpeed,
	const FVector& ProjectileDirection,
	UBallisticsProjectileDataAsset* SourceProjectileData)
{
	APawn* LaunchingPawn = ResolveLaunchingPawn();
	if (!NearbyPawn || NearbyPawn == LaunchingPawn)
	{
		return;
	}

	const float ClampedMissDistance = FMath::Max(0.f, MissDistance);
	const float ClampedProjectileSpeed = FMath::Max(0.f, ProjectileSpeed);
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_Client)
	{
		FBallisticsAuthoritativeFlyByEvent FlyByEvent;
		FlyByEvent.Listener = NearbyPawn;
		FlyByEvent.ClosestPoint = ClosestPoint;
		FlyByEvent.MissDistance = ClampedMissDistance;
		FlyByEvent.ProjectileSpeed = ClampedProjectileSpeed;
		FlyByEvent.ProjectileDataAsset = SourceProjectileData;
		FlyByEvent.Instigator = LaunchingPawn;
		OnAuthoritativeFlyBy.Broadcast(FlyByEvent);
	}
	else if (World && IsLocallyControlledLauncher())
	{
		const ELaunchAuthorityMode Mode = SourceProjectileData && SourceProjectileData->bOverrideAuthorityMode
			? SourceProjectileData->AuthorityModeOverride
			: UBallisticsCoreSettings::Get()->LaunchAuthorityMode;
		if (Mode == ELaunchAuthorityMode::ClientAuthoritative)
		{
			ServerNotifyFlyByCosmetic(
				ClosestPoint,
				ProjectileDirection.GetSafeNormal(),
				ClampedMissDistance,
				ClampedProjectileSpeed);
		}
	}

	if (!bEnableCosmeticRouting || !SourceProjectileData ||
		SourceProjectileData->FlyBySound.IsNull())
	{
		return;
	}

	const FVector ToListener = NearbyPawn->GetActorLocation() - ClosestPoint;
	const FVector SideAxis = FVector::CrossProduct(
		ProjectileDirection.GetSafeNormal(),
		FVector::UpVector);
	const bool bPassesRight = FVector::DotProduct(SideAxis, ToListener) >= 0.f;

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::FlyBy;
	Event.EventID = GenerateCosmeticEventID();
	Event.Location = ClosestPoint;
	Event.Direction = ProjectileDirection.GetSafeNormal();
	Event.EmissionServerTime = GetSynchronizedTime();
	Event.MissDistance = ClampedMissDistance;
	Event.ProjectileSpeed = ClampedProjectileSpeed;
	Event.bPassesRight = bPassesRight;
	PopulateFlyByCosmeticEvent(Event, SourceProjectileData);
	PrepareAuxiliaryWireTemplate(Event, SourceProjectileData, uint32(EProjectileCosmeticEventType::FlyBy) << 8);
	UProjectileCosmeticsManager* Manager = GetCosmeticsManager();
	if (!World || !Manager)
	{
		return;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		if (NearbyPawn->IsLocallyControlled())
		{
			Manager->PlayLocalEvent(Event);
		}
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (APlayerController* Target = Cast<APlayerController>(NearbyPawn->GetController()))
	{
		Manager->QueueTargetedEvent(Event, Target);
	}
}

void UProjectileLauncherComponent::HandleDetonationCosmetic(
	const FVector& Location,
	UBallisticsExplosiveDataAsset* ExplosiveData)
{
	if (!bEnableCosmeticRouting || !ExplosiveData ||
		!ExplosiveData->DetonationCosmetic.HasAnyAsset())
	{
		return;
	}

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Detonation;
	Event.EventID = GenerateCosmeticEventID();
	Event.Location = Location;
	Event.Direction = FVector::UpVector;
	Event.EmissionServerTime = GetSynchronizedTime();
	PopulateDetonationCosmeticEvent(Event, ExplosiveData);
	PrepareAuxiliaryWireTemplate(Event, ExplosiveData, uint32(EProjectileCosmeticEventType::Detonation) << 8);

	UWorld* World = GetWorld();
	UProjectileCosmeticsManager* Manager = GetCosmeticsManager();
	if (!World || !Manager)
	{
		return;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		Manager->PlayLocalEvent(Event);
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		if (IsLocallyControlledLauncher())
		{
			Manager->PlayLocalEvent(Event);
			ServerNotifyDetonationCosmetic(Location);
		}
		return;
	}

	APlayerController* Excluded = ShouldExcludeOwningClientFromImpactRelay(ProjectileDataAsset)
		? GetOwningPlayerController()
		: nullptr;
	Manager->QueueBroadcastEvent(Event, Excluded);
}

bool UProjectileLauncherComponent::ServerNotifyImpactCosmetic_Validate(
	FVector_NetQuantize10 Location,
	FVector_NetQuantizeNormal Normal,
	EPhysicalSurface SurfaceType,
	uint8 Response,
	int32 ProjectileID)
{
	return ValidateCosmeticVector(Location) &&
		Normal.IsNormalized() &&
		Response <= static_cast<uint8>(EImpactResponse::None);
}

void UProjectileLauncherComponent::ServerNotifyImpactCosmetic_Implementation(
	FVector_NetQuantize10 Location,
	FVector_NetQuantizeNormal Normal,
	EPhysicalSurface SurfaceType,
	uint8 Response,
	int32 ProjectileID)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("impact")))
	{
		return;
	}

	UBallisticsProjectileDataAsset* SourceProjectileData = ProjectileDataAsset;
	if (!bEnableCosmeticRouting || !SourceProjectileData ||
		!SourceProjectileData->GetImpactCosmetic(SurfaceType).HasAnyAsset())
	{
		return;
	}

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Impact;
	Event.EventID = ProjectileID;
	Event.Location = Location;
	Event.Direction = Normal;
	Event.SurfaceType = SurfaceType;
	Event.ImpactResponse = Response;
	Event.EmissionServerTime = GetSynchronizedTime();
	PopulateImpactCosmeticEvent(Event, SourceProjectileData, SurfaceType);
	PrepareAuxiliaryWireTemplate(Event, SourceProjectileData, (uint32(EProjectileCosmeticEventType::Impact) << 8) | uint32(SurfaceType));

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueBroadcastEvent(Event, GetOwningPlayerController());
	}
}

bool UProjectileLauncherComponent::ServerNotifyFlyByCosmetic_Validate(
	FVector_NetQuantize10 ClosestPoint,
	FVector_NetQuantizeNormal ProjectileDirection,
	float MissDistance,
	float ProjectileSpeed)
{
	return ValidateCosmeticVector(ClosestPoint) &&
		ValidateCosmeticVector(ProjectileDirection) &&
		!ProjectileDirection.IsNearlyZero() &&
		FMath::IsFinite(MissDistance) &&
		MissDistance >= 0.f &&
		MissDistance <= 1000.f &&
		FMath::IsFinite(ProjectileSpeed) &&
		ProjectileSpeed >= 0.f &&
		ProjectileSpeed <= 10000000.f;
}

void UProjectileLauncherComponent::ServerNotifyFlyByCosmetic_Implementation(
	FVector_NetQuantize10 ClosestPoint,
	FVector_NetQuantizeNormal ProjectileDirection,
	float MissDistance,
	float ProjectileSpeed)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("fly-by")))
	{
		return;
	}

	UWorld* World = GetWorld();
	UProjectileBallisticsSubsystem* Subsystem = World
		? World->GetSubsystem<UProjectileBallisticsSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return;
	}

	APawn* NearbyPawn = nullptr;
	float NearestDistanceSq = TNumericLimits<float>::Max();
	const float AllowedDistance = FMath::Min(MissDistance + 100.f, 1000.f);
	const float AllowedDistanceSq = FMath::Square(AllowedDistance);
	for (const TWeakObjectPtr<APawn>& WeakListener : Subsystem->GetFlyByListeners())
	{
		APawn* Candidate = WeakListener.Get();
		if (!Candidate || Candidate == ResolveLaunchingPawn())
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(
			Candidate->GetActorLocation(), ClosestPoint);
		if (DistanceSq <= AllowedDistanceSq && DistanceSq < NearestDistanceSq)
		{
			NearbyPawn = Candidate;
			NearestDistanceSq = DistanceSq;
		}
	}
	if (!NearbyPawn)
	{
		return;
	}

	UBallisticsProjectileDataAsset* SourceProjectileData = ProjectileDataAsset;
	FBallisticsAuthoritativeFlyByEvent FlyByEvent;
	FlyByEvent.Listener = NearbyPawn;
	FlyByEvent.ClosestPoint = ClosestPoint;
	FlyByEvent.MissDistance = FMath::Sqrt(NearestDistanceSq);
	FlyByEvent.ProjectileSpeed = ProjectileSpeed;
	FlyByEvent.ProjectileDataAsset = SourceProjectileData;
	FlyByEvent.Instigator = ResolveLaunchingPawn();
	OnAuthoritativeFlyBy.Broadcast(FlyByEvent);

	if (!bEnableCosmeticRouting || !SourceProjectileData ||
		SourceProjectileData->FlyBySound.IsNull())
	{
		return;
	}

	APlayerController* Target = Cast<APlayerController>(NearbyPawn->GetController());
	if (!Target || Target == GetOwningPlayerController())
	{
		return;
	}
	const FVector ToListener = NearbyPawn->GetActorLocation() - ClosestPoint;
	const FVector SideAxis = FVector::CrossProduct(
		ProjectileDirection.GetSafeNormal(), FVector::UpVector);

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::FlyBy;
	Event.EventID = GenerateCosmeticEventID();
	Event.Location = ClosestPoint;
	Event.Direction = ProjectileDirection.GetSafeNormal();
	Event.EmissionServerTime = GetSynchronizedTime();
	Event.MissDistance = FMath::Sqrt(NearestDistanceSq);
	Event.ProjectileSpeed = ProjectileSpeed;
	Event.bPassesRight = FVector::DotProduct(SideAxis, ToListener) >= 0.f;
	PopulateFlyByCosmeticEvent(Event, SourceProjectileData);
	PrepareAuxiliaryWireTemplate(Event, SourceProjectileData, uint32(EProjectileCosmeticEventType::FlyBy) << 8);

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueTargetedEvent(Event, Target);
	}
}

bool UProjectileLauncherComponent::ServerNotifyDetonationCosmetic_Validate(
	FVector_NetQuantize10 Location)
{
	return ValidateCosmeticVector(Location);
}

void UProjectileLauncherComponent::ServerNotifyDetonationCosmetic_Implementation(
	FVector_NetQuantize10 Location)
{
	if (!TryAcceptClientCosmeticClaim(TEXT("detonation")))
	{
		return;
	}

	UBallisticsExplosiveDataAsset* ExplosiveData = ProjectileDataAsset
		? ProjectileDataAsset->ExplosiveConfig
		: nullptr;
	if (!bEnableCosmeticRouting || !ExplosiveData ||
		!ExplosiveData->DetonationCosmetic.HasAnyAsset())
	{
		return;
	}

	FProjectileCosmeticEvent Event;
	Event.Type = EProjectileCosmeticEventType::Detonation;
	Event.EventID = GenerateCosmeticEventID();
	Event.Location = Location;
	Event.Direction = FVector::UpVector;
	Event.EmissionServerTime = GetSynchronizedTime();
	PopulateDetonationCosmeticEvent(Event, ExplosiveData);
	PrepareAuxiliaryWireTemplate(Event, ExplosiveData, uint32(EProjectileCosmeticEventType::Detonation) << 8);

	if (UProjectileCosmeticsManager* Manager = GetCosmeticsManager())
	{
		Manager->QueueBroadcastEvent(Event, GetOwningPlayerController());
	}
}

void UProjectileLauncherComponent::PrepareAuxiliaryWireTemplate(FProjectileCosmeticEvent& Event, const UObject* Asset, uint32 Variant)
{
	const uint64 Key = (uint64(Asset->GetUniqueID()) << 32) | Variant;
	if (const auto* Cached = AuxiliaryWireTemplates.Find(Key); Cached && Cached->Asset.Get() == Asset)
	{
		const auto& Old = Cached->Template->Event;
		// Auxiliary asset fields remain runtime-mutable; validate scalars/soft references without serialization or hashing.
		if (Old.Type == Event.Type && Old.Effect == Event.Effect && Old.DecalMaterial == Event.DecalMaterial &&
			Old.Sounds == Event.Sounds && Old.EffectScale == Event.EffectScale && Old.EffectLifeSpan == Event.EffectLifeSpan &&
			Old.VolumeMultiplier == Event.VolumeMultiplier && Old.PitchMultiplier == Event.PitchMultiplier &&
			Old.DecalSize == Event.DecalSize && Old.DecalLifeSpan == Event.DecalLifeSpan &&
			Old.VisualRadius == Event.VisualRadius && Old.AudibleRadius == Event.AudibleRadius &&
			Old.PropagationSpeed == Event.PropagationSpeed && Old.bUsePropagationDelay == Event.bUsePropagationDelay)
		{
			Event.WireTemplate = Cached->Template;
			return;
		}
	}
	if (!AuxiliaryWireTemplates.Contains(Key) && AuxiliaryWireTemplates.Num() >= 64) { AuxiliaryWireTemplates.Reset(); }
	FCachedAuxiliaryWireTemplate Cached;
	Cached.Asset = Asset;
	Cached.Template = BallisticsCosmeticWire::Prepare(Event);
	Event.WireTemplate = Cached.Template;
	AuxiliaryWireTemplates.Add(Key, MoveTemp(Cached));
}

UProjectileCosmeticsManager* UProjectileLauncherComponent::GetCosmeticsManager() const
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem =
			World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			return Subsystem->GetCosmeticsManager();
		}
	}
	return nullptr;
}

APlayerController* UProjectileLauncherComponent::GetOwningPlayerController() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (AController* InstigatorController = OwnerActor->GetInstigatorController())
	{
		if (APlayerController* PlayerController =
			Cast<APlayerController>(InstigatorController))
		{
			return PlayerController;
		}
	}

	for (const AActor* Current = OwnerActor; Current; Current = Current->GetOwner())
	{
		if (const APawn* Pawn = Cast<APawn>(Current))
		{
			if (APlayerController* PlayerController =
				Cast<APlayerController>(Pawn->GetController()))
			{
				return PlayerController;
			}
		}
		if (APlayerController* PlayerController =
			Cast<APlayerController>(const_cast<AActor*>(Current)))
		{
			return PlayerController;
		}
	}
	return nullptr;
}

bool UProjectileLauncherComponent::IsLocallyControlledLauncher() const
{
	if (const APlayerController* Controller = GetOwningPlayerController())
	{
		return Controller->IsLocalController();
	}
	if (const AActor* OwnerActor = GetOwner())
	{
		if (const APawn* Pawn = Cast<APawn>(OwnerActor))
		{
			return Pawn->IsLocallyControlled();
		}
	}
	return false;
}

bool UProjectileLauncherComponent::HasConfiguredLaunchCosmetics() const
{
	if (bRouteCosmeticEventsWithoutAssets)
	{
		return true;
	}
	return GetResolvedLaunchCosmetics().HasAnyAsset();
}

float UProjectileLauncherComponent::GetLaunchCosmeticRoutingRadius() const
{
	const FResolvedLaunchCosmetics& Resolved = GetResolvedLaunchCosmetics();
	if (Resolved.AudibleRadius > 0.f)
	{
		return Resolved.AudibleRadius;
	}
	return bRouteCosmeticEventsWithoutAssets
		? FMath::Max(0.f, CustomCosmeticRoutingRadius)
		: 0.f;
}

bool UProjectileLauncherComponent::ResolveLaunchEffectSource(
	FName Name,
	USceneComponent*& OutComponent,
	FName& OutSocketOverride) const
{
	OutComponent = nullptr;
	OutSocketOverride = NAME_None;
	RebuildLaunchContributorCache();
	const UBallisticsLaunchContributorComponent* SelectedContributor = nullptr;
	int32 SelectedPriority = TNumericLimits<int32>::Lowest();
	int32 SelectedDepth = INDEX_NONE;
	FString SelectedPath;
	for (const TWeakObjectPtr<UBallisticsLaunchContributorComponent>& Weak :
		LaunchContributors)
	{
		const UBallisticsLaunchContributorComponent* Contributor = Weak.Get();
		if (!Contributor)
		{
			continue;
		}
		int32 Priority = 0;
		if (!Contributor->GetSourceOverridePriority(Name, Priority))
		{
			continue;
		}
		const int32 Depth = Contributor->GetAttachmentDepth();
		const FString Path = Contributor->GetPathName();
		if (!SelectedContributor || Priority > SelectedPriority ||
			(Priority == SelectedPriority && Depth > SelectedDepth) ||
			(Priority == SelectedPriority && Depth == SelectedDepth &&
				Path > SelectedPath))
		{
			SelectedContributor = Contributor;
			SelectedPriority = Priority;
			SelectedDepth = Depth;
			SelectedPath = Path;
		}
	}
	if (SelectedContributor)
	{
		SelectedContributor->ResolveSourceOverride(
			Name,
			OutComponent,
			OutSocketOverride);
		return OutComponent != nullptr;
	}
	OutComponent = GetLaunchEffectSourceComponent(Name);
	return OutComponent != nullptr;
}

bool UProjectileLauncherComponent::ResolveLaunchSourceTransform(
	FName SourceBinding,
	FName SocketName,
	FTransform& OutTransform) const
{
	OutTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	USceneComponent* SourceComponent = nullptr;
	FName SocketOverride = NAME_None;
	if (!ResolveLaunchEffectSource(SourceBinding, SourceComponent, SocketOverride) ||
		!SourceComponent)
	{
		return false;
	}
	const FName ResolvedSocket = SocketOverride.IsNone() ? SocketName : SocketOverride;
	if (ResolvedSocket.IsNone())
	{
		OutTransform = SourceComponent->GetComponentTransform();
		return true;
	}
	if (!SourceComponent->DoesSocketExist(ResolvedSocket))
	{
		return false;
	}
	OutTransform = SourceComponent->GetSocketTransform(ResolvedSocket, RTS_World);
	return true;
}

bool UProjectileLauncherComponent::ResolveLaunchEffectTransform(
	const FBallisticsLaunchEffect& Effect,
	const FVector& FallbackOrigin,
	const FVector& Direction,
	FTransform& OutTransform) const
{
	FTransform BaseTransform(Direction.Rotation(), FallbackOrigin);
	if (!Effect.SocketName.IsNone())
	{
		if (!ResolveLaunchSourceTransform(
			Effect.SourceBinding,
			Effect.SocketName,
			BaseTransform))
		{
			OutTransform = BaseTransform;
			return false;
		}
	}

	const FVector Location =
		BaseTransform.TransformPosition(Effect.LocationOffset);
	const FQuat Rotation =
		BaseTransform.GetRotation() * Effect.RotationOffset.Quaternion();
	OutTransform = FTransform(Rotation, Location);
	return true;
}

APawn* UProjectileLauncherComponent::ResolveLaunchingPawn() const
{
	AActor* LauncherOwner = GetOwner();
	if (!LauncherOwner)
	{
		return nullptr;
	}
	if (APawn* OwnerPawn = Cast<APawn>(LauncherOwner))
	{
		return OwnerPawn;
	}
	if (APawn* OwnerInstigator = LauncherOwner->GetInstigator())
	{
		return OwnerInstigator;
	}
	for (AActor* Current = LauncherOwner->GetOwner(); Current; Current = Current->GetOwner())
	{
		if (APawn* ChainPawn = Cast<APawn>(Current))
		{
			return ChainPawn;
		}
	}
	return nullptr;
}

float UProjectileLauncherComponent::GetSynchronizedTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.f;
}

int32 UProjectileLauncherComponent::GenerateCosmeticEventID()
{
	const uint32 OwnerKey = GetOwner() ? GetTypeHash(GetOwner()->GetFName()) : 0u;
	return static_cast<int32>(
		HashCombineFast(OwnerKey, static_cast<uint32>(++CosmeticEventCounter)) &
		MAX_int32);
}

int32 UProjectileLauncherComponent::GenerateSequenceID()
{
	uint32 OwnerKey = GetOwner() ? GetTypeHash(GetOwner()->GetFName()) : 0u;
	if (const APlayerController* Controller = GetOwningPlayerController())
	{
		if (const APlayerState* PlayerState = Controller->PlayerState)
		{
			OwnerKey = static_cast<uint32>(PlayerState->GetPlayerId());
		}
	}

	const uint32 LauncherKey = HashCombineFast(
		OwnerKey,
		static_cast<uint32>(GetUniqueID()));
	int32 Result = static_cast<int32>(
		HashCombineFast(LauncherKey, static_cast<uint32>(++SequenceCounter)) &
		MAX_int32);
	return Result == INDEX_NONE ? 0 : Result;
}

bool UProjectileLauncherComponent::ValidateCosmeticVector(const FVector& Vector) const
{
	return !Vector.ContainsNaN() &&
		FMath::Abs(Vector.X) < UE_BIG_NUMBER &&
		FMath::Abs(Vector.Y) < UE_BIG_NUMBER &&
		FMath::Abs(Vector.Z) < UE_BIG_NUMBER;
}

bool UProjectileLauncherComponent::TryAcceptClientCosmeticClaim(
	const TCHAR* ClaimType)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Standalone)
	{
		return true;
	}

	const float Now = World->GetTimeSeconds();
	if (!FMath::IsFinite(ClientCosmeticClaimWindowStart) ||
		Now < ClientCosmeticClaimWindowStart ||
		Now - ClientCosmeticClaimWindowStart >= 1.f)
	{
		ClientCosmeticClaimWindowStart = Now;
		ClientCosmeticClaimsThisWindow = 0;
	}

	const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get();
	const int32 Limit = Settings
		? FMath::Clamp(Settings->MaxClientCosmeticClaimsPerSecond, 1, 512)
		: 256;
	if (ClientCosmeticClaimsThisWindow >= Limit)
	{
		if (Now - LastClientCosmeticClaimWarningTime >= 1.f)
		{
			LastClientCosmeticClaimWarningTime = Now;
			NPS_LOG(Warning,
				TEXT("NPS dropped %s cosmetic claims from %s at the server rate limit (%d/s)."),
				ClaimType,
				*GetPathName(),
				Limit);
		}
		return false;
	}

	++ClientCosmeticClaimsThisWindow;
	return true;
}

bool UProjectileLauncherComponent::ShouldExcludeOwningClientFromImpactRelay(
	const UBallisticsProjectileDataAsset* SourceProjectileData) const
{
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || Controller->IsLocalController())
	{
		return false;
	}

	const ELaunchAuthorityMode Mode =
		SourceProjectileData && SourceProjectileData->bOverrideAuthorityMode
			? SourceProjectileData->AuthorityModeOverride
			: UBallisticsCoreSettings::Get()->LaunchAuthorityMode;
	if (Mode == ELaunchAuthorityMode::ClientPredicted ||
		Mode == ELaunchAuthorityMode::ClientAuthoritative)
	{
		return true;
	}

	return UBallisticsCoreSettings::Get()->ServerAuthCosmeticMode !=
		EServerAuthCosmeticMode::NoLocalCosmetic;
}

AActor* UProjectileLauncherComponent::SpawnProjectile(const FProjectileInitialConditions& Conditions)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<AActor> ClassToSpawn =
		ProjectileDataAsset ? ProjectileDataAsset->ResolveProjectileClass() : nullptr;
	if (!ClassToSpawn)
	{
		if (const UBallisticsCoreSettings* Settings = UBallisticsCoreSettings::Get())
		{
			if (UClass* DefaultClass = Settings->DefaultProjectileClass.TryLoadClass<AActor>())
			{
				ClassToSpawn = DefaultClass;
			}
		}
	}
	if (!ClassToSpawn)
	{
		return nullptr;
	}

	// Try the pool first
	AActor* Projectile = nullptr;
	AActor* LaunchOwner = GetOwner();
	APawn* LaunchingPawn = ResolveLaunchingPawn();
	if (UProjectileBallisticsSubsystem* Subsystem = World->GetSubsystem<UProjectileBallisticsSubsystem>())
	{
		Projectile = Subsystem->AcquireProjectile(
			ClassToSpawn,
			Conditions.Origin,
			Conditions.Direction.Rotation(),
			LaunchOwner,
			LaunchingPawn);
	}

	if (!Projectile)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = LaunchOwner;
		SpawnParams.Instigator = LaunchingPawn;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Projectile = World->SpawnActor<AActor>(ClassToSpawn, Conditions.Origin, Conditions.Direction.Rotation(), SpawnParams);
	}

	if (!Projectile)
	{
		return nullptr;
	}

	if (UProjectileBallisticsComponent* Comp = Projectile->FindComponentByClass<UProjectileBallisticsComponent>())
	{
		Comp->SetSourceLauncher(this, ProjectileDataAsset);

		if (ProjectileDataAsset && ProjectileDataAsset->MaterialOverride)
		{
			Comp->MaterialDataAsset = ProjectileDataAsset->MaterialOverride;
		}

		if (ProjectileDataAsset && ProjectileDataAsset->SlidingOverride.bOverride)
		{
			Comp->bEnableSliding = ProjectileDataAsset->SlidingOverride.bEnableSliding;
			Comp->MinSlidingSpeed = ProjectileDataAsset->SlidingOverride.MinSlidingSpeed;
			Comp->SlidingFriction = ProjectileDataAsset->SlidingOverride.SlidingFriction;
		}

		if (UBallisticsDetonationComponent* Detonation =
			Projectile->FindComponentByClass<UBallisticsDetonationComponent>())
		{
			Detonation->ConfigureExplosive(
				ProjectileDataAsset ? ProjectileDataAsset->ExplosiveConfig : nullptr);
		}
		else if (ProjectileDataAsset && ProjectileDataAsset->ExplosiveConfig)
		{
			static TSet<const UClass*> WarnedProjectileClasses;
			if (!WarnedProjectileClasses.Contains(Projectile->GetClass()))
			{
				WarnedProjectileClasses.Add(Projectile->GetClass());
				UE_LOG(
					LogNPS,
					Warning,
					TEXT("Projectile class %s is using explosive projectile '%s' but has no "
						"BallisticsDetonationComponent. The projectile will not detonate."),
					*GetNameSafe(Projectile->GetClass()),
					*GetNameSafe(ProjectileDataAsset));
			}
		}

		Comp->InitializeProjectile(Conditions);
	}

	return Projectile;
}

int32 UProjectileLauncherComponent::GenerateSeed()
{
	// Knuth multiplicative hash of owner ID + launch counter for deterministic seeds
	const uint32 OwnerID = GetOwner() ? GetOwner()->GetUniqueID() : 0;
	const uint32 Combined = OwnerID ^ (LaunchCounter++ * 2654435761u);
	return static_cast<int32>(Combined);
}
