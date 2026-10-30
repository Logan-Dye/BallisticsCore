// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "TrajectoryVisualizer.h"
#include "BallisticsProjectileDataAsset.h"
#include "ProjectileLauncherComponent.h"
#include "BallisticsTrajectoryLibrary.h"
#include "SceneManagement.h"

void FTrajectoryVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UProjectileLauncherComponent* Launcher = Cast<UProjectileLauncherComponent>(Component);
	if (!Launcher)
	{
		return;
	}

	const UBallisticsProjectileDataAsset* Projectile = Launcher->GetProjectileDataAsset();
	if (!Projectile)
	{
		return;
	}

	FProjectileInitialConditions Conditions = Projectile->BuildInitialConditions();

	if (const AActor* Owner = Launcher->GetOwner())
	{
		Conditions.Origin = Owner->GetActorLocation();
		Conditions.Direction = Owner->GetActorForwardVector();
	}

	const float Duration = 3.f;
	const float Step = 0.02f;
	const int32 NumSteps = FMath::CeilToInt(Duration / Step);
	const FColor TrajectoryColor(255, 165, 0); // Orange

	FVector PrevPos = Conditions.Origin;

	for (int32 i = 1; i <= NumSteps; ++i)
	{
		const float Time = i * Step;
		const FVector Pos = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);

		PDI->DrawLine(PrevPos, Pos, TrajectoryColor, SDPG_World, 2.f);
		PrevPos = Pos;
	}

	DrawWireSphere(PDI, Conditions.Origin, TrajectoryColor, 5.f, 8, SDPG_World);
}
