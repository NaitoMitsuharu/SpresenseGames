#include "MazeHUD.h"

#include "CanvasItem.h"
#include "AttitudeInputSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "ExperienceUI.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "MazeBallPawn.h"
#include "MazeGenerator.h"
#include "MazeSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "TitleGameMode.h"
#include "SprBoxSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	const FLinearColor Accent(0.96f, 0.59f, 0.12f);
	const FLinearColor ToyPaper(1.f, 0.96f, 0.83f);
	const FLinearColor ToyInk(0.055f, 0.12f, 0.085f);
	const FLinearColor ToyMuted(0.16f, 0.24f, 0.18f);
	const FLinearColor ToyGreen(0.12f, 0.40f, 0.22f);
	float UiScale(const UCanvas* Canvas) { return Canvas ? FMath::Clamp(Canvas->SizeY / 1080.f, 0.6f, 1.6f) : 1.f; }

	float LeftHudMargin(const UCanvas* Canvas)
	{
		// Xperia Game Enhancer places its floating control in the upper-left.
		// Reserve a proportional margin while retaining a compact layout on 1080p.
		return Canvas ? FMath::Clamp(Canvas->SizeX * 0.08f, 20.f, 190.f) : 20.f;
	}
}

FSlateFontInfo AMazeHUD::UIFont(int32 Size) const
{
	return FSlateFontInfo(GEngine ? GEngine->GetLargeFont() : nullptr, static_cast<float>(Size));
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
	if (Color.GetLuminance()>0.7f) { Item.EnableShadow(FLinearColor(0.f,0.f,0.f,0.25f*Color.A),FVector2D(1.f,1.f)); }
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
	const float R = FMath::Min(24.f * UiScale(Canvas), H * 0.3f);
	ExperienceUI::RoundRect(Canvas, X, Y + 6.f * UiScale(Canvas), W, H, R, FLinearColor(0.29f,0.22f,0.10f,0.4f));
	ExperienceUI::RoundRect(Canvas, X, Y, W, H, R, ToyPaper);
}

void AMazeHUD::DrawStatusDot(float X, float Y, bool bOn)
{
	const FLinearColor C = bOn ? ToyGreen : ExperienceUI::Coral;
	ExperienceUI::RoundRect(Canvas, X - 3, Y - 3, 16, 16, 8, C.CopyWithNewOpacity(0.12f));
	ExperienceUI::RoundRect(Canvas, X, Y, 10, 10, 5, C);
}

void AMazeHUD::DrawHUD()
{
	Super::DrawHUD();

	AMazeGenerator* Maze = FindMaze();
	if (!Maze || !Canvas || !GEngine)
	{
		return;
	}
	const float Scale = UiScale(Canvas);
#if !UE_BUILD_SHIPPING
	FString Review;
	if (FParse::Value(FCommandLine::Get(), TEXT("MazeUIReview="), Review))
	{
		if (!bReviewSetup && GetWorld()->GetRealTimeSeconds() > 1.f)
		{
			bReviewSetup = true;
			if (Review == TEXT("playing")) { Maze->StartGame(); }
			else if (Review == TEXT("pause")) { Maze->StartGame(); NotifyHitBoxClick(TEXT("Pause")); }
			else if (Review == TEXT("map")) { Maze->StartGame(); bMapOpen = true; }
			else if (Review == TEXT("celebration")) { Maze->StartGame(); Maze->TransitionMessage=TEXT("STAGE CLEAR"); Maze->TransitionStartTime=GetWorld()->GetTimeSeconds()+1.f; }
			else if (Review == TEXT("tutorial")) { Maze->StartTutorial(); }
			else if (Review == TEXT("clear")) { Maze->Flow = EMazeFlow::ClearChoice; }
			else if (Review == TEXT("ranking"))
			{
				Maze->Flow = EMazeFlow::AllClearRanking;
				// Layout fixture only: never write synthetic records to the player's save.
				Maze->SaveGameData = NewObject<UMazeSaveGame>(Maze);
				Maze->SaveGameData->StageBestTimes.SetNum(Maze->GetStageCount());
				Maze->RunStageTimes.SetNum(Maze->GetStageCount());
				for (int32 I = 0; I < Maze->GetStageCount(); ++I)
				{
					Maze->RunStageTimes[I] = 32.f + I * 14;
					Maze->SaveGameData->StageBestTimes[I].Times = { 32.f + I * 14, 39.f + I * 14, 44.f + I * 14, 51.f + I * 14, 63.f + I * 14 };
				}
			}
		}
		if (!bReviewShot && GetWorld()->GetRealTimeSeconds() > 3.f)
		{
			bReviewShot = true;
			if (Review==TEXT("pause"))
			{
				ensureMsgf(UGameplayStatics::IsGamePaused(this),TEXT("Toy review: menu pauses the world"));
				NotifyHitBoxClick(TEXT("Resume"));
				ensureMsgf(!UGameplayStatics::IsGamePaused(this) && !bMenuOpen,TEXT("Toy review: resume releases pause"));
				NotifyHitBoxClick(TEXT("Pause"));
				UE_LOG(LogTemp,Display,TEXT("ToyReview: pause and resume handlers verified"));
			}
			if (const AMazeBallPawn* Ball=Cast<AMazeBallPawn>(GetOwningPawn()))
			{
				UE_LOG(LogTemp,Display,TEXT("ToyCamera: cell %.1f mazeScale %s arm %.1f camera %s ball %s"), Maze->CellSize,*Maze->GetActorScale3D().ToString(),Ball->SpringArm->TargetArmLength,*Ball->Camera->GetComponentLocation().ToString(),*Ball->GetActorLocation().ToString());
			}
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoShots") / (TEXT("ui_maze_") + Review + TEXT(".png")), true, false);
		}
	}
#endif

	if (bMenuOpen) { DrawToyMenu(); return; }
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
		if (bMapOpen) { DrawMinimap(Maze); }
		DrawButton(TEXT("Pause"), TEXT("ひとやすみ"), LeftHudMargin(Canvas) + 340.f*Scale, 34.f*Scale, 204.f*Scale, 64.f*Scale, false);
		DrawButton(TEXT("Map"), bMapOpen ? TEXT("とじる") : TEXT("ちず"), Canvas->SizeX - 200.f*Scale, 34.f*Scale, 164.f*Scale, 64.f*Scale, false);

		DrawJumpFeedback();
		const FVector GoalScreen=Project(Maze->GoalLocation+FVector(0,0,150));
		if (GoalScreen.Z>0 && GoalScreen.X>90*Scale && GoalScreen.X<Canvas->SizeX-90*Scale && GoalScreen.Y>160*Scale && GoalScreen.Y<Canvas->SizeY-160*Scale)
		{
			DrawGlassPanel(GoalScreen.X-60*Scale,GoalScreen.Y-48*Scale,120*Scale,45*Scale);
			JText(TEXT("ゴール"),ToyGreen,GoalScreen.X-42*Scale,GoalScreen.Y-43*Scale,FMath::RoundToInt(24*Scale));
		}
		DrawMessages(Maze);
		DrawTutorialHint(Maze);
		DrawOrientationWarning();
		DrawTouchController();
		if (AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn()); Ball && Ball->IsAwaitingNeutral())
		{
			const FString Msg = TEXT("「ひとやすみ」で もちかたを あわせよう");
			const FVector2D MS = JSize(Msg, 26);
			const float MX = (Canvas->SizeX - MS.X) * 0.5f;
			const float MY = Canvas->SizeY * 0.72f;
			DrawGlassPanel(MX - 24.f, MY - 14.f, MS.X + 48.f, MS.Y + 28.f);
			JText(Msg, ToyInk, MX, MY, 26);
		}
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
	ExperienceUI::RoundRect(Canvas, Knob.X - KnobRadius, Knob.Y - KnobRadius, KnobRadius * 2, KnobRadius * 2, KnobRadius,
		ToyPaper.CopyWithNewOpacity(0.2f));
	Ring(Knob, KnobRadius, FLinearColor(0.28f, 0.88f, 1.f, 0.9f), 4.f);

	const bool bJumpDown = Ball->IsTouchJumpPressed();
	const FLinearColor JumpColor = bJumpDown
		? ToyGreen
		: ToyPaper.CopyWithNewOpacity(0.65f);
	ExperienceUI::RoundRect(Canvas, JumpCenter.X - JumpRadius, JumpCenter.Y - JumpRadius, JumpRadius * 2, JumpRadius * 2, JumpRadius,
		ToyInk.CopyWithNewOpacity(0.5f));
	Ring(JumpCenter, JumpRadius, JumpColor, bJumpDown ? 7.f : 5.f);
	// Two upward chevrons communicate jump without another label.
	const FString JumpLabel = TEXT("ジャンプ");
	const int32 JumpSize = FMath::RoundToInt(19.f*UiScale(Canvas));
	const FVector2D JS = JSize(JumpLabel, JumpSize);
	JText(JumpLabel, ToyInk, JumpCenter.X-JS.X*0.5f, JumpCenter.Y-JumpRadius-30.f*UiScale(Canvas), JumpSize);
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

	if (BoxName == TEXT("Pause"))
	{
		bMenuOpen = true;
		UGameplayStatics::SetGamePaused(this, true);
		return;
	}
	if (BoxName == TEXT("Resume") || BoxName == TEXT("MenuHome") || BoxName == TEXT("MenuReset"))
	{
		bMenuOpen = false; UGameplayStatics::SetGamePaused(this, false);
		if (BoxName == TEXT("MenuHome")) { Maze->GoHome(); }
		if (BoxName == TEXT("MenuReset") && Ball) { Ball->StartCalibration(false); }
		return;
	}
	if (bMenuOpen) { return; }
	if (BoxName == TEXT("Map")) { bMapOpen = !bMapOpen; return; }
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
	else if (BoxName == TEXT("Top"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::TitleMapName);
	}
}

void AMazeHUD::DrawButton(FName Id, const FString& Label, float X, float Y, float W, float H, bool bPrimary)
{
	const bool Hover = ExperienceUI::Hovered(GetOwningPlayerController(), X, Y, W, H);
	const float Lift = 5.f * UiScale(Canvas);
	ExperienceUI::RoundRect(Canvas, X, Y + Lift, W, H, 19.f * UiScale(Canvas), FLinearColor(0.35f,0.27f,0.12f,0.8f));
	ExperienceUI::RoundRect(Canvas, X, Y, W, H, 19.f * UiScale(Canvas),
		bPrimary ? (Hover ? FLinearColor(1.f,0.79f,0.32f) : Accent) : ToyPaper);
	const float Scale = UiScale(Canvas);
	const int32 Size = FMath::RoundToInt((H / Scale > 62.f ? 26.f : 20.f) * Scale);
	const FVector2D TS = JSize(Label, Size);
	JText(Label,
		ToyInk,
		X + (W - TS.X) * 0.5f, Y + (H - TS.Y) * 0.5f, Size);

	AddHitBox(FVector2D(X, Y), FVector2D(W, H), Id, true);
}

void AMazeHUD::DrawHomeScreen(AMazeGenerator* Maze)
{
    using namespace ExperienceUI;
    const float Scale = UiScale(Canvas), Width = Canvas->SizeX, Height = Canvas->SizeY;
    const float Left = LeftHudMargin(Canvas), Top = Height * 0.19f;
    Gradient(Canvas, 0, 0, Width, Height, ToyPaper.CopyWithNewOpacity(0.98f), ToyPaper.CopyWithNewOpacity(0.05f), true);
    Gradient(Canvas, 0, Height * 0.7f, Width, Height * 0.3f, FLinearColor(0,0,0,0), ToyPaper.CopyWithNewOpacity(0.35f));
    JText(TEXT("ころころ ・ からくり"), ToyGreen, Left, Top, FMath::RoundToInt(17 * Scale));
    JText(TEXT("めいろ"), ToyInk, Left - 2 * Scale, Top + 38 * Scale, FMath::RoundToInt(80 * Scale));
    JText(TEXT("ころがして、ジャンプして、ゴール！"), ToyInk, Left, Top + 150 * Scale, FMath::RoundToInt(25 * Scale));
    JText(TEXT("6つの めいろで あそぼう。"), ToyMuted, Left, Top + 195 * Scale, FMath::RoundToInt(18 * Scale));
    DrawButton(TEXT("Top"), TEXT("トップ"), Width - 190 * Scale, 60 * Scale, 124 * Scale, 54 * Scale, false);
    const float ButtonY = Height * 0.55f;
    DrawButton(TEXT("Start"), TEXT("はじめる"), Left, ButtonY, 390 * Scale, 78 * Scale, true);
    DrawButton(TEXT("Tutorial"), TEXT("あそびかた"), Left, ButtonY + 96 * Scale, 188 * Scale, 58 * Scale, false);
    DrawButton(TEXT("Calibrate"), TEXT("もちかた"), Left + 202 * Scale, ButtonY + 96 * Scale, 188 * Scale, 58 * Scale, false);
    const EAttitudeSource Input = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>()->GetState().Source;
    const FString InputName = Input == EAttitudeSource::SprBox ? TEXT("箱を傾けて操作") : Input == EAttitudeSource::DeviceMotion ? TEXT("端末を傾けて操作") :
        Input == EAttitudeSource::Keyboard ? TEXT("キーボードで操作") : TEXT("画面のスティックで操作");
    const float StatusY = ButtonY + 184 * Scale;
    RoundRect(Canvas, Left + 2 * Scale, StatusY + 8 * Scale, 7 * Scale, 7 * Scale, 3.5f * Scale, ToyGreen);
    JText(InputName, ToyMuted, Left + 22 * Scale, StatusY, FMath::RoundToInt(17 * Scale));
    JText(TEXT("小さな傾きで、少しずつ。"), ToyMuted, Width * 0.66f, Height * 0.86f, FMath::RoundToInt(18 * Scale));
}

void AMazeHUD::DrawClearChoice(AMazeGenerator* Maze)
{
    using namespace ExperienceUI;
    const float Scale = UiScale(Canvas), CX = Canvas->SizeX * 0.5f, CY = Canvas->SizeY * 0.5f;
    DrawRect(ToyInk.CopyWithNewOpacity(0.72f), 0, 0, Canvas->SizeX, Canvas->SizeY);
    DrawGlassPanel(CX - 300 * Scale, CY - 238 * Scale, 600 * Scale, 476 * Scale);
    const FString Number = FString::Printf(TEXT("ステージ %d / %d"), Maze->CurrentStage + 1, Maze->GetStageCount());
    const FVector2D NS = JSize(Number, FMath::RoundToInt(17 * Scale));
    JText(Number, ToyGreen, CX - NS.X * 0.5f, CY - 191 * Scale, FMath::RoundToInt(17 * Scale));
    const FString Title = TEXT("やったね！");
    const FVector2D TS = JSize(Title, FMath::RoundToInt(56 * Scale));
    JText(Title, ToyGreen, CX - TS.X * 0.5f, CY - 135 * Scale, FMath::RoundToInt(56 * Scale));
    const FString Stage = Maze->GetStageName();
    const FVector2D SS = JSize(Stage, FMath::RoundToInt(21 * Scale));
    JText(Stage, ToyMuted, CX - SS.X * 0.5f, CY - 43 * Scale, FMath::RoundToInt(21 * Scale));
    DrawButton(TEXT("Next"), TEXT("つぎへ"), CX - 204 * Scale, CY + 27 * Scale, 408 * Scale, 72 * Scale, true);
    DrawButton(TEXT("Home"), TEXT("もどる"), CX - 204 * Scale, CY + 117 * Scale, 408 * Scale, 58 * Scale, false);
}

void AMazeHUD::DrawAllClearRanking(AMazeGenerator* Maze)
{
    using namespace ExperienceUI;
    const float Scale = UiScale(Canvas), Width = Canvas->SizeX, Height = Canvas->SizeY;
    DrawRect(ToyPaper, 0, 0, Width, Height);
    const FString Heading = TEXT("すべてのステージをクリア");
    const int32 HeadingFont = FMath::RoundToInt(42 * Scale);
    const FVector2D HS = JSize(Heading, HeadingFont);
    JText(Heading, ToyInk, (Width - HS.X) * 0.5f, Height * 0.07f, HeadingFont);
    const FString Subtitle = TEXT("ステージ別の記録");
    const FVector2D SS = JSize(Subtitle, FMath::RoundToInt(18 * Scale));
    JText(Subtitle, ToyGreen, (Width - SS.X) * 0.5f, Height * 0.07f + 68 * Scale, FMath::RoundToInt(18 * Scale));
    const int32 Count = Maze->GetStageCount(), Columns = Count > 6 ? 3 : 2, Rows = FMath::DivideAndRoundUp(Count, Columns);
    const float TotalW = FMath::Min(Width * 0.84f, 1600 * Scale), Gap = 22 * Scale;
    const float CardW = (TotalW - Gap * (Columns - 1)) / Columns;
    const float ListTop = Height * 0.23f, RowH = (Height * 0.85f - ListTop) / FMath::Max(1, Rows);
    auto TimeLabel = [](float Seconds)
    {
        if (Seconds < 0) { return FString(TEXT("--:--.--")); }
        const int32 Centi = FMath::RoundToInt(Seconds * 100);
        return FString::Printf(TEXT("%02d:%02d.%02d"), Centi / 6000, (Centi / 100) % 60, Centi % 100);
    };
    for (int32 I = 0; I < Count; ++I)
    {
        const float X = (Width - TotalW) * 0.5f + (I % Columns) * (CardW + Gap), Y = ListTop + (I / Columns) * RowH;
        DrawGlassPanel(X, Y, CardW, RowH - 14 * Scale);
        const int32 Font = FMath::RoundToInt(18 * Scale);
        const FString Stage = FString::Printf(TEXT("STAGE %02d"), I + 1);
        JText(Stage, ToyMuted, X + 22 * Scale, Y + 16 * Scale, Font);
        const float Run = Maze->RunStageTimes.IsValidIndex(I) ? Maze->RunStageTimes[I] : -1.f;
        const FString Current = TEXT("今回  ") + TimeLabel(Run);
        const FVector2D CS = JSize(Current, Font);
        JText(Current, ToyInk, X + CardW - CS.X - 22 * Scale, Y + 16 * Scale, Font);
        if (Maze->SaveGameData && Maze->SaveGameData->StageBestTimes.IsValidIndex(I))
        {
            const TArray<float>& Times = Maze->SaveGameData->StageBestTimes[I].Times;
            if (Times.Num() > 0 && Run >= 0 && FMath::IsNearlyEqual(Times[0], Run, 0.001f))
            {
                RoundRect(Canvas, X + 132 * Scale, Y + 17 * Scale, 54 * Scale, 22 * Scale, 11 * Scale, ToyGreen);
                JText(TEXT("BEST"), ToyPaper, X + 141 * Scale, Y + 20 * Scale, FMath::RoundToInt(12 * Scale));
            }
            const float CellW = (CardW - 44 * Scale) / 3;
            for (int32 Rank = 0; Rank < Times.Num(); ++Rank)
            {
                const bool CurrentRun = Run >= 0 && FMath::IsNearlyEqual(Times[Rank], Run, 0.001f);
                const FString Record = FString::Printf(TEXT("%d  %s"), Rank + 1, *TimeLabel(Times[Rank]));
                JText(Record, CurrentRun ? ToyGreen : ToyMuted, X + 22 * Scale + (Rank % 3) * CellW,
                    Y + 64 * Scale + (Rank / 3) * 42 * Scale, FMath::RoundToInt(16 * Scale));
            }
        }
    }
    DrawButton(TEXT("Home"), TEXT("もどる"), Width * 0.5f - 170 * Scale, Height - 104 * Scale, 340 * Scale, 66 * Scale, true);
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
	const float PH = Phase == 2 ? 130.f : 106.f;
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

		// Live "is real board data actually arriving" readout, so a dead
		// stream is obvious well before the 5s step ends and silently fails.
		const bool bFlowing = Ball->IsCalibDataFlowing();
		const FString DataMsg = bFlowing ? TEXT("データ受信中") : TEXT("データが届いていません");
		const FLinearColor DataColor = bFlowing
			? FLinearColor(0.4f, 1.f, 0.55f)
			: FLinearColor(1.f, 0.35f, 0.3f);
		DrawStatusDot(CX - BarW * 0.5f, CY + TS.Y + 30.f, bFlowing);
		JText(DataMsg, DataColor, CX - BarW * 0.5f + 18.f, CY + TS.Y + 24.f, 16);
	}
}

void AMazeHUD::DrawAttitudeIndicator()
{
    const AMazeBallPawn* Ball = Cast<AMazeBallPawn>(GetOwningPawn());
    if (!Ball) { return; }
    using namespace ExperienceUI;
    const float Scale = UiScale(Canvas), X = LeftHudMargin(Canvas), Y = 132 * Scale;
    DrawGlassPanel(X, Y, 218 * Scale, 82 * Scale);
    JText(TEXT("傾き"), ToyMuted, X + 20 * Scale, Y + 26 * Scale, FMath::RoundToInt(18 * Scale));
    const float CX = X + 154 * Scale, CY = Y + 41 * Scale;
    RoundRect(Canvas, CX - 30 * Scale, CY - 2 * Scale, 60 * Scale, 4 * Scale, 2 * Scale, ToyPaper.CopyWithNewOpacity(0.18f));
    RoundRect(Canvas, CX - 2 * Scale, CY - 24 * Scale, 4 * Scale, 48 * Scale, 2 * Scale, ToyPaper.CopyWithNewOpacity(0.18f));
    const float DX = FMath::Clamp(Ball->CurrentRollDeg / 30.f, -1.f, 1.f) * 27 * Scale;
    const float DY = FMath::Clamp(Ball->CurrentPitchDeg / 30.f, -1.f, 1.f) * 21 * Scale;
    RoundRect(Canvas, CX + DX - 5 * Scale, CY + DY - 5 * Scale, 10 * Scale, 10 * Scale, 5 * Scale, Ball->bOrientationWarning ? Coral : ToyGreen);
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
	const float Scale = UiScale(Canvas);
	const float Cell = MinimapSize * 0.8f * Scale / FMath::Max(NumRows, NumCols);
	const float MapW = Cell * NumRows;
	const float MapH = Cell * NumCols;
	const float Pad = 18.f * Scale;
	const float PX = Canvas->SizeX - MapW - MinimapMargin * Scale - Pad * 2.f;
	const float PY = 122.f * Scale;
	const float X0 = PX + Pad;
	const float Y0 = PY + Pad + 30 * Scale;

	DrawGlassPanel(PX, PY, MapW + Pad * 2.f, MapH + Pad * 2.f + 30 * Scale);
	JText(TEXT("ちず"), ToyMuted, PX + Pad, PY + 12 * Scale, FMath::RoundToInt(15 * Scale));

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
			ExperienceUI::RoundRect(Canvas, Px - R - 3, Py - R - 3, (R + 3) * 2, (R + 3) * 2, R + 3, ToyGreen.CopyWithNewOpacity(0.2f));
			ExperienceUI::RoundRect(Canvas, Px - R, Py - R, R * 2, R * 2, R, ToyPaper);
			ExperienceUI::RoundRect(Canvas, Px - R * 0.5f, Py - R * 0.5f, R, R, R * 0.5f, ToyGreen);
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
	JText(Hint, ToyInk, ContentX + IconBox + Gap, PY + (PH - TS.Y) * 0.5f, Size);
}

void AMazeHUD::DrawMessages(AMazeGenerator* Maze)
{

    {
        using namespace ExperienceUI;
        const float Scale = UiScale(Canvas), X = LeftHudMargin(Canvas), Y = 30 * Scale;
        DrawGlassPanel(X, Y, 300 * Scale, 108 * Scale);
        const FString Stage = Maze->bTutorialMode ? FString::Printf(TEXT("れんしゅう %d / %d"), Maze->CurrentStage + 1, Maze->GetStageCount()) :
            FString::Printf(TEXT("ステージ %d / %d"), Maze->CurrentStage + 1, Maze->GetStageCount());
        JText(Stage, ToyGreen, X + 20 * Scale, Y + 12 * Scale, FMath::RoundToInt(15 * Scale));
        JText(Maze->GetStageName(), ToyInk, X + 20 * Scale, Y + 36 * Scale, FMath::RoundToInt(23 * Scale));
        if (!Maze->bTutorialMode) { for (int32 I=0; I<Maze->GetStageCount(); ++I) {
            RoundRect(Canvas, X+(20+I*24)*Scale, Y+83*Scale, 14*Scale, 9*Scale, 4.5f*Scale, I<=Maze->CurrentStage?ToyGreen:ToyGreen.CopyWithNewOpacity(0.15f));
        } }
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
	const FString Cheer = bAllClear ? TEXT("ぜんぶ できたね！") : TEXT("やったね！");
	const FVector2D CheerSize = JSize(Cheer, Size);
	JText(Cheer, TextColor, (Canvas->SizeX-CheerSize.X)*0.5f, Y, Size);

	const float LineW = TextW * 1.1f * Ease;
	FLinearColor LineColor = Accent; LineColor.A = Fade;
	DrawRect(LineColor, (Canvas->SizeX - LineW) * 0.5f, Y + TextH + 14.f, LineW, 4.f);

}

void AMazeHUD::DrawToyMenu()
{
    const float S=UiScale(Canvas), CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f;
    DrawRect(FLinearColor(0.04f,0.07f,0.04f,0.45f),0,0,Canvas->SizeX,Canvas->SizeY);
    DrawGlassPanel(CX-290*S,CY-250*S,580*S,500*S);
    const FString Heading=TEXT("ひとやすみ");
    const int32 Size=FMath::RoundToInt(44*S);
    JText(Heading,ToyInk,CX-JSize(Heading,Size).X*0.5f,CY-194*S,Size);
    DrawButton(TEXT("Resume"),TEXT("つづける"),CX-220*S,CY-88*S,440*S,76*S,true);
    DrawButton(TEXT("MenuReset"),TEXT("もちかたを あわせる"),CX-220*S,CY+12*S,440*S,64*S,false);
    DrawButton(TEXT("MenuHome"),TEXT("さいしょに もどる"),CX-220*S,CY+104*S,440*S,64*S,false);
}
