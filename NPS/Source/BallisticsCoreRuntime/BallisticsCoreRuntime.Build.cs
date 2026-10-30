// Copyright Persidera Industries LLC. All Rights Reserved.

using UnrealBuildTool;

public class BallisticsCoreRuntime : ModuleRules
{
	public BallisticsCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Let the engine configure Iris dependencies without selecting the project's replication system.
		SetupIrisSupport(Target);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Niagara",
			"NetCore",
			"PhysicsCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore"
		});
	}
}
