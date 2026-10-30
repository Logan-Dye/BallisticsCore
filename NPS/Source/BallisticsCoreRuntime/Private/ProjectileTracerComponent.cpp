// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "ProjectileTracerComponent.h"
#include "ProjectileBallisticsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

UProjectileTracerComponent::UProjectileTracerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // Tick after ballistics updates position

	BaseScale = FVector(0.1f, 0.1f, 1.f);
	VelocityStretchFactor = 0.00005f;
	MaxStretchMultiplier = 10.f;
	TracerColor = FLinearColor(1.f, 0.8f, 0.2f, 1.f); // Warm yellow
	EmissiveIntensity = 5.f;

	MinVisibleDistance = 200.f;
	MaxVisibleDistance = 50000.f;
	FadeDistance = 500.f;

	FadeOutDuration = 0.3f;
	bDestroyAfterFadeOut = true;

	bIsFadingOut = false;
	FadeOutTimer = 0.f;
}

void UProjectileTracerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		BallisticsComponent = Owner->FindComponentByClass<UProjectileBallisticsComponent>();

		if (BallisticsComponent.IsValid())
		{
			BallisticsComponent->OnStateChanged.AddDynamic(this, &UProjectileTracerComponent::OnBallisticsStateChanged);
		}
	}

	SetupMeshComponent();
}

void UProjectileTracerComponent::SetupMeshComponent()
{
	if (!TracerMesh || !GetOwner())
	{
		return;
	}

	MeshComponent = NewObject<UStaticMeshComponent>(GetOwner());
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->SetStaticMesh(TracerMesh);
	MeshComponent->SetWorldScale3D(BaseScale);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);
	MeshComponent->RegisterComponent();
	MeshComponent->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	if (MeshComponent->GetNumMaterials() > 0)
	{
		DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(0);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(FName("Color"), TracerColor);
			DynamicMaterial->SetVectorParameterValue(FName("EmissiveColor"), TracerColor * EmissiveIntensity);
		}
	}
}

void UProjectileTracerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsFadingOut)
	{
		UpdateFadeOut(DeltaTime);
		return;
	}

	if (!BallisticsComponent.IsValid())
	{
		return;
	}

	const FProjectileSnapshot Snap = BallisticsComponent->GetCurrentSnapshot();
	UpdateStretch(Snap.Speed);
	UpdateDistanceVisibility();
}

void UProjectileTracerComponent::UpdateStretch(float Speed)
{
	if (!MeshComponent)
	{
		return;
	}

	float StretchMultiplier = 1.f + (Speed * VelocityStretchFactor);
	StretchMultiplier = FMath::Min(StretchMultiplier, MaxStretchMultiplier);

	FVector Scale = BaseScale;
	Scale.X *= StretchMultiplier;
	MeshComponent->SetWorldScale3D(Scale);
}

void UProjectileTracerComponent::UpdateDistanceVisibility()
{
	if (!MeshComponent || !DynamicMaterial)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Get distance to local player camera
	float Opacity = 1.f;
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

		const float Distance = FVector::Dist(GetOwner()->GetActorLocation(), CameraLocation);

		if (Distance < MinVisibleDistance)
		{
			Opacity = 0.f;
		}
		else if (Distance < MinVisibleDistance + FadeDistance)
		{
			Opacity = (Distance - MinVisibleDistance) / FMath::Max(FadeDistance, 1.f);
		}
		else if (Distance > MaxVisibleDistance - FadeDistance)
		{
			Opacity = FMath::Max(0.f, (MaxVisibleDistance - Distance) / FMath::Max(FadeDistance, 1.f));
		}
	}

	DynamicMaterial->SetScalarParameterValue(FName("Opacity"), Opacity);
}

void UProjectileTracerComponent::OnBallisticsStateChanged(EProjectileState OldState, EProjectileState NewState)
{
	if (NewState == EProjectileState::Embedded || NewState == EProjectileState::Destroyed)
	{
		bIsFadingOut = true;
		FadeOutTimer = 0.f;
	}
}

void UProjectileTracerComponent::UpdateFadeOut(float DeltaTime)
{
	FadeOutTimer += DeltaTime;
	const float Alpha = FMath::Clamp(1.f - (FadeOutTimer / FMath::Max(FadeOutDuration, 0.01f)), 0.f, 1.f);

	if (DynamicMaterial)
	{
		DynamicMaterial->SetScalarParameterValue(FName("Opacity"), Alpha);
	}

	if (MeshComponent)
	{
		MeshComponent->SetWorldScale3D(BaseScale * Alpha);
	}

	if (FadeOutTimer >= FadeOutDuration && bDestroyAfterFadeOut)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->Destroy();
		}
	}
}
