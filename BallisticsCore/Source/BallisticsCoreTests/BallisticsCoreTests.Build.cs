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
			"BallisticsCoreRuntime"
		});
	}
}
