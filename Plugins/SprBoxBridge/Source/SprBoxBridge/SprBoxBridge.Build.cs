using UnrealBuildTool;
using System.IO;

public class SprBoxBridge : ModuleRules
{
	public SprBoxBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("Launch"); // for AndroidJNI / GameActivity access

			// Hook up the UPL file that injects the .aar, manifest entries and Java bridge code
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "SprBoxBridge_UPL.xml"));
		}
	}
}
