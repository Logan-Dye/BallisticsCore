// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "TrajectoryVisualizer.h"
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

	// Build conditions from the launcher's current transform + base settings
	FProjectileInitialConditions Conditions = Launcher->BaseConditions;

	if (const AActor* Owner = Launcher->GetOwner())
	{
		Conditions.Origin = Owner->GetActorLocation();
		Conditions.Direction = Owner->GetActorForwardVector();
	}

	// Draw trajectory as a series of line segments
	const float Duration = 3.f;
	const float Step = 0.02f;
	const int32 NumSteps = FMath::CeilToInt(Duration / Step);
	const FColor TrajectoryColor(255, 165, 0); // Orange

	FVector PrevPos = Conditions.Origin;

	for (int32 i = 1; i <= NumSteps; i++)
	{
		const float Time = i * Step;
		const FVector Pos = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);

		PDI->DrawLine(PrevPos, Pos, TrajectoryColor, SDPG_World, 1.5f);
		PrevPos = Pos;
	}
}
