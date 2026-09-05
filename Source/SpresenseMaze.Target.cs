using UnrealBuildTool;

public class SpresenseMazeTarget : TargetRules
{
	public SpresenseMazeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SpresenseMaze");
	}
}
