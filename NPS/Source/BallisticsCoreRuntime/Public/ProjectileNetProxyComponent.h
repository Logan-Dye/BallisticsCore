// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "ProjectileNetProxyComponent.generated.h"

/**
 * Replicates a compact transform for long-lived server-authoritative projectiles.
 *
 * The server simulates the projectile. Clients interpolate the owning actor toward
 * quantized replicated position and rotation targets. This preserves relevancy and
 * late-join behavior without enabling full replicated movement.
 */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileNetProxyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileNetProxyComponent();

	/** Client interpolation speed toward each replicated transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Net Proxy", meta = (ClampMin = "1"))
	float InterpolationSpeed = 12.f;

	/** Corrections larger than this distance in centimeters snap immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Net Proxy", meta = (ClampMin = "0"))
	float SnapDistance = 1000.f;

	/** Stop publishing authoritative transform updates after the projectile is spent. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Net Proxy")
	void StopReplicatingTransform();

	/** Resume publishing after a pooled projectile is reinitialized. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Net Proxy")
	void StartReplicatingTransform();

	UFUNCTION(BlueprintPure, Category = "Ballistics|Net Proxy")
	bool IsPublishingTransform() const { return bPublishing; }

#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	uint64 GetTickInvocationCountForTests() const { return TickInvocationCount; }
	FVector GetPublishedLocationForTests() const { return ProxyLocation; }
	FVector GetTargetLocationForTests() const { return TargetLocation; }
	bool HasReceivedTargetForTests() const { return bHasTarget; }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_ProxyTransform)
	FVector_NetQuantize10 ProxyLocation;

	UPROPERTY(ReplicatedUsing = OnRep_ProxyTransform)
	FRotator ProxyRotation = FRotator::ZeroRotator;

	UFUNCTION()
	void OnRep_ProxyTransform();

	void PublishAuthoritativeTransform(const AActor& Owner);

	FVector TargetLocation = FVector::ZeroVector;
	FRotator TargetRotation = FRotator::ZeroRotator;
	FVector SmoothedLocation = FVector::ZeroVector;
	FRotator SmoothedRotation = FRotator::ZeroRotator;
	bool bHasTarget = false;
	bool bPublishing = true;
#if WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS
	uint64 TickInvocationCount = 0;
#endif
};
