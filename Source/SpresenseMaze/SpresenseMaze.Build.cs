using UnrealBuildTool;

public class SpresenseMaze : ModuleRules
{
	public SpresenseMaze(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent",
			"SprBoxBridge"
		});
	}
}
