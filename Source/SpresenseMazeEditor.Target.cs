using UnrealBuildTool;

public class SpresenseMazeEditorTarget : TargetRules
{
	public SpresenseMazeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SpresenseMaze");
	}
}
