#include "GameHudBase.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

const FLinearColor AGameHudBase::Accent(1.f, 0.58f, 0.1f);
const FLinearColor AGameHudBase::Cyan(0.25f, 0.88f, 1.f);
const FLinearColor AGameHudBase::PanelColor(0.015f, 0.025f, 0.045f, 0.72f);
const FLinearColor AGameHudBase::PanelEdge(0.62f, 0.75f, 0.88f, 0.2f);

void AGameHudBase::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas)
	{
		W = Canvas->SizeX;
		H = Canvas->SizeY;
		S = FMath::Clamp(H / 1080.f, 0.6f, 1.6f);
	}
}

float AGameHudBase::SafeLeft() const
{
	return FMath::Clamp(W * 0.08f, 20.f, 190.f);
}

FVector2D AGameHudBase::TextSize(const FString& InText, float Size) const
{
	const int32 Px = FMath::Max(1, FMath::RoundToInt(Size * S));
	if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
	{
		return FVector2D(InText.Len() * Px * 0.55f, Px);
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FVector2D(Measure->Measure(InText, FCoreStyle::GetDefaultFontStyle("Bold", Px)));
}

void AGameHudBase::Text(const FString& InText, const FLinearColor& Color, float X, float Y, float Size, bool bShadow)
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Canvas || !Font)
	{
		return;
	}
	// Request the final pixel size so large headings stay sharp (see AMazeHUD::JText).
	const FSlateFontInfo ExactSizeFont(Font, static_cast<float>(FMath::Max(1, FMath::RoundToInt(Size * S))));
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(InText), ExactSizeFont, Color);
	if (bShadow)
	{
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.55f * Color.A), FVector2D(2.f, 2.f));
	}
	Canvas->DrawItem(Item);
}

void AGameHudBase::TextCentered(const FString& InText, const FLinearColor& Color, float CX, float Y, float Size)
{
	const FVector2D TS = TextSize(InText, Size);
	Text(InText, Color, CX - TS.X * 0.5f, Y, Size);
}

void AGameHudBase::Panel(float X, float Y, float PW, float PH, float Alpha)
{
	DrawRect(PanelColor.CopyWithNewOpacity(Alpha), X, Y, PW, PH);
	DrawRect(PanelEdge, X, Y, PW, 1.f);
	DrawRect(PanelEdge, X, Y + PH - 1.f, PW, 1.f);
	DrawRect(PanelEdge, X, Y, 1.f, PH);
	DrawRect(PanelEdge, X + PW - 1.f, Y, 1.f, PH);
	DrawRect(Accent.CopyWithNewOpacity(0.82f), X, Y, 2.f, PH);
}

void AGameHudBase::Button(FName Id, const FString& Label, float X, float Y, float BW, float BH, bool bPrimary)
{
	if (bPrimary)
	{
		DrawRect(Accent.CopyWithNewOpacity(0.92f), X, Y, BW, BH);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.25f), X, Y, BW, 2.f);
	}
	else
	{
		Panel(X, Y, BW, BH);
	}
	const float Size = BH / S > 56.f ? 28.f : 21.f;
	const FVector2D TS = TextSize(Label, Size);
	Text(Label, bPrimary ? FLinearColor(0.08f, 0.06f, 0.02f) : FLinearColor::White,
		X + (BW - TS.X) * 0.5f, Y + (BH - TS.Y) * 0.5f, Size, !bPrimary);
	AddHitBox(FVector2D(X, Y), FVector2D(BW, BH), Id, true);
}

void AGameHudBase::Dot(float X, float Y, float R, const FLinearColor& Color)
{
	FLinearColor Glow = Color;
	Glow.A = Color.A * 0.3f;
	DrawRect(Glow, X - R * 1.6f, Y - R * 1.6f, R * 3.2f, R * 3.2f);
	DrawRect(Color, X - R, Y - R, R * 2.f, R * 2.f);
}

void AGameHudBase::Ring(float CX, float CY, float R, const FLinearColor& Color, float Thickness, int32 Segments)
{
	Arc(CX, CY, R, 0.f, 360.f, Color, Thickness);
}

void AGameHudBase::Arc(float CX, float CY, float R, float FromDeg, float ToDeg, const FLinearColor& Color, float Thickness)
{
	const int32 Segments = FMath::Max(4, FMath::CeilToInt(FMath::Abs(ToDeg - FromDeg) / 8.f));
	FVector2D Prev = FVector2D::ZeroVector;
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float A = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, i / static_cast<float>(Segments)));
		const FVector2D P(CX + FMath::Sin(A) * R, CY - FMath::Cos(A) * R);
		if (i > 0)
		{
			DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, Thickness);
		}
		Prev = P;
	}
}
