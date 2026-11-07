#include "MazeGameMode.h"
#include "MazeBallPawn.h"
#include "MazeHUD.h"

AMazeGameMode::AMazeGameMode()
{
	DefaultPawnClass = AMazeBallPawn::StaticClass();
	HUDClass = AMazeHUD::StaticClass();
	PlayerControllerClass = AMazePlayerController::StaticClass();
}
