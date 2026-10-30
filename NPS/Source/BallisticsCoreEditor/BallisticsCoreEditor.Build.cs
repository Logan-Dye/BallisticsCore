// Copyright Persidera Industries LLC. All Rights Reserved.

using UnrealBuildTool;

public class BallisticsCoreEditor : ModuleRules
{
	public BallisticsCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BallisticsCoreRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"PropertyEditor",
			"ComponentVisualizers"
		});
	}
}
