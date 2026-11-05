#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

class UCanvas;
class APlayerController;
struct FSlateBrush;

/** Lightweight Canvas/Slate primitives shared by the experience menus. */
namespace ExperienceUI
{
	extern const FLinearColor Paper;
	extern const FLinearColor Muted;
	extern const FLinearColor Mint;
	extern const FLinearColor Coral;
	extern const FLinearColor Ink;
	void RoundRect(UCanvas* Canvas, float X, float Y, float Width, float Height, float Radius, const FLinearColor& Color);
	void Glass(UCanvas* Canvas, float X, float Y, float Width, float Height, float Radius = 24.f, float Opacity = 0.88f);
	void Gradient(UCanvas* Canvas, float X, float Y, float Width, float Height, const FLinearColor& From, const FLinearColor& To, bool Horizontal = false);
	bool Hovered(APlayerController* PC, float X, float Y, float Width, float Height);
	const FSlateBrush* ModalBrush();
	const FButtonStyle* ButtonStyle(bool Primary = false);
	const FEditableTextBoxStyle* FieldStyle();
}
