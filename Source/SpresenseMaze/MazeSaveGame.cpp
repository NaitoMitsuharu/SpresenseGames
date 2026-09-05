#include "MazeSaveGame.h"

bool UMazeSaveGame::SubmitStageTime(int32 StageIndex, float Time)
{
	if (StageIndex < 0)
	{
		return false;
	}
	while (StageBestTimes.Num() <= StageIndex)
	{
		StageBestTimes.AddDefaulted();
	}

	TArray<float>& List = StageBestTimes[StageIndex].Times;
	const bool bEntersTop = List.Num() < MaxRankedTimes || Time < List.Last();

	List.Add(Time);
	List.Sort();
	if (List.Num() > MaxRankedTimes)
	{
		List.SetNum(MaxRankedTimes);
	}
	return bEntersTop;
}
