#pragma once

#include "CoreMinimal.h"
#include "GameHudBase.h"
#include "FlightHUD.generated.h"

class AFlightPawn;
class AFlightCourse;

/**
 * Flight HUD: shield, line-of-fire reticles, ring count, input source and a
 * compact attitude readout, plus the start / pause / shot-down / clear panels.
 */
UCLASS()
class SPRESENSEMAZE_API AFlightHUD : public AGameHudBase
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

private:
	void DrawShield(AFlightPawn* Ship);
	void DrawRings(AFlightCourse* Course);
	void DrawInputChip();
	void DrawAttitude();
	void DrawReticles(AFlightPawn* Ship);
	void DrawBoss(AFlightCourse* Course);
	void DrawBanner(AFlightPawn* Ship);
	void DrawDamageEdges(AFlightPawn* Ship);
	void DrawAwaitNeutral(AFlightPawn* Ship);
	void DrawCountdown(AFlightPawn* Ship);
	void DrawPaused();
	void DrawDestroyed();
	void DrawCleared(AFlightCourse* Course);
	void Dim(float Alpha);
};
