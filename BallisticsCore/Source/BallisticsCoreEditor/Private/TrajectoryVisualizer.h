// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizers.h"

/**
 * Draws a trajectory preview line in the editor viewport when a
 * UProjectileLauncherComponent is selected. Uses PredictTrajectory
 * from the subsystem to compute the path.
 *
 * TODO: Implement DrawVisualization override to:
 *  1. Get the launcher's BaseConditions
 *  2. Call UBallisticsTrajectoryLibrary::EvaluatePositionAtTime in a loop
 *  3. Draw the path as a dashed line using PDI->DrawDashedLine
 *  4. Optionally draw velocity vectors at intervals
 */
class FTrajectoryVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};
