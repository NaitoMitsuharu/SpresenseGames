#include "TitleGameMode.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "FlightCourse.h"
#include "FlightHUD.h"
#include "FlightPawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

const FName ATitleGameMode::TitleMapName(TEXT("/Game/Maps/Title_Select"));
const FName ATitleGameMode::MazeMapName(TEXT("/Game/Maps/MainMaze"));
const FName ATitleGameMode::FlightMapName(TEXT("/Game/Maps/Flight_Course"));

ATitleGameMode::ATitleGameMode()
{
	DefaultPawnClass = ATitlePawn::StaticClass();
	HUDClass = ATitleHUD::StaticClass();
}

AFlightGameMode::AFlightGameMode()
{
	DefaultPawnClass = AFlightPawn::StaticClass();
	HUDClass = AFlightHUD::StaticClass();
}

// ---------------------------------------------------------------------------

ATitlePawn::ATitlePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	ShipPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipPivot"));
	ShipPivot->SetupAttachment(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocationAndRotation(FVector(-1500.f, -900.f, 420.f), FRotator(-12.f, 30.f, 0.f));
	Camera->FieldOfView = 60.f;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ATitlePawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector(0.f, 0.f, 9000.f));
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		PC->bEnableClickEvents = true;
		PC->bEnableTouchEvents = true;
	}
	FlightEnvironment::Build(this, Root, true);
	FlightModels::BuildShip(this, ShipPivot, Parts);
	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		// Connect early so the source is already known when a game starts.
		Attitude->EnsureSprBox();
	}
}

void ATitlePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds();
	ShipPivot->SetRelativeLocation(FVector(0.f, 0.f, FMath::Sin(T * 0.8f) * 25.f));
	ShipPivot->SetRelativeRotation(FRotator(FMath::Sin(T * 0.6f) * 3.f, 35.f + FMath::Sin(T * 0.25f) * 8.f,
		FMath::Sin(T * 0.45f) * 14.f));
	if (Parts.PlumeGlow)
	{
		Parts.PlumeGlow->SetVectorParameterValue(TEXT("Color"),
			FlightPalette::Mint * (0.5f + 0.08f * FMath::Sin(T * 40.f)));
	}

#if !UE_BUILD_SHIPPING
	static bool bShot = false;
	if (!bShot && T > 3.f && FParse::Param(FCommandLine::Get(), TEXT("TitleAutoShot")))
	{
		bShot = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoShots/00_title.png"), true, false);
	}
	if (bShot && T > 4.f && FParse::Param(FCommandLine::Get(), TEXT("TitleAutoShot")))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::FlightMapName);
	}
#endif

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->WasInputKeyJustPressed(EKeys::One)) { UGameplayStatics::OpenLevel(this, ATitleGameMode::MazeMapName); }
		if (PC->WasInputKeyJustPressed(EKeys::Two)) { UGameplayStatics::OpenLevel(this, ATitleGameMode::FlightMapName); }
	}
}

// ---------------------------------------------------------------------------

void ATitleHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const float T = GetWorld()->GetTimeSeconds();

	// Soft dark band behind the cards so they read over a bright sky.
	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.35f), 0.f, 0.f, W, H);

	TextCentered(TEXT("SPRESENSE GAMES"), FLinearColor::White, W * 0.5f, H * 0.08f, 52.f);
	const FVector2D TS = TextSize(TEXT("SPRESENSE GAMES"), 52.f);
	const float LineW = TS.X * (0.75f + 0.05f * FMath::Sin(T * 2.f));
	DrawRect(Accent, (W - LineW) * 0.5f, H * 0.08f + TS.Y + 8.f * S, LineW, 4.f * S);

	const float CW = FMath::Min(W * 0.32f, 620.f * S);
	const float CH = FMath::Min(H * 0.56f, 560.f * S);
	const float Gap = W * 0.12f;
	const float Y = H * 0.27f;
	DrawCard(TEXT("Maze"), TEXT("迷路"), TEXT("傾けて転がす"), W * 0.5f - Gap * 0.5f - CW, Y, CW, CH, 0);
	DrawCard(TEXT("Flight"), TEXT("フライト"), TEXT("傾けて飛ぶ"), W * 0.5f + Gap * 0.5f, Y, CW, CH, 1);

	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		const FAttitudeState& Att = Attitude->GetState();
		const FString Label = FString::Printf(TEXT("入力  %s"), *UAttitudeInputSubsystem::SourceLabel(Att.Source));
		const FVector2D LS = TextSize(Label, 18.f);
		const float LX = W * 0.5f - (LS.X + 24.f * S) * 0.5f;
		const float LY = Y + CH + 30.f * S;
		Dot(LX + 6.f * S, LY + LS.Y * 0.5f, 5.f * S,
			Att.Source == EAttitudeSource::SprBox ? FLinearColor(0.25f, 0.95f, 0.45f) : Cyan);
		Text(Label, FLinearColor(1.f, 1.f, 1.f, 0.75f), LX + 24.f * S, LY, 18.f);
	}
}

void ATitleHUD::DrawCard(FName Id, const FString& Name, const FString& Sub, float X, float Y, float CW, float CH, int32 Icon)
{
	DrawRect(FLinearColor(0.015f, 0.025f, 0.045f, 0.8f), X, Y, CW, CH);
	DrawRect(PanelEdge, X, Y, CW, 1.f);
	DrawRect(PanelEdge, X, Y + CH - 1.f, CW, 1.f);
	DrawRect(PanelEdge, X, Y, 1.f, CH);
	DrawRect(PanelEdge, X + CW - 1.f, Y, 1.f, CH);
	DrawRect(Accent, X, Y + CH - 6.f * S, CW, 6.f * S);

	const float IconSize = FMath::Min(CW, CH) * 0.5f;
	if (Icon == 0)
	{
		DrawMazeIcon(X + CW * 0.5f, Y + CH * 0.4f, IconSize);
	}
	else
	{
		DrawFlightIcon(X + CW * 0.5f, Y + CH * 0.4f, IconSize);
	}
	TextCentered(Name, FLinearColor::White, X + CW * 0.5f, Y + CH * 0.66f, 52.f);
	TextCentered(Sub, FLinearColor(1.f, 1.f, 1.f, 0.6f), X + CW * 0.5f, Y + CH * 0.7f + 92.f * S, 20.f);
	AddHitBox(FVector2D(X, Y), FVector2D(CW, CH), Id, true);
}

void ATitleHUD::DrawMazeIcon(float CX, float CY, float Size)
{
	// A tiny maze with the ball rolling along its corridor.
	const float U = Size / 6.f;
	const FLinearColor Wall(0.75f, 0.82f, 0.9f, 0.9f);
	const float L = CX - 3.f * U, Tp = CY - 3.f * U;
	auto WallLine = [&](float X0, float Y0, float X1, float Y1)
	{
		DrawLine(L + X0 * U, Tp + Y0 * U, L + X1 * U, Tp + Y1 * U, Wall, 5.f * S);
	};
	WallLine(0, 0, 6, 0); WallLine(6, 0, 6, 6); WallLine(6, 6, 0, 6); WallLine(0, 6, 0, 0);
	WallLine(0, 2, 4, 2); WallLine(2, 4, 6, 4); WallLine(4, 2, 4, 3);
	const float T = FMath::Fmod(GetWorld()->GetTimeSeconds() * 0.35f, 1.f);
	// Path: top corridor left->right, down, middle right->left, down, bottom left->right.
	const FVector2D Pts[] = { { 0.8f, 1.f }, { 5.2f, 1.f }, { 5.2f, 3.f }, { 0.8f, 3.f }, { 0.8f, 5.f }, { 5.2f, 5.f } };
	const float Seg = T * 5.f;
	const int32 I = FMath::Min(FMath::FloorToInt(Seg), 4);
	const FVector2D P = FMath::Lerp(Pts[I], Pts[I + 1], Seg - I);
	DrawRect(Accent, L + 5.f * U, Tp + 4.6f * U, 0.8f * U, 0.8f * U);
	Dot(L + P.X * U, Tp + P.Y * U, 0.38f * U, FLinearColor(0.8f, 0.88f, 1.f));
}

void ATitleHUD::DrawFlightIcon(float CX, float CY, float Size)
{
	// Ship silhouette heading into a ring, banking gently.
	const float T = GetWorld()->GetTimeSeconds();
	const float Bank = FMath::Sin(T * 1.2f) * 0.25f;
	const FLinearColor Ship(0.85f, 0.9f, 0.95f);
	for (int32 i = 0; i < 3; ++i)
	{
		const float K = FMath::Fmod(T * 0.5f + i / 3.f, 1.f);
		Ring(CX, CY - Size * 0.05f, Size * (0.12f + 0.5f * K), Cyan.CopyWithNewOpacity(1.f - K), 3.f * S);
	}
	const FVector2D C(CX, CY + Size * 0.08f);
	auto Rot = [&](float X, float Y)
	{
		return FVector2D(C.X + X * FMath::Cos(Bank) - Y * FMath::Sin(Bank), C.Y + X * FMath::Sin(Bank) + Y * FMath::Cos(Bank));
	};
	const float U = Size * 0.1f;
	const FVector2D WingL = Rot(-4.2f * U, 0.6f * U), WingR = Rot(4.2f * U, 0.6f * U);
	const FVector2D Nose = Rot(0.f, -1.6f * U), Tail = Rot(0.f, 1.2f * U);
	const FVector2D FinL = Rot(-0.8f * U, -1.2f * U), FinR = Rot(0.8f * U, -1.2f * U);
	DrawLine(WingL.X, WingL.Y, WingR.X, WingR.Y, Ship, 6.f * S);
	DrawLine(Nose.X, Nose.Y, Tail.X, Tail.Y, Ship, 10.f * S);
	DrawLine(Tail.X, Tail.Y, FinL.X, FinL.Y, Ship, 4.f * S);
	DrawLine(Tail.X, Tail.Y, FinR.X, FinR.Y, Ship, 4.f * S);
	Dot(Tail.X, Tail.Y + 6.f * S, 5.f * S, Cyan);
}

void ATitleHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (BoxName == TEXT("Maze"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::MazeMapName);
	}
	else if (BoxName == TEXT("Flight"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::FlightMapName);
	}
}
