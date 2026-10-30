// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileNetProxyComponent.h"

#include "GameFramework/Actor.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

UProjectileNetProxyComponent::UProjectileNetProxyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
#if UE_WITH_IRIS
	// Iris uses registered subobjects; legacy replication also supports this list.
	bReplicateUsingRegisteredSubObjectList = true;
#endif
}

void UProjectileNetProxyComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.RepNotifyCondition = REPNOTIFY_OnChanged;
	DOREPLIFETIME_WITH_PARAMS_FAST(
		UProjectileNetProxyComponent, ProxyLocation, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(
		UProjectileNetProxyComponent, ProxyRotation, Params);
}

void UProjectileNetProxyComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(true);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Disable standard movement replication to preserve client interpolation.
	Owner->SetReplicateMovement(false);

	if (Owner->HasAuthority())
	{
		PublishAuthoritativeTransform(*Owner);
	}
	else
	{
		TargetLocation = Owner->GetActorLocation();
		TargetRotation = Owner->GetActorRotation();
		SmoothedLocation = TargetLocation;
		SmoothedRotation = TargetRotation;
	}
}

void UProjectileNetProxyComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	++TickInvocationCount;
#endif

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (bPublishing)
		{
			PublishAuthoritativeTransform(*Owner);
		}
		return;
	}

	if (!bHasTarget)
	{
		return;
	}

	SmoothedLocation = FMath::VInterpTo(
		SmoothedLocation,
		TargetLocation,
		DeltaTime,
		InterpolationSpeed);
	SmoothedRotation = FMath::RInterpTo(
		SmoothedRotation,
		TargetRotation,
		DeltaTime,
		InterpolationSpeed);
	Owner->SetActorLocationAndRotation(SmoothedLocation, SmoothedRotation);
}

void UProjectileNetProxyComponent::OnRep_ProxyTransform()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TargetLocation = ProxyLocation;
	TargetRotation = ProxyRotation;

	const bool bSnap = !bHasTarget ||
		FVector::DistSquared(Owner->GetActorLocation(), TargetLocation) >
			FMath::Square(SnapDistance);
	bHasTarget = true;

	if (bSnap)
	{
		SmoothedLocation = TargetLocation;
		SmoothedRotation = TargetRotation;
		Owner->SetActorLocationAndRotation(TargetLocation, TargetRotation);
	}
}

void UProjectileNetProxyComponent::StopReplicatingTransform()
{
	bPublishing = false;
}

void UProjectileNetProxyComponent::StartReplicatingTransform()
{
	bPublishing = true;

	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		PublishAuthoritativeTransform(*Owner);
		Owner->ForceNetUpdate();
	}
}

void UProjectileNetProxyComponent::PublishAuthoritativeTransform(
	const AActor& Owner)
{
	const FVector_NetQuantize10 NewLocation = Owner.GetActorLocation();
	if (ProxyLocation != NewLocation)
	{
		ProxyLocation = NewLocation;
		MARK_PROPERTY_DIRTY_FROM_NAME(
			UProjectileNetProxyComponent, ProxyLocation, this);
	}

	const FRotator NewRotation = Owner.GetActorRotation();
	if (!ProxyRotation.Equals(NewRotation))
	{
		ProxyRotation = NewRotation;
		MARK_PROPERTY_DIRTY_FROM_NAME(
			UProjectileNetProxyComponent, ProxyRotation, this);
	}
}
