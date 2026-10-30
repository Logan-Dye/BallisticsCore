// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

/**
 * Draws a trajectory preview line in the editor viewport when a
 * UProjectileLauncherComponent is selected. Uses EvaluatePositionAtTime
 * from the trajectory library to compute the path.
 */
class FTrajectoryVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};
