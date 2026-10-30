// Copyright Persidera Industries LLC. All Rights Reserved.

using UnrealBuildTool;

public class NPSExamples : ModuleRules
{
	public NPSExamples(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"BallisticsCoreRuntime",
			"PhysicsCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Niagara"
		});
	}
}
