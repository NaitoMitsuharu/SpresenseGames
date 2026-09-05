#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MazeSaveGame.generated.h"

/** One stage's ranked clear times. UHT doesn't support TArray<TArray<>>, so
 *  this wraps the inner list for UMazeSaveGame::StageBestTimes. */
USTRUCT(BlueprintType)
struct FMazeStageTimeList
{
	GENERATED_BODY()

	/** Up to UMazeSaveGame::MaxRankedTimes clear times in seconds, ascending. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranking")
	TArray<float> Times;
};

/**
 * Persisted per-stage best times (top 5 per stage, ascending = fastest first).
 * Only written to disk after a true all-stages clear; going home mid-run never
 * touches this (see AMazeGenerator::HandleGoalOverlap).
 */
UCLASS()
class SPRESENSEMAZE_API UMazeSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxRankedTimes = 5;

	/** Outer index = stage index. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranking")
	TArray<FMazeStageTimeList> StageBestTimes;

	/**
	 * Inserts Time into stage StageIndex's ranked list (growing the outer
	 * array as needed), sorts ascending, and trims to MaxRankedTimes.
	 * @return true if Time entered the top MaxRankedTimes (i.e. is a new record).
	 */
	bool SubmitStageTime(int32 StageIndex, float Time);
};
