#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MazeGameMode.generated.h"

/** Keep HUD touch/click handling alive while physics and stage time are paused. */
UCLASS()
class SPRESENSEMAZE_API AMazePlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AMazePlayerController() { bShouldPerformFullTickWhenPaused = true; }
};

UCLASS()
class SPRESENSEMAZE_API AMazeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMazeGameMode();
};
