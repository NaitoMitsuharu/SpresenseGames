#include "MazeHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "MazeBallPawn.h"
#include "MazeGenerator.h"
#include "MazeSaveGame.h"
#include "SprBoxSubsystem.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor Accent(1.f, 0.58f, 0.1f);
	const FLinearColor PanelBG(0.015f, 0.025f, 0.04f, 0.72f);
	const FLinearColor PanelBorder(0.62f, 0.75f, 0.88f, 0.18f);

	float LeftHudMargin(const UCanvas* Canvas)
	{
		// Xperia Game Enhancer places its floating control in the upper-left.
		// Reserve a proportional margin while retaining a compact layout on 1080p.
		return Canvas ? FMath::Clamp(Canvas->SizeX * 0.08f, 20.f, 190.f) : 20.f;
	}
}

FSlateFontInfo AMazeHUD::UIFont(int32 Size) const
{
	return FCoreStyle::GetDefaultFontStyle("Bold", Size);
}

FVector2D AMazeHUD::JSize(const FString& Text, int32 Size) const
{
	// Rough fallback: the measure service is only there once Slate has a renderer
	if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
	{
		return FVector2D(Text.Len() * Size * 0.5f, Size);
	}
	const TSharedRef<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FVector2D(Measure->Measure(Text, UIFont(Size)));
}

void AMazeHUD::JText(const FString& Text, const FLinearColor& Color, float X, float Y, int32 Size)
{
	if (!Canvas)
	{
		return;
	}

	// Keep the Android-safe UObject-backed font resource, but request the final
	// pixel size from Slate's runtime font cache. Scaling a LegacyFontSize glyph
	// after rasterization made large headings visibly soft on the Xperia display.
	// A bare CoreStyle FSlateFontInfo is not reliable on Mobile Deferred; backing
	// it with UFont preserves the Android path and the Japanese fallback face.
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const FSlateFontInfo ExactSizeFont(Font, static_cast<float>(Size));
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), ExactSizeFont, Color);
	// Small baked drop shadow: keeps white text readable over the bright stage
	// palettes without drawing every string twice.
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.55f * Color.A), FVector2D(2.f, 2.f));
	Canvas->DrawItem(Item);
}

/**
 * Wordless lesson pictograms. Kids read these long before they read the caption,
 * so the caption is deliberately short and the picture carries the meaning.
 * Everything is boxes and lines - no textures to author or cook.
 */
void AMazeHUD::DrawLessonIcon(int32 Kind, float CX, float CY, float Size)
{
	const float U = Size / 10.f; // grid unit
	const float T = GetWorld()->GetTimeSeconds();
	const FLinearColor Ball(0.35f, 0.75f, 1.f);
	const FLinearColor Ground(1.f, 1.f, 1.f, 0.55f);
	const FLinearColor Hot(1.f, 0.62f, 0.15f);
	const FLinearColor Bad(1.f, 0.3f, 0.25f);

	auto Dot = [&](float X, float Y, float R, const FLinearColor& C)
	{
		DrawRect(C, CX + X - R, CY + Y - R, R * 2.f, R * 2.f);
	};
	auto Bar = [&](float X, float Y, float W, float H, const FLinearColor& C)
	{
		DrawRect(C, CX + X - W * 0.5f, CY + Y - H * 0.5f, W, H);
	};

	switch (Kind)
	{
	case 0: // TILT - a board see-sawing, ball running downhill
	{
		const float Tip = FMath::Sin(T * 2.2f) * 1.6f * U;
		DrawLine(CX - 4.f * U, CY + 2.f * U - Tip, CX + 4.f * U, CY + 2.f * U + Tip, Hot, 5.f);
		Dot(FMath::Sin(T * 2.2f) > 0.f ? 2.6f * U : -2.6f * U,
			2.f * U + (FMath::Sin(T * 2.2f) > 0.f ? Tip : -Tip) - 1.4f * U, 1.3f * U, Ball);
		break;
	}
	case 1: // JUMP - ball arcing over a bar
	{
		Bar(0.f, 2.5f * U, 1.4f * U, 2.4f * U, Hot);
		DrawLine(CX - 4.5f * U, CY + 3.7f * U, CX + 4.5f * U, CY + 3.7f * U, Ground, 3.f);
		const float P = FMath::Fmod(T * 0.9f, 1.f);
		const float X = FMath::Lerp(-4.f * U, 4.f * U, P);
		const float Y = 2.6f * U - FMath::Sin(P * PI) * 4.6f * U;
		Dot(X, Y, 1.2f * U, Ball);
		break;
	}
	case 2: // HOLE - gap in the floor, ball dropping through
	{
		DrawLine(CX - 4.5f * U, CY + 2.f * U, CX - 1.4f * U, CY + 2.f * U, Ground, 3.f);
		DrawLine(CX + 1.4f * U, CY + 2.f * U, CX + 4.5f * U, CY + 2.f * U, Ground, 3.f);
		Bar(0.f, 3.4f * U, 2.8f * U, 2.6f * U, FLinearColor(0.f, 0.f, 0.f, 0.85f));
		Dot(0.f, -2.6f * U + FMath::Fmod(T * 2.2f, 1.f) * 5.f * U, 1.2f * U, Bad);
		break;
	}
	case 3: // ICE - ball sliding, long speed trails
	{
		DrawLine(CX - 4.5f * U, CY + 2.4f * U, CX + 4.5f * U, CY + 2.4f * U,
			FLinearColor(0.6f, 0.9f, 1.f, 0.8f), 4.f);
		Dot(2.2f * U, 0.6f * U, 1.3f * U, Ball);
		for (int32 i = 0; i < 3; ++i)
		{
			const float A = 0.5f - i * 0.14f;
			DrawLine(CX - 3.6f * U + i * 0.6f * U, CY + (0.2f + i * 0.5f) * U,
				CX + 0.4f * U, CY + (0.2f + i * 0.5f) * U, FLinearColor(1.f, 1.f, 1.f, A), 3.f);
		}
		break;
	}
	case 4: // MUD - ball sunk in a thick band, short stubby trail
	{
		Bar(0.f, 2.6f * U, 9.f * U, 2.2f * U, FLinearColor(0.42f, 0.28f, 0.14f));
		Dot(0.6f * U, 1.6f * U, 1.3f * U, Ball);
		DrawLine(CX - 2.4f * U, CY + 1.6f * U, CX - 1.f * U, CY + 1.6f * U,
			FLinearColor(1.f, 1.f, 1.f, 0.35f), 3.f);
		break;
	}
	case 5: // BOOST - chevrons scrolling right, ball riding them
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const float P = FMath::Fmod(T * 1.3f + i / 3.f, 1.f);
			const float X = FMath::Lerp(-4.2f * U, 2.4f * U, P);
			const float A = FMath::Sin(P * PI);
			DrawLine(CX + X, CY - 1.4f * U, CX + X + 1.3f * U, CY, Hot.CopyWithNewOpacity(A), 4.f);
			DrawLine(CX + X, CY + 1.4f * U, CX + X + 1.3f * U, CY, Hot.CopyWithNewOpacity(A), 4.f);
		}
		Dot(3.4f * U, 0.f, 1.2f * U, Ball);
		break;
	}
	case 6: // CRUMBLE - a tile breaking into falling pieces
	{
		const float P = FMath::Fmod(T * 0.7f, 1.f);
		const float Drop = FMath::Max(P - 0.55f, 0.f) * 8.f * U;
		Dot(0.f, -1.8f * U, 1.2f * U, Ball);
		Bar(-1.6f * U, 2.f * U + Drop, 2.6f * U, 0.9f * U, FLinearColor(0.6f, 0.58f, 0.54f));
		Bar(1.6f * U, 2.f * U + Drop * 1.4f, 2.6f * U, 0.9f * U, FLinearColor(0.6f, 0.58f, 0.54f));
		DrawLine(CX - 0.2f * U, CY + 1.2f * U, CX + 0.2f * U, CY + 2.8f * U, Bad, 2.f);
		break;
	}
	case 7: // BUTTON & GATE - press left, bars lift on the right
	{
		const float P = FMath::Fmod(T * 0.8f, 1.f);
		const float Press = P > 0.4f ? 0.6f * U : 0.f;
		Dot(-3.2f * U, -0.6f * U - (P > 0.4f ? 0.f : 1.f) * U, 1.1f * U, Ball);
		Bar(-3.2f * U, 1.9f * U + Press, 2.6f * U, 0.8f * U, Bad);
		const float Lift = P > 0.4f ? 2.6f * U : 0.f;
		for (int32 i = 0; i < 3; ++i)
		{
			Bar(2.6f * U, (0.4f + i * 1.f) * U + Lift, 3.f * U, 0.5f * U,
				FLinearColor(0.72f, 0.45f, 1.f));
		}
		break;
	}
	case 8: // WARP - in one glowing ring, out of the other
	{
		const float Pulse = 0.55f + 0.45f * FMath::Sin(T * 5.f);
		const FLinearColor Cyan(0.15f, 0.95f, 1.f, Pulse);
		Bar(-2.8f * U, 2.2f * U, 3.f * U, 1.f * U, Cyan);
		Bar(2.8f * U, 2.2f * U, 3.f * U, 1.f * U, Cyan);
		const float P = FMath::Fmod(T * 0.9f, 1.f);
		if (P < 0.5f)
		{
			Dot(-2.8f * U, -1.f * U + P * 6.f * U, 1.1f * U, Ball);
		}
		else
		{
			Dot(2.8f * U, 2.f * U - (P - 0.5f) * 6.f * U, 1.1f * U, Ball);
		}
		DrawLine(CX - 1.6f * U, CY + 3.4f * U, CX + 1.6f * U, CY + 3.4f * U, Cyan, 2.f);
		break;
	}
	default:
		break;
	}
}

AMazeGenerator* AMazeHUD::FindMaze()
{
	if (CachedMaze.IsValid())
	{
		return CachedMaze.Get();
	}
	TActorIterator<AMazeGenerator> It(GetWorld());
	if (It)
	{
		CachedMaze = *It;
		return *It;
	}
	return nullptr;
}

void AMazeHUD::DrawGlassPanel(float X, float Y, float W, float H)
{
	DrawRect(PanelBG, X, Y, W, H);
	// 1px border
	DrawRect(PanelBorder, X, Y, W, 1.f);
	DrawRect(PanelBorder, X, Y + H - 1.f, W, 1.f);
	DrawRect(PanelBorder, X, Y, 1.f, H);
	DrawRect(PanelBorder, X + W - 1.f, Y, 1.f, H);
	// accent bar (left edge)
	DrawRect(Accent.CopyWithNewOpacity(0.82f), X, Y, 2.f, H);
}

void AMazeHUD::DrawStatusDot(float X, float Y, bool bOn)
{
	const FLinearColor On(0.25f, 0.95f, 0.45f);
	const FLinearColor Off(0.95f, 0.3f, 0.25f);
	const FLinearColor C = bOn ? On : Off;
	// soft glow: layered translucent squares
	FLinearColor Glow = C; Glow.A = 0.25f;
	DrawRect(Glow, X - 3.f, Y - 3.f, 16.f, 16.f);
	Glow.A = 0.45f;
	DrawRect(Glow, X - 1.f, Y - 1.f, 12.f, 12.f);
	DrawRect(C, X, Y, 10.f, 10.f);
}

void AMazeHUD::DrawHUD()
{
	Super::DrawHUD();

	AMazeGenerator* Maze = FindMaze();
	if (!Maze || !Canvas || !GEngine)
	{
		return;
	}

	switch (Maze->Flow)
	{
	case EMazeFlow::Home:
		DrawHomeScreen(Maze);
		break;
	case EMazeFlow::ClearChoice:
		DrawClearChoice(Maze);
		break;
	case EMazeFlow::AllClearRanking:
		DrawAllClearRanking(Maze);
		break;
	case EMazeFlow::Playing:
	default:
		DrawMinimap(Maze);
		DrawButton(TEXT("Home"), TEXT("ホーム"), LeftHudMargin(Canvas) + 270.f + 12.f, 20.f, 120.f, 60.f, false);
		if (AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn()); Ball && !Ball->IsTouchControllerActive())
		{
			DrawAttitudeIndicator();
			DrawButton(TEXT("LevelReset"), TEXT("姿勢リセット"), LeftHudMargin(Canvas), 244.f, 190.f, 38.f, false);
		}
		DrawJumpFeedback();
		DrawGoalArrow(Maze);
		DrawMessages(Maze);
		DrawTutorialHint(Maze);
		DrawOrientationWarning();
		DrawTouchController();
		break;
	}

	DrawCalibrationOverlay();
}

void AMazeHUD::DrawJumpFeedback()
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Ball || !Canvas)
	{
		return;
	}
	const float Age = GetWorld()->GetTimeSeconds() - Ball->GetLastJumpTime();
	if (Age < 0.f || Age > 0.5f)
	{
		return;
	}
	const FVector Screen = Project(Ball->GetActorLocation());
	if (Screen.Z <= 0.f)
	{
		return;
	}
	const float T = FMath::Clamp(Age / 0.5f, 0.f, 1.f);
	const float Radius = 22.f + 70.f * T;
	const float Alpha = (1.f - T) * 0.85f;
	const FLinearColor C(0.25f, 0.9f, 1.f, Alpha);
	for (int32 Ring = 0; Ring < 2; ++Ring)
	{
		const float R = Radius + Ring * 10.f;
		const float Thick = 3.f - Ring;
		for (int32 I = 0; I < 32; ++I)
		{
			const float A0 = (I / 32.f) * 2.f * PI;
			const float A1 = ((I + 1) / 32.f) * 2.f * PI;
			DrawLine(Screen.X + FMath::Cos(A0) * R, Screen.Y + FMath::Sin(A0) * R,
				Screen.X + FMath::Cos(A1) * R, Screen.Y + FMath::Sin(A1) * R, C, Thick);
		}
	}
	// Four small lift streaks emphasize the upward impulse without obscuring play.
	for (int32 I = 0; I < 4; ++I)
	{
		const float A = I * PI * 0.5f + PI * 0.25f;
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D From(Screen.X, Screen.Y);
		const FVector2D To = From + Dir * (34.f + 22.f * T);
		DrawLine(From.X + Dir.X * Radius * 0.45f, From.Y + Dir.Y * Radius * 0.45f,
			To.X, To.Y, C, 3.f);
	}
}

void AMazeHUD::DrawTouchController()
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Ball || !Ball->IsTouchControllerActive() || !Canvas)
	{
		return;
	}

	FVector2D StickCenter;
	FVector2D JumpCenter;
	float StickRadius = 0.f;
	float JumpRadius = 0.f;
	Ball->GetTouchControlLayout(FVector2D(Canvas->SizeX, Canvas->SizeY),
		StickCenter, StickRadius, JumpCenter, JumpRadius);

	auto Ring = [this](const FVector2D& Center, float Radius,
		const FLinearColor& Color, float Thickness)
	{
		constexpr int32 Segments = 40;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float A0 = 2.f * PI * static_cast<float>(Index) / Segments;
			const float A1 = 2.f * PI * static_cast<float>(Index + 1) / Segments;
			DrawLine(Center.X + FMath::Cos(A0) * Radius, Center.Y + FMath::Sin(A0) * Radius,
				Center.X + FMath::Cos(A1) * Radius, Center.Y + FMath::Sin(A1) * Radius,
				Color, Thickness);
		}
	};

	// Low-opacity industrial rings fit the Modern Kinetic Lab HUD without hiding
	// the maze. The cyan knob reflects analogue direction and distance.
	Ring(StickCenter, StickRadius, FLinearColor(0.62f, 0.78f, 0.92f, 0.42f), 5.f);
	Ring(StickCenter, StickRadius * 0.55f, FLinearColor(0.62f, 0.78f, 0.92f, 0.16f), 2.f);
	DrawLine(StickCenter.X - StickRadius * 0.75f, StickCenter.Y,
		StickCenter.X + StickRadius * 0.75f, StickCenter.Y,
		FLinearColor(0.72f, 0.84f, 0.95f, 0.16f), 2.f);
	DrawLine(StickCenter.X, StickCenter.Y - StickRadius * 0.75f,
		StickCenter.X, StickCenter.Y + StickRadius * 0.75f,
		FLinearColor(0.72f, 0.84f, 0.95f, 0.16f), 2.f);

	const FVector2D Move = Ball->GetTouchMoveInput();
	const FVector2D Knob = StickCenter + FVector2D(Move.Y, -Move.X) * StickRadius * 0.62f;
	const float KnobRadius = StickRadius * 0.27f;
	DrawRect(FLinearColor(0.08f, 0.65f, 0.9f, 0.18f),
		Knob.X - KnobRadius, Knob.Y - KnobRadius, KnobRadius * 2.f, KnobRadius * 2.f);
	Ring(Knob, KnobRadius, FLinearColor(0.28f, 0.88f, 1.f, 0.9f), 4.f);

	const bool bJumpDown = Ball->IsTouchJumpPressed();
	const FLinearColor JumpColor = bJumpDown
		? FLinearColor(1.f, 0.68f, 0.18f, 0.95f)
		: FLinearColor(1.f, 0.58f, 0.1f, 0.58f);
	DrawRect(JumpColor.CopyWithNewOpacity(bJumpDown ? 0.24f : 0.10f),
		JumpCenter.X - JumpRadius, JumpCenter.Y - JumpRadius, JumpRadius * 2.f, JumpRadius * 2.f);
	Ring(JumpCenter, JumpRadius, JumpColor, bJumpDown ? 7.f : 5.f);
	// Two upward chevrons communicate jump without another label.
	const float Icon = JumpRadius * 0.24f;
	for (int32 Row = 0; Row < 2; ++Row)
	{
		const float Y = JumpCenter.Y + Icon * (0.55f + Row * 0.95f);
		DrawLine(JumpCenter.X - Icon, Y, JumpCenter.X, Y - Icon, JumpColor, 5.f);
		DrawLine(JumpCenter.X + Icon, Y, JumpCenter.X, Y - Icon, JumpColor, 5.f);
	}
}

void AMazeHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);

	AMazeGenerator* Maze = FindMaze();
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Maze)
	{
		return;
	}

	if (BoxName == TEXT("Start"))
	{
		Maze->StartGame();
	}
	else if (BoxName == TEXT("Tutorial"))
	{
		Maze->StartTutorial();
	}
	else if (BoxName == TEXT("Calibrate") && Ball)
	{
		Ball->StartCalibration(true);  // full SDK filter reset + zero capture
	}
	else if (BoxName == TEXT("LevelReset") && Ball)
	{
		Ball->StartCalibration(false); // instant SDK lightReset (drift fix mid-game)
	}
	else if (BoxName == TEXT("CalibContinue") && Ball)
	{
		Ball->ContinueCalibration();
	}
	else if (BoxName == TEXT("Next"))
	{
		Maze->ChooseNextStage();
	}
	else if (BoxName == TEXT("Home"))
	{
		Maze->GoHome();
	}
}

void AMazeHUD::DrawButton(FName Id, const FString& Label, float X, float Y, float W, float H, bool bPrimary)
{
	if (bPrimary)
	{
		DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f), X, Y, W, H);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.25f), X, Y, W, 2.f);
	}
	else
	{
		DrawGlassPanel(X, Y, W, H);
	}

	const int32 Size = H > 50.f ? 28 : 20;
	const FVector2D TS = JSize(Label, Size);
	JText(Label,
		bPrimary ? FLinearColor(0.08f, 0.06f, 0.02f) : FLinearColor::White,
		X + (W - TS.X) * 0.5f, Y + (H - TS.Y) * 0.5f, Size);

	AddHitBox(FVector2D(X, Y), FVector2D(W, H), Id, true);
}

void AMazeHUD::DrawHomeScreen(AMazeGenerator* Maze)
{
	const float CX = Canvas->SizeX * 0.5f;

	// Dim + vignette-ish bars
	DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.65f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	// Title (kept in Latin - it is the product name, not a sentence to read)
	const FString Title = TEXT("SPRESENSE MAZE");
	const float T = GetWorld()->GetTimeSeconds();
	const FVector2D TS = JSize(Title, 68);
	const float TitleY = Canvas->SizeY * 0.17f;
	JText(Title, FLinearColor::White, CX - TS.X * 0.5f, TitleY, 68);
	const float LineW = TS.X * (0.8f + 0.05f * FMath::Sin(T * 2.f));
	DrawRect(Accent, CX - LineW * 0.5f, TitleY + TS.Y + 8.f, LineW, 4.f);

	// Buttons
	const float BW = 340.f, BH = 64.f, Gap = 14.f;
	const float BY = Canvas->SizeY * 0.45f;
	DrawButton(TEXT("Start"), TEXT("開始"), CX - BW * 0.5f, BY, BW, BH, true);
	DrawButton(TEXT("Tutorial"), TEXT("チュートリアル"), CX - BW * 0.5f, BY + (BH + Gap), BW, BH, false);
	DrawButton(TEXT("Calibrate"), TEXT("キャリブレーション"), CX - BW * 0.5f, BY + (BH + Gap) * 2.f, BW, BH, false);

	// Connection status
	bool bService = false, bBoard = false;
	if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
	{
		SprBox->GetConnectionStatus(bService, bBoard);
	}
	const bool bSprBoxReady = bService && bBoard;
	const FString InputName = bSprBoxReady ? TEXT("SPRBOX") : TEXT("TOUCH");
	const FVector2D InputSize = JSize(InputName, 18);
	const float SY = BY + (BH + Gap) * 3.f + 12.f;
	const float StatusX = CX - (InputSize.X + 28.f) * 0.5f;
	if (bSprBoxReady)
	{
		DrawStatusDot(StatusX, SY + 2.f, true);
	}
	else
	{
		DrawRect(FLinearColor(0.22f, 0.82f, 1.f, 0.9f), StatusX, SY + 2.f, 10.f, 10.f);
	}
	JText(InputName, FLinearColor(1.f, 1.f, 1.f, 0.8f), StatusX + 20.f, SY - 3.f, 18);
}

void AMazeHUD::DrawClearChoice(AMazeGenerator* Maze)
{
	const float CX = Canvas->SizeX * 0.5f;

	DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const FString Title = FString::Printf(TEXT("STAGE %02d CLEAR"), Maze->CurrentStage + 1);
	const FVector2D TS = JSize(Title, 54);
	const float TitleY = Canvas->SizeY * 0.28f;
	JText(Title, FLinearColor::White, CX - TS.X * 0.5f, TitleY, 54);
	DrawRect(Accent, CX - TS.X * 0.55f, TitleY + TS.Y + 10.f, TS.X * 1.1f, 4.f);

	const float BW = 340.f, BH = 64.f;
	DrawButton(TEXT("Next"), TEXT("次へ"), CX - BW * 0.5f, Canvas->SizeY * 0.52f, BW, BH, true);
	DrawButton(TEXT("Home"), TEXT("ホーム"), CX - BW * 0.5f, Canvas->SizeY * 0.52f + BH + 18.f, BW, BH, false);
}

void AMazeHUD::DrawAllClearRanking(AMazeGenerator* Maze)
{
	const float CX = Canvas->SizeX * 0.5f;

	DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.7f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const FString Title = TEXT("ALL CLEAR");
	const FVector2D TS = JSize(Title, 46);
	const float TitleY = Canvas->SizeY * 0.05f;
	JText(Title, FLinearColor::White, CX - TS.X * 0.5f, TitleY, 46);
	DrawRect(Accent, CX - TS.X * 0.55f, TitleY + TS.Y + 8.f, TS.X * 1.1f, 4.f);

	const FString Subtitle = TEXT("ステージ別タイムランキング");
	const FVector2D SubSize = JSize(Subtitle, 18);
	JText(Subtitle, FLinearColor(1.f, 1.f, 1.f, 0.75f), CX - SubSize.X * 0.5f, TitleY + TS.Y + 24.f, 18);

	auto FormatTime = [](float Seconds) -> FString
	{
		if (Seconds < 0.f)
		{
			return TEXT("--:--.-");
		}
		const int32 Minutes = FMath::FloorToInt(Seconds / 60.f);
		const float Remainder = Seconds - static_cast<float>(Minutes) * 60.f;
		return FString::Printf(TEXT("%d:%04.1f"), Minutes, Remainder);
	};

	const int32 StageCount = Maze->GetStageCount();
	const float ListTop = TitleY + TS.Y + 56.f;
	const float BottomMargin = Canvas->SizeY * 0.16f;
	const float RowH = FMath::Clamp((Canvas->SizeY - BottomMargin - ListTop) / FMath::Max(StageCount, 1), 46.f, 74.f);
	const float PanelW = FMath::Min(Canvas->SizeX * 0.86f, 940.f);
	const float PanelX = CX - PanelW * 0.5f;

	for (int32 i = 0; i < StageCount; ++i)
	{
		const float RowY = ListTop + i * RowH;
		DrawGlassPanel(PanelX, RowY, PanelW, RowH - 6.f);

		JText(FString::Printf(TEXT("Stage%02d"), i + 1), FLinearColor::White, PanelX + 16.f, RowY + 6.f, 19);

		const float RunTime = Maze->RunStageTimes.IsValidIndex(i) ? Maze->RunStageTimes[i] : -1.f;
		JText(FString::Printf(TEXT("今回 %s"), *FormatTime(RunTime)),
			FLinearColor(0.6f, 0.85f, 1.f, 1.f), PanelX + 16.f, RowY + 30.f, 15);

		FString RankLine;
		bool bIsNewBest = false;
		if (Maze->SaveGameData && Maze->SaveGameData->StageBestTimes.IsValidIndex(i))
		{
			const TArray<float>& Times = Maze->SaveGameData->StageBestTimes[i].Times;
			for (int32 Rank = 0; Rank < Times.Num(); ++Rank)
			{
				if (Rank > 0)
				{
					RankLine += TEXT("   ");
				}
				RankLine += FString::Printf(TEXT("%d.%s"), Rank + 1, *FormatTime(Times[Rank]));
			}
			if (Times.Num() > 0 && RunTime >= 0.f && FMath::IsNearlyEqual(Times[0], RunTime, 0.001f))
			{
				bIsNewBest = true;
			}
		}
		JText(RankLine, FLinearColor(1.f, 1.f, 1.f, 0.85f), PanelX + 210.f, RowY + 18.f, 15);
		if (bIsNewBest)
		{
			JText(TEXT("NEW!"), Accent, PanelX + PanelW - 84.f, RowY + 18.f, 18);
		}
	}

	const float BW = 340.f, BH = 64.f;
	DrawButton(TEXT("Home"), TEXT("ホーム"), CX - BW * 0.5f, Canvas->SizeY - BottomMargin + 10.f, BW, BH, true);
}

void AMazeHUD::DrawCalibrationOverlay()
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Ball || Ball->GetCalibrationPhase() == 0)
	{
		return;
	}
	const float CX = Canvas->SizeX * 0.5f;
	const float CY = Canvas->SizeY * 0.72f;
	const int32 Phase = Ball->GetCalibrationPhase();

	FString Msg;
	switch (Phase)
	{
	case 1: Msg = TEXT("1/2　水平に置く"); break;
	case 2: Msg = TEXT("姿勢を変える"); break;
	default: Msg = TEXT("2/2　静止する"); break;
	}

	const FVector2D TS = JSize(Msg, 26);
	const float PW = FMath::Max(TS.X + 60.f, 460.f);
	const float PH = Phase == 2 ? 130.f : 74.f;
	DrawGlassPanel(CX - PW * 0.5f, CY - 18.f, PW, PH);
	JText(Msg, FLinearColor::White, CX - TS.X * 0.5f, CY - 6.f, 26);

	if (Phase == 2)
	{
		// wait for the user to reposition, then continue
		DrawButton(TEXT("CalibContinue"), TEXT("次へ"), CX - 130.f, CY + TS.Y + 14.f, 260.f, 52.f, true);
	}
	else
	{
		const float BarW = PW - 60.f;
		const float Progress = Ball->GetCalibrationProgress();
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.15f), CX - BarW * 0.5f, CY + TS.Y + 8.f, BarW, 8.f);
		DrawRect(Accent, CX - BarW * 0.5f, CY + TS.Y + 8.f, BarW * Progress, 8.f);
	}
}

void AMazeHUD::DrawAttitudeIndicator()
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Ball)
	{
		return;
	}

	// Panel below the stage/status panel
	const float PX = LeftHudMargin(Canvas), PY = 96.f, PW = 250.f, PH = 138.f;
	DrawGlassPanel(PX, PY, PW, PH);

	// Oblong box (Spresense lying flat), rotated by the live roll/pitch.
	// Mapping (matches how the device is held):
	//   P down -> the NEAR long edge dips (rotation around the long axis)
	//   R down -> the RIGHT short edge rises (rotation around the short axis)
	const FVector Extents(60.f, 35.f, 14.f);
	const FRotator DeviceRot(Ball->CurrentRollDeg, 0.f, Ball->CurrentPitchDeg);
	const FRotationMatrix DeviceM(DeviceRot);
	const FRotationMatrix ViewM(FRotator(-25.f, 35.f, 0.f)); // fixed isometric view

	const float CX = PX + PW * 0.5f;
	const float CY = PY + PH * 0.5f;

	FVector2D P[8];
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Corner(
			(i & 1) ? Extents.X : -Extents.X,
			(i & 2) ? Extents.Y : -Extents.Y,
			(i & 4) ? Extents.Z : -Extents.Z);
		const FVector V = ViewM.InverseTransformVector(DeviceM.TransformVector(Corner));
		P[i] = FVector2D(CX + V.Y, CY - V.Z);
	}

	const FLinearColor EdgeColor = Ball->bOrientationWarning
		? FLinearColor(1.f, 0.25f, 0.2f)
		: FLinearColor(1.f, 1.f, 1.f, 0.55f);
	const FLinearColor TopColor = Ball->bOrientationWarning
		? FLinearColor(1.f, 0.35f, 0.3f)
		: Accent;

	static const int32 Edges[12][2] = {
		{0,1},{2,3},{0,2},{1,3},          // bottom (z-)
		{4,5},{6,7},{4,6},{5,7},          // top (z+)
		{0,4},{1,5},{2,6},{3,7} };        // verticals
	for (int32 e = 0; e < 12; ++e)
	{
		const bool bTop = e >= 4 && e < 8;
		DrawLine(P[Edges[e][0]].X, P[Edges[e][0]].Y, P[Edges[e][1]].X, P[Edges[e][1]].Y,
			bTop ? TopColor : EdgeColor, bTop ? 3.f : 1.5f);
	}

	// Keep the numeric attitude visible next to the model for calibration and
	// input-source debugging. Values are degrees and are intentionally compact.
	const FString AttitudeText = FString::Printf(TEXT("R %+.3f  P %+.3f  Y %+.3f"),
		Ball->CurrentRollDeg, Ball->CurrentPitchDeg, Ball->CurrentYawDeg);
	JText(AttitudeText, FLinearColor::White, PX + 14.f, PY + PH - 24.f, 13);

	// Acceleration direction arrow (device-frame ax/ay, drawn on the box plane)
	if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
	{
		const FSprBoxImuData Imu = SprBox->GetImuData();
		if (Imu.bValid)
		{
			const float Scale = 6.f; // px per m/s^2
			FVector AccLocal(Imu.Ax * Scale, Imu.Ay * Scale, 0.f);
			AccLocal = AccLocal.GetClampedToMaxSize(70.f);
			if (AccLocal.Size() > 6.f)
			{
				const FVector V = ViewM.InverseTransformVector(DeviceM.TransformVector(AccLocal));
				const FVector2D From(CX, CY);
				const FVector2D To(CX + V.Y, CY - V.Z);
				const FLinearColor AccColor(0.2f, 0.95f, 1.f);
				DrawLine(From.X, From.Y, To.X, To.Y, AccColor, 3.f);
				// arrow head
				FVector2D Dir = To - From;
				if (Dir.Normalize())
				{
					const FVector2D Perp(-Dir.Y, Dir.X);
					const FVector2D T1 = To - Dir * 10.f + Perp * 6.f;
					const FVector2D T2 = To - Dir * 10.f - Perp * 6.f;
					DrawLine(To.X, To.Y, T1.X, T1.Y, AccColor, 3.f);
					DrawLine(To.X, To.Y, T2.X, T2.Y, AccColor, 3.f);
				}
			}
		}
	}
}

void AMazeHUD::DrawOrientationWarning()
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
	if (!Ball || !Ball->bOrientationWarning || !Canvas || !GEngine)
	{
		return;
	}

	const float T = GetWorld()->GetTimeSeconds();
	const float Pulse = 0.6f + 0.4f * FMath::Sin(T * 6.f);

	DrawRect(FLinearColor(0.3f, 0.02f, 0.02f, 0.45f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const FString Title = TEXT("傾斜制限");
	const FString Sub = TEXT("端末を水平に戻してください");
	const FVector2D TS = JSize(Title, 54);
	const float X = (Canvas->SizeX - TS.X) * 0.5f;
	const float Y = Canvas->SizeY * 0.4f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 40.f, Y - 24.f, TS.X + 80.f, TS.Y + 80.f);
	DrawRect(FLinearColor(1.f, 0.3f, 0.2f, Pulse), X - 40.f, Y - 24.f, TS.X + 80.f, 4.f);
	JText(Title, FLinearColor(1.f, 0.45f, 0.35f, Pulse), X, Y, 54);

	const FVector2D SS = JSize(Sub, 24);
	JText(Sub, FLinearColor(1.f, 1.f, 1.f, 0.9f), (Canvas->SizeX - SS.X) * 0.5f, Y + TS.Y + 12.f, 24);
}

void AMazeHUD::DrawMinimap(AMazeGenerator* Maze)
{
	const TArray<FString>& Layout = Maze->MazeLayout;
	const int32 NumRows = Layout.Num();
	int32 NumCols = 0;
	for (const FString& Row : Layout)
	{
		NumCols = FMath::Max(NumCols, Row.Len());
	}
	if (NumRows == 0 || NumCols == 0)
	{
		return;
	}

	// Camera looks down world +X: screen-right = +Y (rows), screen-up = +X (cols)
	const float Cell = MinimapSize / FMath::Max(NumRows, NumCols);
	const float MapW = Cell * NumRows;
	const float MapH = Cell * NumCols;
	const float Pad = 10.f;
	const float PX = Canvas->SizeX - MapW - MinimapMargin - Pad * 2.f;
	const float PY = MinimapMargin;
	const float X0 = PX + Pad;
	const float Y0 = PY + Pad;

	DrawGlassPanel(PX, PY, MapW + Pad * 2.f, MapH + Pad * 2.f);

	const float T = GetWorld()->GetTimeSeconds();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 5.f);

	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		const FString& Line = Layout[Row];
		for (int32 Col = 0; Col < NumCols; ++Col)
		{
			const TCHAR Ch = Col < Line.Len() ? Line[Col] : TEXT('.');
			FLinearColor Color;
			switch (Ch)
			{
			// Same three-colour code as the maze itself: amber = you act on it,
			// red = it hurts, cyan = it moves you.
			case TEXT('#'): Color = FLinearColor(0.66f, 0.72f, 0.82f, 0.9f); break;              // wall
			case TEXT('H'): Color = FLinearColor(0.85f, 0.62f, 0.15f, 0.95f); break;             // hurdle
			case TEXT('O'): Color = FLinearColor(1.f, 0.3f, 0.24f, 0.95f); break;                // hole
			case TEXT('I'): Color = FLinearColor(0.6f, 0.85f, 1.f, 0.55f); break;                // ice
			case TEXT('M'): Color = FLinearColor(0.34f, 0.26f, 0.16f, 0.9f); break;              // mud
			case TEXT('C'): Color = FLinearColor(0.35f, 0.85f, 1.f, 0.78f); break;               // glass
			case TEXT('X'): Color = FLinearColor(1.f, 0.48f, 0.06f, 0.88f); break;               // breakable wall
			case TEXT('B'): Color = FLinearColor(1.f, 0.72f, 0.15f, 1.f); break;                 // button
			case TEXT('D'): Color = FLinearColor(0.62f, 0.46f, 0.2f, 0.95f); break;              // door
			case TEXT('W'): Color = FLinearColor(0.15f, 0.85f, 1.f, 0.9f * Pulse + 0.1f); break; // warp
			case TEXT('^'): case TEXT('v'): case TEXT('<'): case TEXT('>'):
				Color = FLinearColor(0.15f, 0.85f, 1.f, 0.85f); break;                            // boost pad
			case TEXT('G'): Color = FLinearColor(1.f, 0.85f * Pulse + 0.1f, 0.f, 1.f); break;    // goal (pulsing)
			default: continue;
			}
			// No per-cell gutter: connected structures read as one diagram rather
			// than a pixel-art grid.
			DrawRect(Color, X0 + Row * Cell, Y0 + (NumCols - 1 - Col) * Cell,
				Cell + 0.35f, Cell + 0.35f);
		}
	}

	// Ball position marker
	if (APawn* Pawn = GetOwningPawn())
	{
		const FVector Rel = Maze->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
		const float Col = Rel.X / Maze->CellSize;
		const float Row = Rel.Y / Maze->CellSize;
		if (Col >= 0.f && Col <= NumCols && Row >= 0.f && Row <= NumRows)
		{
			const float Px = X0 + Row * Cell;
			const float Py = Y0 + (NumCols - Col) * Cell;
			const float R = FMath::Max(Cell * 0.32f, 3.f);
			FLinearColor Glow(0.2f, 0.6f, 1.f, 0.35f);
			DrawRect(Glow, Px - R - 2.f, Py - R - 2.f, (R + 2.f) * 2.f, (R + 2.f) * 2.f);
			DrawRect(FLinearColor::White, Px - R, Py - R, R * 2.f, R * 2.f);
			DrawRect(FLinearColor(0.15f, 0.55f, 1.f), Px - R + 1.f, Py - R + 1.f, R * 2.f - 2.f, R * 2.f - 2.f);
		}
	}
}

void AMazeHUD::DrawGoalArrow(AMazeGenerator* Maze)
{
	APawn* Pawn = GetOwningPawn();
	if (!Pawn || !Maze->TransitionMessage.IsEmpty())
	{
		return;
	}

	const FVector BallScreen3 = Project(Pawn->GetActorLocation());
	const FVector GoalScreen3 = Project(Maze->GoalLocation + FVector(0.f, 0.f, 50.f));
	if (BallScreen3.Z <= 0.f)
	{
		return;
	}

	FVector2D Dir;
	if (GoalScreen3.Z > 0.f)
	{
		Dir = FVector2D(GoalScreen3.X - BallScreen3.X, GoalScreen3.Y - BallScreen3.Y);
	}
	else
	{
		Dir = FVector2D(0.f, -1.f);
	}
	if (!Dir.Normalize())
	{
		return;
	}

	const float DistPx = FVector2D::Distance(
		FVector2D(GoalScreen3.X, GoalScreen3.Y), FVector2D(BallScreen3.X, BallScreen3.Y));
	if (GoalScreen3.Z > 0.f && DistPx < 160.f)
	{
		return;
	}

	const FVector2D Center(BallScreen3.X + Dir.X * 90.f, BallScreen3.Y + Dir.Y * 90.f);
	const FVector2D Perp(-Dir.Y, Dir.X);
	const float T = GetWorld()->GetTimeSeconds();
	const float Bob = 6.f * FMath::Sin(T * 6.f);
	const FVector2D Tip = Center + Dir * (26.f + Bob);
	const FVector2D L = Center - Dir * 6.f + Perp * 14.f;
	const FVector2D R = Center - Dir * 6.f - Perp * 14.f;

	const float Thick = 5.f;
	DrawLine(Tip.X, Tip.Y, L.X, L.Y, Accent, Thick);
	DrawLine(Tip.X, Tip.Y, R.X, R.Y, Accent, Thick);
	DrawLine(L.X, L.Y, R.X, R.Y, Accent, Thick);
}

void AMazeHUD::DrawTutorialHint(AMazeGenerator* Maze)
{
	if (!Maze->bTutorialMode)
	{
		return;
	}
	FString Hint = Maze->GetStageHint();
	// The same lessons support the SPR-BOX and the phone fallback. Name the
	// control that is actually on screen instead of describing a missing device.
	if (const AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn()); Ball && Ball->IsTouchControllerActive())
	{
		if (Maze->CurrentStage == 0)
		{
			Hint = TEXT("スティックで移動");
		}
		else if (Maze->CurrentStage == 1)
		{
			Hint = TEXT("ボタンでジャンプ");
		}
	}
	if (Hint.IsEmpty())
	{
		return;
	}

	// The animated pictogram is the lesson; the caption only names it.
	const int32 Icon = Maze->GetStageHintIcon();
	const float IconBox = Icon >= 0 ? 86.f : 0.f;
	const float Gap = Icon >= 0 ? 18.f : 0.f;

	const int32 Size = 30;
	const FVector2D TS = JSize(Hint, Size);
	const float PH = FMath::Max(TS.Y + 34.f, IconBox + 18.f);
	const float PW = FMath::Min(TS.X + IconBox + Gap + 56.f, Canvas->SizeX - 40.f);
	const float PX = (Canvas->SizeX - PW) * 0.5f;
	const float PY = Canvas->SizeY - PH - 30.f;

	DrawGlassPanel(PX, PY, PW, PH);
	// Slow pulse on the top edge: reads as "look here" without adding noise
	const float Pulse = 0.72f + 0.28f * FMath::Sin(GetWorld()->GetTimeSeconds() * 3.f);
	DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.9f * Pulse), PX, PY, PW, 3.f);

	const float ContentW = TS.X + IconBox + Gap;
	const float ContentX = PX + (PW - ContentW) * 0.5f;
	if (Icon >= 0)
	{
		DrawLessonIcon(Icon, ContentX + IconBox * 0.5f, PY + PH * 0.5f, IconBox);
	}
	JText(Hint, FLinearColor::White, ContentX + IconBox + Gap, PY + (PH - TS.Y) * 0.5f, Size);
}

void AMazeHUD::DrawMessages(AMazeGenerator* Maze)
{
	// ---- Top-left: stage chip + connection status in one glass panel ----
	{
		const float PX = LeftHudMargin(Canvas), PY = 20.f, PW = 270.f, PH = 60.f;
		DrawGlassPanel(PX, PY, PW, PH);

		const FString StageNo = Maze->bTutorialMode
			? FString::Printf(TEXT("GUIDE %d/%d"), Maze->CurrentStage + 1, Maze->GetStageCount())
			: FString::Printf(TEXT("STAGE %02d"), Maze->CurrentStage + 1);
		JText(StageNo, Accent, PX + 14.f, PY + 6.f, 18);
		const AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
		const FString InputSource = Ball && Ball->IsUsingSprBoxInput()
			? TEXT("IMU: SPR-BOX")
			: (Ball && Ball->IsUsingDeviceMotion() ? TEXT("IMU: Android") : TEXT("入力: タッチ"));
		JText(InputSource, FLinearColor::White, PX + 14.f, PY + 30.f, 16);

		bool bService = false, bBoard = false;
		if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
		{
			SprBox->GetConnectionStatus(bService, bBoard);
		}
		if (!Ball || !Ball->IsTouchControllerActive())
		{
			DrawStatusDot(PX + PW - 24.f, PY + 12.f, bService && bBoard);
		}
	}

	// ---- Stage-clear celebration ----
	if (Maze->TransitionMessage.IsEmpty())
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - Maze->TransitionStartTime;
	const float Fade = FMath::Clamp(Elapsed / 0.35f, 0.f, 1.f);
	const float EaseT = FMath::Clamp(Elapsed / 0.6f, 0.f, 1.f);
	const float Ease = 1.f - FMath::Pow(1.f - EaseT, 3.f);

	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.55f * Fade), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const bool bAllClear = Maze->TransitionMessage.Equals(TEXT("ALL CLEAR"));

	// All-clear extra: expanding golden firework rings from the screen center
	if (bAllClear)
	{
		const float CX = Canvas->SizeX * 0.5f;
		const float CY = Canvas->SizeY * 0.45f;
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Radius = FMath::Fmod(Elapsed * 320.f + Ring * 280.f, 850.f);
			const float RingAlpha = FMath::Clamp(1.f - Radius / 850.f, 0.f, 1.f) * Fade;
			for (int32 k = 0; k < 36; ++k)
			{
				const float Ang = k * 10.f * PI / 180.f + Ring * 0.3f;
				const float X = CX + Radius * FMath::Cos(Ang);
				const float Y = CY + Radius * 0.6f * FMath::Sin(Ang);
				FLinearColor C = (k % 2 == 0) ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(1.f, 0.55f, 0.1f);
				C.A = RingAlpha;
				DrawRect(C, X - 3.f, Y - 3.f, 6.f, 6.f);
			}
		}
	}

	const FLinearColor Palette[5] = {
		FLinearColor(1.f, 0.8f, 0.1f), FLinearColor(0.3f, 0.8f, 1.f), FLinearColor(1.f, 0.4f, 0.5f),
		FLinearColor(0.5f, 1.f, 0.5f), FLinearColor(0.8f, 0.5f, 1.f) };
	const int32 ConfettiCount = bAllClear ? 130 : 60;
	for (int32 i = 0; i < ConfettiCount; ++i)
	{
		const float Rand1 = FMath::Frac(FMath::Sin(i * 12.9898f) * 43758.5453f);
		const float Rand2 = FMath::Frac(FMath::Sin(i * 78.233f) * 12543.2f);
		const float X = Rand1 * Canvas->SizeX;
		const float Speed = 180.f + Rand2 * 320.f;
		const float Y = FMath::Fmod(Elapsed * Speed + Rand2 * Canvas->SizeY, Canvas->SizeY + 40.f) - 20.f;
		const float Sway = 18.f * FMath::Sin(Elapsed * 3.f + i);
		const float Size = 5.f + Rand2 * 6.f;
		FLinearColor C = Palette[i % 5];
		C.A = 0.9f * Fade;
		DrawRect(C, X + Sway, Y, Size, Size * (0.5f + Rand1));
	}

	const int32 Size = FMath::RoundToInt(38.f + 24.f * Ease);
	const FVector2D TextS = JSize(Maze->TransitionMessage, Size);
	const float TextW = TextS.X, TextH = TextS.Y;
	const float X = (Canvas->SizeX - TextW) * 0.5f;
	const float Y = Canvas->SizeY * 0.38f;

	FLinearColor TextColor = FLinearColor::White;
	TextColor.A = Fade;
	JText(Maze->TransitionMessage, TextColor, X, Y, Size);

	const float LineW = TextW * 1.1f * Ease;
	FLinearColor LineColor = Accent; LineColor.A = Fade;
	DrawRect(LineColor, (Canvas->SizeX - LineW) * 0.5f, Y + TextH + 14.f, LineW, 4.f);

}
