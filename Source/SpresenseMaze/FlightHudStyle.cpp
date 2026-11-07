#include "FlightHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

const FLinearColor AFlightHUD::Paper(0.95f, 0.98f, 1.f);
const FLinearColor AFlightHUD::Muted(0.70f, 0.77f, 0.81f);
const FLinearColor AFlightHUD::Mint(0.08f, 0.88f, 1.f);
const FLinearColor AFlightHUD::Coral(1.f, 0.12f, 0.48f);
const FLinearColor AFlightHUD::Ink(0.006f, 0.014f, 0.038f, 0.96f);

namespace
{
	FSlateFontInfo FlightFont(float Size, bool bBold)
	{
		// Canvas needs a runtime UFont object; standalone Slate composite fonts
		// measure correctly but do not render through the Canvas text path.
		FSlateFontInfo Font(GEngine ? GEngine->GetLargeFont() : nullptr, Size);
		Font.TypefaceFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
		return Font;
	}
}

FVector2D AFlightHUD::Measure(const FString& Value, float Size, bool bBold) const
{
	const int32 Pixels = FMath::Max(1, FMath::RoundToInt(Size * S));
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer()) { return FVector2D(Value.Len() * Pixels * 0.55f, Pixels); }
	return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, FlightFont(Pixels,bBold)));
}

void AFlightHUD::Label(const FString& Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold)
{
	if (!Canvas || Value.IsEmpty()) { return; }
	const FSlateFontInfo Font = FlightFont(FMath::Max(1, FMath::RoundToInt(Size * S)),bBold);
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Value), Font, Color);
	Canvas->DrawItem(Item);
}

void AFlightHUD::CenterLabel(const FString& Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold)
{
	Label(Value, X - Measure(Value, Size, bBold).X * 0.5f, Y, Size, Color, bBold);
}

void AFlightHUD::RoundRect(float X, float Y, float Width, float Height, float Radius, const FLinearColor& Color)
{
	if (!Canvas || !Canvas->DefaultTexture || Width <= 0 || Height <= 0 || Color.A <= 0) { return; }
	Radius = FMath::Clamp(Radius, 0.1f, FMath::Min(Width, Height) * 0.5f);
	TArray<FVector2D, TInlineAllocator<36>> Boundary;
	const FVector2D Centers[] = { {X+Width-Radius,Y+Radius}, {X+Width-Radius,Y+Height-Radius}, {X+Radius,Y+Height-Radius}, {X+Radius,Y+Radius} };
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		for (int32 Step = 0; Step <= 8; ++Step)
		{
			const float A = FMath::DegreesToRadians(-90.f + Corner * 90.f + Step * 90.f / 8.f);
			Boundary.Add(Centers[Corner] + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
	}
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Boundary.Num());
	for (int32 I = 0; I < Boundary.Num(); ++I)
	{
		FCanvasUVTri T;
		T.V0_Pos = FVector2D(X+Width*0.5f,Y+Height*0.5f);
		T.V1_Pos = Boundary[I]; T.V2_Pos = Boundary[(I+1)%Boundary.Num()];
		T.V0_UV = T.V1_UV = T.V2_UV = FVector2D(0.5,0.5);
		T.V0_Color = T.V1_Color = T.V2_Color = Color;
		Triangles.Add(T);
	}
	FCanvasTriangleItem Item(Triangles, Canvas->DefaultTexture->GetResource());
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AFlightHUD::Modal(float X, float Y, float Width, float Height)
{
	CyberPanel(X,Y,Width,Height,Mint);
}

void AFlightHUD::PillButton(FName Id, const FString& Value, float X, float Y, float Width, float Height, bool bPrimary)
{
	float MouseX = -1, MouseY = -1;
	APlayerController* PC = GetOwningPlayerController();
	if (PC) { PC->GetMousePosition(MouseX, MouseY); }
	const bool Hover = MouseX >= X && MouseX <= X+Width && MouseY >= Y && MouseY <= Y+Height;
	const bool Pressed = Hover && PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	const FLinearColor Fill = bPrimary ? (Pressed ? Mint : Paper) : Paper.CopyWithNewOpacity(Hover ? 0.16f : 0.075f);
	CyberPanel(X,Y,Width,Height,bPrimary?Mint:Muted);
	if (bPrimary) { DrawRect(Mint.CopyWithNewOpacity(Pressed?0.65f:Hover?0.32f:0.17f),X+2*S,Y+2*S,Width-20*S,Height-4*S); }
	const float Size = Height > 80*S ? 26.f : 22.f;
	const FVector2D TextExtent = Measure(Value, Size, true);
	Label(Value, X+(Width-TextExtent.X)*0.5f, Y+(Height-TextExtent.Y)*0.5f, Size, bPrimary ? Mint : Paper, true);
	AddHitBox(FVector2D(X,Y), FVector2D(Width,Height), Id, true);
}

void AFlightHUD::FadeEdges()
{
	if (!Canvas || !Canvas->DefaultTexture) { return; }
	TArray<FCanvasUVTri> Triangles;
	for (int32 Strip=0; Strip<2; ++Strip)
	{
		const float Y0=Strip?H-180*S:0,Y1=Strip?H:180*S;
		const FVector2D P[]={{0,Y0},{W,Y0},{W,Y1},{0,Y1}};
		const FLinearColor Top=FLinearColor(0.012f,0.02f,0.03f,Strip?0.f:0.42f);
		const FLinearColor Bottom=FLinearColor(0.012f,0.02f,0.03f,Strip?0.42f:0.f);
		FCanvasUVTri A,B;
		A.V0_Pos=P[0]; A.V1_Pos=P[1]; A.V2_Pos=P[2];
		A.V0_Color=A.V1_Color=Top; A.V2_Color=Bottom;
		B.V0_Pos=P[0]; B.V1_Pos=P[2]; B.V2_Pos=P[3];
		B.V0_Color=Top; B.V1_Color=B.V2_Color=Bottom;
		A.V0_UV=A.V1_UV=A.V2_UV=B.V0_UV=B.V1_UV=B.V2_UV=FVector2D(0.5,0.5);
		Triangles.Add(A); Triangles.Add(B);
	}
	FCanvasTriangleItem Item(Triangles,Canvas->DefaultTexture->GetResource());
	Item.BlendMode=SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}
