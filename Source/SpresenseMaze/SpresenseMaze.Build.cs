using UnrealBuildTool;
using System.IO;

public class SpresenseMaze : ModuleRules
{
	public SpresenseMaze(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Keep file-local scene helpers isolated (unity builds merge anonymous namespaces).
		bUseUnity = false;

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

		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "Json", "RenderCore", "RHI" });
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("Launch");
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "Android", "LightBox_UPL.xml"));
		}
	}
}
