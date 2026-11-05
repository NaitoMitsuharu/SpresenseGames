#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "FlightModels.h"
#include "GameHudBase.h"
#include "TitleGameMode.generated.h"

class UCameraComponent;
class ULightBoxPointCloud;

/** Experience-select screen shown at launch. */
UCLASS()
class SPRESENSEMAZE_API ATitleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATitleGameMode();

	static const FName TitleMapName;
	static const FName MazeMapName;
	static const FName FlightMapName;
	static const FName LightBoxMapName;
};

/** Flight game: ship pawn, flight HUD; the course is spawned by the pawn. */
UCLASS()
class SPRESENSEMAZE_API AFlightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFlightGameMode();
};

/** Backdrop for the title: the flight ship idling in the alien sky. */
UCLASS()
class SPRESENSEMAZE_API ATitlePawn : public APawn
{
	GENERATED_BODY()

public:
	ATitlePawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ShipPivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> MazePreview;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LightBoxPreview;

private:
	FFlightShipParts Parts;
	UPROPERTY() TObjectPtr<ULightBoxPointCloud> LightPoints;
};

UCLASS()
class SPRESENSEMAZE_API ATitleHUD : public AGameHudBase
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

private:
	void DrawCard(FName Id, const FString& Name, float X, float Y, float CW, float CH, int32 Icon);
	void SetPreview(FName Id);
	void DrawMazeIcon(float CX, float CY, float Size);
	void DrawFlightIcon(float CX, float CY, float Size);
	void DrawLightBoxIcon(float CX, float CY, float Size);
	FName SelectedId;
};
