#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/HUD.h"
#include "GameHudBase.generated.h"

/**
 * Canvas drawing helpers shared by the title and flight HUDs. Uses the same
 * Japanese-capable text path as AMazeHUD (Slate default font backed by the
 * engine UFont so the DroidSansFallback CJK face is used on Android), plus a
 * resolution scale so layouts authored for 1080p stay physically similar on
 * the Xperia 1 VI and the PDT-FP1.
 */
UCLASS(Abstract)
class SPRESENSEMAZE_API AGameHudBase : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	/** 1.0 at 1080px tall; everything below is laid out in these units. */
	float S = 1.f;
	float W = 0.f;
	float H = 0.f;

	/** Left inset that keeps clear of the Xperia Game Enhancer floating control. */
	float SafeLeft() const;

	FVector2D TextSize(const FString& Text, float Size) const;
	void Text(const FString& Text, const FLinearColor& Color, float X, float Y, float Size, bool bShadow = true);
	void TextCentered(const FString& Text, const FLinearColor& Color, float CX, float Y, float Size);

	void Panel(float X, float Y, float PW, float PH, float Alpha = 0.72f);
	void Button(FName Id, const FString& Label, float X, float Y, float BW, float BH, bool bPrimary);
	void Dot(float X, float Y, float R, const FLinearColor& Color);
	void Ring(float CX, float CY, float R, const FLinearColor& Color, float Thickness, int32 Segments = 32);
	void Arc(float CX, float CY, float R, float FromDeg, float ToDeg, const FLinearColor& Color, float Thickness);

	static const FLinearColor Accent;
	static const FLinearColor Cyan;
	static const FLinearColor PanelColor;
	static const FLinearColor PanelEdge;
};
