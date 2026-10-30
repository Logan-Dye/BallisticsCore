// Copyright Persidera Industries LLC. All Rights Reserved.

using UnrealBuildTool;

public class BallisticsCoreTests : ModuleRules
{
	public BallisticsCoreTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"BallisticsCoreRuntime",
			"NPSExamples",
			"CQTest",
			"NetCore",
			"Niagara"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd"
			});
		}

		SetupIrisSupport(Target);
	}
}
