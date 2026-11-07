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
	/** True over the pause or dodge buttons, so a tap there never moves the slide aim. */
	bool IsOverControl(const FVector2D& Point) const;

private:
	void DrawShield(AFlightPawn* Ship);
	void DrawRings(AFlightCourse* Course);
	void DrawScore(AFlightCourse* Course);
	void DrawDodge(AFlightPawn* Ship);
	void DrawThreats(AFlightPawn* Ship);
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
	void RoundRect(float X, float Y, float Width, float Height, float Radius, const FLinearColor& Color);
	void Label(const FString& Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold = false);
	FVector2D Measure(const FString& Value, float Size, bool bBold = false) const;
	void CenterLabel(const FString& Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold = false);
	void PillButton(FName Id, const FString& Value, float X, float Y, float Width, float Height, bool bPrimary = false);
	void Modal(float X, float Y, float Width, float Height);
	void FadeEdges();
	void CyberPanel(float X, float Y, float Width, float Height, const FLinearColor& AccentColor, float Alpha = 1.f);
	void DrawCyberMotion(AFlightPawn* Ship, AFlightCourse* Course);
	float ShownShield = 100.f;
	float ShownScore = 0.f;
	float UiTime = 0.f;
	bool bUiInitialised = false;
	static const FLinearColor Paper;
	static const FLinearColor Muted;
	static const FLinearColor Mint;
	static const FLinearColor Coral;
	static const FLinearColor Ink;
};
