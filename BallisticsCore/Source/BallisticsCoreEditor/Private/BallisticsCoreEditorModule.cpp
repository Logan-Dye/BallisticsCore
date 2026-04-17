// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "BallisticsCoreEditorModule.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "ComponentVisualizers.h"

#define LOCTEXT_NAMESPACE "FBallisticsCoreEditorModule"

void FBallisticsCoreEditorModule::StartupModule()
{
	// Register component visualizers here
	// Example:
	// if (GUnrealEd)
	// {
	//     TSharedPtr<FComponentVisualizer> Visualizer = MakeShareable(new FTrajectoryVisualizer);
	//     GUnrealEd->RegisterComponentVisualizer(
	//         UProjectileLauncherComponent::StaticClass()->GetFName(),
	//         Visualizer
	//     );
	// }
}

void FBallisticsCoreEditorModule::ShutdownModule()
{
	// Unregister component visualizers here
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBallisticsCoreEditorModule, BallisticsCoreEditor)
