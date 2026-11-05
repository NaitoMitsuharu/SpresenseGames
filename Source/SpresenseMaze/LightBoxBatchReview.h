#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"
class ALightBoxPawn;

/** Development-only, resumable review of actual provider replies using production rendering. */
class FLightBoxBatchReview
{
public:
    explicit FLightBoxBatchReview(FString InDirectory):Directory(MoveTemp(InDirectory)){}
    void Tick(ALightBoxPawn& Pawn);
private:
    FString Directory,Tag;
    TSet<FString> Processed;
    FLightBoxStyle Scene;
    int32 Shot=0,WaitFrames=0;
    bool bWaiting=false;
    FString ShotPath;
    double LastWork=0,AppliedAt=0;
};
