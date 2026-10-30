// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallisticsCosmeticTypes.h"
#include "Components/ActorComponent.h"
#include "BallisticsLaunchContributorComponent.generated.h"

class UBallisticsLaunchCosmeticModifier;
class UProjectileLauncherComponent;
class USceneComponent;

/** Supplies launch sources and an optional cosmetic modifier from an attached actor. */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UBallisticsLaunchContributorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBallisticsLaunchContributorComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Modifier,
		Category = "Ballistics|Cosmetics")
	TObjectPtr<UBallisticsLaunchCosmeticModifier> Modifier;

	/** Inline source bindings owned by this attachment. No modifier asset is required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_SourceOverrides,
		Category = "Ballistics|Sources", meta = (TitleProperty = "BindingName"))
	TArray<FBallisticsLaunchSourceOverride> SourceOverrides;

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	bool SetModifier(UBallisticsLaunchCosmeticModifier* NewModifier);

	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	UBallisticsLaunchCosmeticModifier* GetModifier() const { return Modifier; }

	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	UProjectileLauncherComponent* GetBoundLauncher() const { return BoundLauncher.Get(); }

	/** Immediately re-evaluate the owning actor's attachment ancestry. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	void RefreshLauncherBinding();

	/** Cheap attachment-parent poll used by the world aggregate. */
	void PollLauncherBinding();

	int32 GetAttachmentDepth() const { return AttachmentDepth; }

	bool ResolveSourceOverride(
		FName BindingName,
		USceneComponent*& OutComponent,
		FName& OutSocketName) const;
	bool GetSourceOverridePriority(FName BindingName, int32& OutPriority) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Modifier();

	UFUNCTION()
	void OnRep_SourceOverrides();

#if WITH_DEV_AUTOMATION_TESTS
public:
	int32 GetBindingSearchCountForTests() const { return BindingSearchCount; }
#endif

private:
	TWeakObjectPtr<UProjectileLauncherComponent> BoundLauncher;
	TWeakObjectPtr<AActor> CachedAttachParentActor;
	int32 AttachmentDepth = INDEX_NONE;
	bool bAttachmentStateCached = false;
	bool bCachedWithoutLauncher = false;
	mutable TSet<FName> WarnedInvalidSourceBindings;

	UProjectileLauncherComponent* FindNearestLauncher(int32& OutAttachmentDepth) const;
	const FBallisticsLaunchSourceOverride* FindSourceOverride(
		FName BindingName,
		int32& OutPriority) const;
	USceneComponent* FindTaggedSourceComponent(FName ComponentTag) const;

#if WITH_DEV_AUTOMATION_TESTS
	mutable int32 BindingSearchCount = 0;
#endif
};
