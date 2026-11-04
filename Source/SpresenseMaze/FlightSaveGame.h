#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FlightSaveGame.generated.h"

UCLASS()
class SPRESENSEMAZE_API UFlightSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 ScoreVersion = 1;
	UPROPERTY() int32 BestScore = 0;
	UPROPERTY() FString BestGrade;
};
