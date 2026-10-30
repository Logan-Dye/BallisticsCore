// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "ProjectileTracerComponent.generated.h"

class UProjectileBallisticsComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/** Velocity-stretched tracer rendering with distance fading and state-driven shutdown. */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileTracerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileTracerComponent();

	/** Static mesh to use as the tracer body. Stretched along velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	TObjectPtr<UStaticMesh> TracerMesh;

	/** Base scale of the tracer mesh (before velocity stretch) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	FVector BaseScale;

	/** How much to stretch the mesh along the velocity axis per cm/s of speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "0"))
	float VelocityStretchFactor;

	/** Maximum stretch multiplier at high projectile speeds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "1"))
	float MaxStretchMultiplier;

	/** Tracer color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	FLinearColor TracerColor;

	/** Tracer emissive intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "0"))
	float EmissiveIntensity;

	/** Tracer visibility starts at this camera distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float MinVisibleDistance;

	/** Tracer visibility ends at this camera distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float MaxVisibleDistance;

	/** Distance over which the fade-in/fade-out occurs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float FadeDistance;

	/** Time (seconds) for the tracer to fade out after the projectile stops */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "0"))
	float FadeOutDuration;

	/** If true, destroy the tracer actor after fade-out completes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	bool bDestroyAfterFadeOut;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Reference to the sibling ballistics component */
	UPROPERTY()
	TWeakObjectPtr<UProjectileBallisticsComponent> BallisticsComponent;

	/** Runtime tracer mesh. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Dynamic material instance for color/emissive/opacity control */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	/** Fade state. */
	bool bIsFadingOut;
	float FadeOutTimer;

	void SetupMeshComponent();

	void UpdateStretch(float Speed);

	void UpdateDistanceVisibility();

	void UpdateFadeOut(float DeltaTime);

	UFUNCTION()
	void OnBallisticsStateChanged(EProjectileState OldState, EProjectileState NewState);
};
