#include "ExperienceUI.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace ExperienceUI
{
	const FLinearColor Paper(0.95f, 0.98f, 1.f);
	const FLinearColor Muted(0.52f, 0.64f, 0.69f);
	const FLinearColor Mint(0.35f, 0.94f, 0.75f);
	const FLinearColor Coral(1.f, 0.32f, 0.38f);
	const FLinearColor Ink(0.012f, 0.023f, 0.034f);

	void RoundRect(UCanvas* Canvas, float X, float Y, float Width, float Height, float Radius, const FLinearColor& Color)
	{
		if (!Canvas || !Canvas->DefaultTexture || Width <= 0 || Height <= 0 || Color.A <= 0) { return; }
		Radius = FMath::Clamp(Radius, 0.f, FMath::Min(Width, Height) * 0.5f);
		const FVector2D Centers[] = { {X+Width-Radius,Y+Radius}, {X+Width-Radius,Y+Height-Radius}, {X+Radius,Y+Height-Radius}, {X+Radius,Y+Radius} };
		TArray<FVector2D, TInlineAllocator<36>> Points;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			for (int32 Step = 0; Step <= 8; ++Step)
			{
				const float A = FMath::DegreesToRadians(-90.f + Corner * 90.f + Step * 11.25f);
				Points.Add(Centers[Corner] + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
		}
		TArray<FCanvasUVTri> Triangles;
		Triangles.Reserve(Points.Num());
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			FCanvasUVTri T;
			T.V0_Pos = FVector2D(X + Width * 0.5f, Y + Height * 0.5f); T.V1_Pos = Points[I]; T.V2_Pos = Points[(I + 1) % Points.Num()];
			T.V0_UV = T.V1_UV = T.V2_UV = FVector2D(0.5, 0.5);
			T.V0_Color = T.V1_Color = T.V2_Color = Color; Triangles.Add(T);
		}
		FCanvasTriangleItem Item(Triangles, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Item);
	}

	void Glass(UCanvas* Canvas, float X, float Y, float Width, float Height, float Radius, float Opacity)
	{
		RoundRect(Canvas, X, Y + 8, Width, Height, Radius, FLinearColor(0, 0, 0, 0.17f));
		RoundRect(Canvas, X, Y, Width, Height, Radius, Paper.CopyWithNewOpacity(0.14f));
		RoundRect(Canvas, X + 1, Y + 1, Width - 2, Height - 2, FMath::Max(0.f, Radius - 1), Ink.CopyWithNewOpacity(Opacity));
	}

	void Gradient(UCanvas* Canvas, float X, float Y, float Width, float Height, const FLinearColor& From, const FLinearColor& To, bool Horizontal)
	{
		if (!Canvas || !Canvas->DefaultTexture) { return; }
		FCanvasUVTri A, B;
		A.V0_Pos = FVector2D(X, Y); A.V1_Pos = FVector2D(X + Width, Y); A.V2_Pos = FVector2D(X + Width, Y + Height);
		B.V0_Pos = FVector2D(X, Y); B.V1_Pos = FVector2D(X + Width, Y + Height); B.V2_Pos = FVector2D(X, Y + Height);
		A.V0_Color = From; A.V1_Color = Horizontal ? To : From; A.V2_Color = To;
		B.V0_Color = From; B.V1_Color = To; B.V2_Color = Horizontal ? From : To;
		A.V0_UV = A.V1_UV = A.V2_UV = B.V0_UV = B.V1_UV = B.V2_UV = FVector2D(0.5, 0.5);
		TArray<FCanvasUVTri> Triangles; Triangles.Add(A); Triangles.Add(B);
		FCanvasTriangleItem Item(Triangles, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Item);
	}

	bool Hovered(APlayerController* PC, float X, float Y, float Width, float Height)
	{
		float MX = -1, MY = -1;
		return PC && PC->GetMousePosition(MX, MY) && MX >= X && MX <= X + Width && MY >= Y && MY <= Y + Height;
	}

	const FSlateBrush* ModalBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Paper, 24.f);
		return &Brush;
	}

	const FButtonStyle* ButtonStyle(bool Primary)
	{
		static const FButtonStyle Secondary = FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.065f, 0.1f, 0.12f), 24.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.1f, 0.19f, 0.2f), 24.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.15f, 0.14f), 24.f))
			.SetNormalForeground(Paper).SetHoveredForeground(Paper).SetPressedForeground(Mint);
		static const FButtonStyle Main = FButtonStyle(Secondary)
			.SetNormal(FSlateRoundedBoxBrush(Paper, 24.f)).SetHovered(FSlateRoundedBoxBrush(Mint, 24.f))
			.SetPressed(FSlateRoundedBoxBrush(Mint * 0.8f, 24.f))
			.SetNormalForeground(Ink).SetHoveredForeground(Ink).SetPressedForeground(Ink);
		return Primary ? &Main : &Secondary;
	}

	const FEditableTextBoxStyle* FieldStyle()
	{
		static const FEditableTextBoxStyle Style = FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
			.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0.006f, 0.014f, 0.022f), 12.f, Paper.CopyWithNewOpacity(0.12f), 1.f))
			.SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(0.008f, 0.02f, 0.028f), 12.f, Paper.CopyWithNewOpacity(0.3f), 1.f))
			.SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(0.008f, 0.02f, 0.028f), 12.f, Mint.CopyWithNewOpacity(0.7f), 1.f))
			.SetForegroundColor(Paper).SetPadding(FMargin(16, 12));
		return &Style;
	}
}
