// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "ProjectileTracerComponent.generated.h"

class UProjectileBallisticsComponent;
class UStaticMeshComponent;
class UParticleSystemComponent;
class UNiagaraComponent;

/**
 * Visual tracer component. Attach alongside UProjectileBallisticsComponent
 * on your projectile actor. Handles:
 *   - Mesh scaling based on velocity (stretch tracer at speed)
 *   - Distance-based visibility (tracers visible at range)
 *   - Trail particle / Niagara ribbon hookup
 *   - Fade-out on embed/destroy
 *
 * Decoupled from physics — swap this out or don't use it at all,
 * the ballistics component doesn't care.
 */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileTracerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectileTracerComponent();

	// ─── Configuration ───────────────────────────

	/** Static mesh to use as the tracer body. Stretched along velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	TObjectPtr<UStaticMesh> TracerMesh;

	/** Base scale of the tracer mesh (before velocity stretch) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	FVector BaseScale;

	/** How much to stretch the mesh along the velocity axis per cm/s of speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "0"))
	float VelocityStretchFactor;

	/** Max stretch multiplier to prevent absurd scaling at extreme speeds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "1"))
	float MaxStretchMultiplier;

	/** Tracer color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer")
	FLinearColor TracerColor;

	/** Tracer emissive intensity (for glowing tracer rounds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer", meta = (ClampMin = "0"))
	float EmissiveIntensity;

	// ─── Distance Visibility ─────────────────────

	/** Below this distance from camera, tracer is invisible (too close to see) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float MinVisibleDistance;

	/** Above this distance, tracer fades out */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float MaxVisibleDistance;

	/** Distance over which the fade-in/fade-out occurs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Tracer|Visibility", meta = (ClampMin = "0"))
	float FadeDistance;

	// ─── Lifetime ────────────────────────────────

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

	/** The mesh component we create at runtime */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Dynamic material instance for color/emissive/opacity control */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	/** Are we in the fade-out phase? */
	bool bIsFadingOut;
	float FadeOutTimer;

	/** Create and configure the mesh component */
	void SetupMeshComponent();

	/** Update mesh scale based on current velocity */
	void UpdateStretch(float Speed);

	/** Update opacity based on distance from camera */
	void UpdateDistanceVisibility();

	/** Handle fade-out after projectile stops */
	void UpdateFadeOut(float DeltaTime);

	/** Called when the ballistics component changes state */
	UFUNCTION()
	void OnBallisticsStateChanged(EProjectileState OldState, EProjectileState NewState);
};
