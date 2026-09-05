#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/HUD.h"
#include "MazeHUD.generated.h"

class AMazeGenerator;

/**
 * Canvas HUD: top-right minimap (walls/hurdles/holes/goal/ball),
 * stage name top-left, goal-direction arrow around the ball,
 * and a big celebration message during stage transitions.
 */
UCLASS()
class SPRESENSEMAZE_API AMazeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	/** Minimap width in pixels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float MinimapSize = 260.f;

	/** Screen margin from the top-right corner */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float MinimapMargin = 20.f;

private:
	/**
	 * Japanese-capable font. GEngine->GetLargeFont() is an offline Roboto bitmap
	 * with Latin glyphs only; the default *Slate* font family has the engine's
	 * DroidSansFallback registered as its fallback typeface, which is where the
	 * kana and kanji come from. Everything on this HUD draws through JText.
	 */
	FSlateFontInfo UIFont(int32 Size) const;
	void JText(const FString& Text, const FLinearColor& Color, float X, float Y, int32 Size);
	FVector2D JSize(const FString& Text, int32 Size) const;

	/** Wordless pictogram for a tutorial lesson, drawn inside a Size x Size box */
	void DrawLessonIcon(int32 Kind, float CX, float CY, float Size);

	AMazeGenerator* FindMaze();
	void DrawGlassPanel(float X, float Y, float W, float H);
	void DrawStatusDot(float X, float Y, bool bOn);
	void DrawMinimap(AMazeGenerator* Maze);
	void DrawAttitudeIndicator();
	void DrawOrientationWarning();
	void DrawButton(FName Id, const FString& Label, float X, float Y, float W, float H, bool bPrimary);
	void DrawHomeScreen(AMazeGenerator* Maze);
	void DrawClearChoice(AMazeGenerator* Maze);
	void DrawAllClearRanking(AMazeGenerator* Maze);
	void DrawCalibrationOverlay();
	void DrawTutorialHint(AMazeGenerator* Maze);
	void DrawGoalArrow(AMazeGenerator* Maze);
	void DrawJumpFeedback();
	void DrawMessages(AMazeGenerator* Maze);
	void DrawTouchController();

	TWeakObjectPtr<AMazeGenerator> CachedMaze;
};
