// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsLaunchContributorComponent.h"

#include "BallisticsCosmeticTypes.h"
#include "BallisticsDebugDraw.h"
#include "ProjectileBallisticsSubsystem.h"
#include "ProjectileLauncherComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UBallisticsLaunchContributorComponent::UBallisticsLaunchContributorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
#if UE_WITH_IRIS
	// Iris uses registered subobjects; legacy replication also supports this list.
	bReplicateUsingRegisteredSubObjectList = true;
#endif
}

void UBallisticsLaunchContributorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem =
			World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			Subsystem->RegisterLaunchContributor(this);
		}
	}
	RefreshLauncherBinding();
}

void UBallisticsLaunchContributorComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UProjectileLauncherComponent* Launcher = BoundLauncher.Get())
	{
		Launcher->UnregisterLaunchContributor(this);
	}
	BoundLauncher.Reset();
	CachedAttachParentActor.Reset();
	AttachmentDepth = INDEX_NONE;
	bAttachmentStateCached = false;
	bCachedWithoutLauncher = false;

	if (UWorld* World = GetWorld())
	{
		if (UProjectileBallisticsSubsystem* Subsystem =
			World->GetSubsystem<UProjectileBallisticsSubsystem>())
		{
			Subsystem->UnregisterLaunchContributor(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool UBallisticsLaunchContributorComponent::SetModifier(
	UBallisticsLaunchCosmeticModifier* NewModifier)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || (World->GetNetMode() != NM_Standalone &&
		(!Owner || !Owner->HasAuthority())))
	{
		return false;
	}

	if (Modifier != NewModifier)
	{
		Modifier = NewModifier;
		WarnedInvalidSourceBindings.Reset();
		if (UProjectileLauncherComponent* Launcher = BoundLauncher.Get())
		{
			Launcher->NotifyLaunchContributorChanged(this);
		}
		if (Owner)
		{
			Owner->ForceNetUpdate();
		}
	}
	return true;
}

void UBallisticsLaunchContributorComponent::OnRep_Modifier()
{
	WarnedInvalidSourceBindings.Reset();
	RefreshLauncherBinding();
	if (UProjectileLauncherComponent* Launcher = BoundLauncher.Get())
	{
		Launcher->NotifyLaunchContributorChanged(this);
	}
}

void UBallisticsLaunchContributorComponent::OnRep_SourceOverrides()
{
	WarnedInvalidSourceBindings.Reset();
	RefreshLauncherBinding();
	if (UProjectileLauncherComponent* Launcher = BoundLauncher.Get())
	{
		Launcher->NotifyLaunchContributorChanged(this);
	}
}

void UBallisticsLaunchContributorComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBallisticsLaunchContributorComponent, Modifier);
	DOREPLIFETIME(UBallisticsLaunchContributorComponent, SourceOverrides);
}

void UBallisticsLaunchContributorComponent::RefreshLauncherBinding()
{
	AActor* Owner = GetOwner();
	CachedAttachParentActor = Owner ? Owner->GetAttachParentActor() : nullptr;
	bAttachmentStateCached = true;

	int32 NewAttachmentDepth = INDEX_NONE;
	UProjectileLauncherComponent* NewLauncher =
		FindNearestLauncher(NewAttachmentDepth);
	UProjectileLauncherComponent* PreviousLauncher = BoundLauncher.Get();
	const bool bLauncherChanged = NewLauncher != PreviousLauncher;
	const bool bDepthChanged = NewAttachmentDepth != AttachmentDepth;
	AttachmentDepth = NewAttachmentDepth;
	bCachedWithoutLauncher = NewLauncher == nullptr;

	if (!bLauncherChanged)
	{
		if (bDepthChanged && NewLauncher)
		{
			NewLauncher->NotifyLaunchContributorChanged(this);
		}
		return;
	}

	if (PreviousLauncher)
	{
		PreviousLauncher->UnregisterLaunchContributor(this);
	}
	BoundLauncher = NewLauncher;
	if (NewLauncher)
	{
		NewLauncher->RegisterLaunchContributor(this);
	}
}

void UBallisticsLaunchContributorComponent::PollLauncherBinding()
{
	AActor* Owner = GetOwner();
	AActor* CurrentAttachParent = Owner ? Owner->GetAttachParentActor() : nullptr;
	const bool bParentMatches = bAttachmentStateCached &&
		!CachedAttachParentActor.IsStale() &&
		CurrentAttachParent == CachedAttachParentActor.Get();
	if (bParentMatches && (BoundLauncher.IsValid() || bCachedWithoutLauncher))
	{
		return;
	}
	RefreshLauncherBinding();
}

UProjectileLauncherComponent*
UBallisticsLaunchContributorComponent::FindNearestLauncher(
	int32& OutAttachmentDepth) const
{
	OutAttachmentDepth = INDEX_NONE;
#if WITH_DEV_AUTOMATION_TESTS
	++BindingSearchCount;
#endif
	int32 Depth = 0;
	for (AActor* Actor = GetOwner(); Actor;
		Actor = Actor->GetAttachParentActor(), ++Depth)
	{
		if (UProjectileLauncherComponent* Launcher =
			Actor->FindComponentByClass<UProjectileLauncherComponent>())
		{
			OutAttachmentDepth = Depth;
			return Launcher;
		}
	}
	return nullptr;
}

const FBallisticsLaunchSourceOverride*
UBallisticsLaunchContributorComponent::FindSourceOverride(
	FName BindingName,
	int32& OutPriority) const
{
	OutPriority = 0;
	const int32 InlineOverrideCount = FMath::Min(SourceOverrides.Num(), 16);
	for (int32 OverrideIndex = 0; OverrideIndex < InlineOverrideCount; ++OverrideIndex)
	{
		const FBallisticsLaunchSourceOverride& Override =
			SourceOverrides[OverrideIndex];
		if (Override.BindingName == BindingName)
		{
			OutPriority = Override.bOverridePriority ? Override.Priority : 0;
			return &Override;
		}
	}

	if (!Modifier)
	{
		return nullptr;
	}
	const int32 LegacyOverrideCount = FMath::Min(Modifier->SourceOverrides.Num(), 16);
	for (int32 OverrideIndex = 0; OverrideIndex < LegacyOverrideCount; ++OverrideIndex)
	{
		const FBallisticsLaunchSourceOverride& Override =
			Modifier->SourceOverrides[OverrideIndex];
		if (Override.BindingName == BindingName)
		{
			OutPriority = Override.bOverridePriority
				? Override.Priority
				: (Modifier->bOverridePriority ? Modifier->Priority : 0);
			return &Override;
		}
	}
	return nullptr;
}

USceneComponent* UBallisticsLaunchContributorComponent::FindTaggedSourceComponent(
	FName ComponentTag) const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (ComponentTag.IsNone())
	{
		return Owner->GetRootComponent();
	}

	TArray<USceneComponent*> Components;
	Owner->GetComponents<USceneComponent>(Components);
	USceneComponent* Result = nullptr;
	for (USceneComponent* Component : Components)
	{
		if (Component && Component->ComponentTags.Contains(ComponentTag) &&
			(!Result || Component->GetPathName() < Result->GetPathName()))
		{
			Result = Component;
		}
	}
	return Result;
}

bool UBallisticsLaunchContributorComponent::ResolveSourceOverride(
	FName BindingName,
	USceneComponent*& OutComponent,
	FName& OutSocketName) const
{
	OutComponent = nullptr;
	OutSocketName = NAME_None;
	if (BindingName.IsNone())
	{
		return false;
	}

	int32 IgnoredPriority = 0;
	const FBallisticsLaunchSourceOverride* Override =
		FindSourceOverride(BindingName, IgnoredPriority);
	if (!Override)
	{
		return false;
	}

	OutComponent = FindTaggedSourceComponent(Override->ComponentTag);
	OutSocketName = Override->SocketNameOverride;
	if (!OutComponent && !WarnedInvalidSourceBindings.Contains(BindingName))
	{
		WarnedInvalidSourceBindings.Add(BindingName);
		NPS_LOG(Warning,
			TEXT("NPS contributor %s could not resolve source binding %s with component tag %s."),
			*GetPathName(),
			*BindingName.ToString(),
			*Override->ComponentTag.ToString());
	}
	return true;
}

bool UBallisticsLaunchContributorComponent::GetSourceOverridePriority(
	FName BindingName,
	int32& OutPriority) const
{
	return !BindingName.IsNone() &&
		FindSourceOverride(BindingName, OutPriority) != nullptr;
}
