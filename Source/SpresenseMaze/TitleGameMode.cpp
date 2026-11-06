#include "TitleGameMode.h"
#include "LightBoxPointCloud.h"
#include "LightBoxStyle.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "ExperienceUI.h"
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
const FName ATitleGameMode::LightBoxMapName(TEXT("/Game/Maps/LightBox_World"));

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
	const FVector View(1500.f, -1500.f, 900.f);
	Camera->SetRelativeLocationAndRotation(View, (FVector(0, 0, 30) - View).Rotation());
	Camera->FieldOfView = 43.f;
	MazePreview = CreateDefaultSubobject<USceneComponent>(TEXT("MazePreview"));
	MazePreview->SetupAttachment(Root);
	MazePreview->SetRelativeLocation(FVector(-260.f, -260.f, 0.f));
	MazePreview->SetRelativeScale3D(FVector(1.6f));
	MazePreview->SetVisibility(false, true);
	LightBoxPreview = CreateDefaultSubobject<USceneComponent>(TEXT("LightBoxPreview"));
	LightBoxPreview->SetupAttachment(Root);
	LightBoxPreview->SetRelativeLocation(FVector(-260.f, -260.f, 0.f));
	LightBoxPreview->SetRelativeScale3D(FVector(1.6f));
	LightBoxPreview->SetVisibility(false, true);
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
	// A dedicated studio-lit backdrop keeps the white airframe readable and leaves
	// quiet space for the menu, without changing the flight game's environment.
	for (int32 I = 0; I < 2; ++I)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(this, I == 0 ? TEXT("TitleKey") : TEXT("TitleFill"));
		Light->SetupAttachment(Root); Light->SetMobility(EComponentMobility::Movable);
		Light->SetWorldRotation(I == 0 ? FRotator(-40, -60, 0) : FRotator(-15, 130, 0));
		Light->SetIntensity(I == 0 ? 4.5f : 1.4f);
		Light->SetForwardShadingPriority(I == 0 ? 2 : 0);
		Light->SetLightColor(I == 0 ? FLinearColor(0.94f, 0.98f, 1.f) : FLinearColor(0.3f, 0.7f, 0.65f));
		Light->SetCastShadows(false); Light->RegisterComponent(); AddInstanceComponent(Light);
	}
	UPostProcessComponent* Post = NewObject<UPostProcessComponent>(this, TEXT("TitlePost"));
	Post->SetupAttachment(Root); Post->bUnbound = true;
	Post->Settings.bOverride_BloomIntensity = true; Post->Settings.BloomIntensity = 0.3f;
	Post->Settings.bOverride_VignetteIntensity = true; Post->Settings.VignetteIntensity = 0.32f;
	Post->RegisterComponent(); AddInstanceComponent(Post);
	FlightModels::BuildShip(this, ShipPivot, Parts);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* MazeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeTile.M_MazeTile"));
	UMaterialInterface* BallMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ball.M_Ball"));
	auto AddPreviewMesh = [this](USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh,
		const FVector& Location, const FVector& Scale, UMaterialInterface* Material)
	{
		if (!Parent || !Mesh) { return; }
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, FName(Name));
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(Mesh);
		Component->SetRelativeLocation(Location);
		Component->SetRelativeScale3D(Scale);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Material) { Component->SetMaterial(0, Material); }
		Component->RegisterComponent();
		AddInstanceComponent(Component);
	};
	AddPreviewMesh(MazePreview, TEXT("MazeBoardPreview"), Cube, FVector(0, 0, 0), FVector(3.8f, 3.8f, .12f), MazeMaterial);
	const FVector WallScale(0.12f, 0.12f, 0.78f);
	const FVector WallLocations[] = {
		{-130,-70,43},{-60,-130,43},{10,-70,43},{80,-130,43},{150,-70,43},
		{-130,70,43},{-60,130,43},{10,70,43},{80,130,43},{150,70,43},
		{-70,0,43},{70,0,43},{0,-130,43},{0,130,43}
	};
	for (int32 I = 0; I < UE_ARRAY_COUNT(WallLocations); ++I)
	{
		AddPreviewMesh(MazePreview, *FString::Printf(TEXT("MazeWallPreview%d"), I), Cube, WallLocations[I], WallScale, MazeMaterial);
	}
	AddPreviewMesh(MazePreview, TEXT("MazeBallPreview"), Sphere, FVector(-125, -125, 48), FVector(.34f), BallMaterial);
    LightPoints=NewObject<ULightBoxPointCloud>(this,TEXT("LightBoxParticlePreview"));LightPoints->SetupAttachment(LightBoxPreview);LightPoints->RegisterComponent();AddInstanceComponent(LightPoints);
    FLightBoxStyle Preview;Preview.Count=0;Preview.PointCount=24576;Preview.Background=FLinearColor::Black;Preview.AssembleSeconds=2;
    FLightBoxObject Orb;Orb.Id=TEXT("light");Orb.Shape=TEXT("sphere");Orb.Size=FVector(140);Orb.Position=FVector(0,0,115);Orb.Color=FLinearColor(.1,.65,.8);Orb.Emission=1;Preview.Objects.Add(Orb);
    FLightBoxObject Ring=Orb;Ring.Id=TEXT("ring");Ring.Shape=TEXT("torus");Ring.Size=FVector(230,230,10);Ring.Radius=3;Ring.Color=FLinearColor(.8,.4,.12);Ring.Rotation=FVector(55,20,0);Preview.Objects.Add(Ring);LightPoints->Assemble(Preview);
	MazePreview->SetVisibility(false, true);
	LightBoxPreview->SetVisibility(false, true);
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
	ShipPivot->SetRelativeLocation(FVector(-260.f, -260.f, 45.f + FMath::Sin(T * 0.65f) * 16.f));
	ShipPivot->SetRelativeRotation(FRotator(2.f + FMath::Sin(T * 0.4f) * 2.f, -12.f + FMath::Sin(T * 0.22f) * 6.f,
		-6.f + FMath::Sin(T * 0.35f) * 4.f));
	MazePreview->SetRelativeLocation(FVector(-260.f, -260.f, FMath::Sin(T * 0.7f) * 14.f));
	MazePreview->SetRelativeRotation(FRotator(0.f, T * 12.f, 0.f));
	LightBoxPreview->SetRelativeLocation(FVector(-260.f, -260.f, FMath::Sin(T * 0.8f) * 14.f));
	LightBoxPreview->SetRelativeRotation(FRotator(0.f, T * 10.f, 0.f));
	if(LightPoints){LightPoints->SetVisibility(LightBoxPreview->IsVisible()&&!LightPoints->IsPreparing());if(LightBoxPreview->IsVisible())LightPoints->Advance(DeltaSeconds,FVector2D::ZeroVector,Camera->GetRightVector(),Camera->GetUpVector());}
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
		if (PC->WasInputKeyJustPressed(EKeys::Three)) { UGameplayStatics::OpenLevel(this, ATitleGameMode::LightBoxMapName); }
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
	using namespace ExperienceUI;
	const float Left = SafeLeft();
	Gradient(Canvas, 0, 0, W, H, FLinearColor(0.014f, 0.032f, 0.044f, 0.94f), FLinearColor(0.014f, 0.032f, 0.044f, 0.03f), true);
	Gradient(Canvas, 0, H * 0.72f, W, H * 0.28f, FLinearColor(0, 0, 0, 0), FLinearColor(0.005f, 0.013f, 0.02f, 0.95f));
	RoundRect(Canvas, Left, H * 0.08f + 6 * S, 6 * S, 24 * S, 3 * S, Mint);
	Text(TEXT("SPRESENSE"), Paper, Left + 22 * S, H * 0.08f, 26, false);
	const float CW = FMath::Min(W * 0.4f, 640.f * S);
	const float CH = 104 * S, Gap = 14 * S, Y = H * 0.31f;
	DrawCard(TEXT("Maze"), TEXT("迷路"), Left, Y, CW, CH, 0);
	DrawCard(TEXT("Flight"), TEXT("フライト"), Left, Y + CH + Gap, CW, CH, 1);
	DrawCard(TEXT("LightBox"), TEXT("ひかりのアトリエ"), Left, Y + (CH + Gap) * 2, CW, CH, 2);
	DrawRect(Paper.CopyWithNewOpacity(0.1f), Left, H * 0.89f, W - Left - 64 * S, S);

	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		const FAttitudeState& Att = Attitude->GetState();
		const FString Label = Att.Source == EAttitudeSource::SprBox ? TEXT("箱とつながっています") :
			Att.Source == EAttitudeSource::DeviceMotion ? TEXT("端末を傾けて操作できます") :
			Att.Source == EAttitudeSource::Keyboard ? TEXT("キーボードで操作できます") : TEXT("箱を接続してください");
		const float LY = H * 0.925f;
		RoundRect(Canvas, Left, LY + 8 * S, 7 * S, 7 * S, 3.5f * S, Att.bAvailable ? Mint : Muted);
		Text(Label, Muted, Left + 20 * S, LY, 17, false);
		Text(TEXT("体験をえらんでください"), Muted.CopyWithNewOpacity(0.7f), W - 340 * S, LY, 17, false);
	}
}

void ATitleHUD::DrawCard(FName Id, const FString& Name, float X, float Y, float CW, float CH, int32 Icon)
{
	using namespace ExperienceUI;
	const bool Hover = Hovered(GetOwningPlayerController(), X, Y, CW, CH);
	const bool Selected = SelectedId == Id;
	const bool Active = Hover || Selected;
	RoundRect(Canvas, X, Y, CW, CH, 22 * S, Paper.CopyWithNewOpacity(Active ? 0.22f : 0.08f));
	RoundRect(Canvas, X + S, Y + S, CW - 2 * S, CH - 2 * S, 21 * S,
		Active ? FLinearColor(0.035f, 0.11f, 0.115f, 0.96f) : Ink.CopyWithNewOpacity(0.82f));
	const float IconSize = 36 * S, ICX = X + 42 * S, ICY = Y + CH * 0.5f;
	if (Icon == 0)
	{
		DrawMazeIcon(ICX, ICY, IconSize);
	}
	else if (Icon == 1)
	{
		DrawFlightIcon(ICX, ICY, IconSize);
	}
	else { DrawLightBoxIcon(ICX, ICY, IconSize); }
	const float TextX = X + (Active ? 88 : 84) * S;
	Text(Name, Paper, TextX, Y + 33 * S, 31, false);
	const float AX = X + CW - 44 * S, AY = Y + CH * 0.5f;
	RoundRect(Canvas, AX - 18 * S, AY - 18 * S, 36 * S, 36 * S, 18 * S, Active ? Mint : Paper.CopyWithNewOpacity(0.07f));
	const FLinearColor Arrow = Active ? Ink : Paper;
	DrawLine(AX - 5 * S, AY, AX + 6 * S, AY, Arrow, 1.6f * S);
	DrawLine(AX + 1 * S, AY - 5 * S, AX + 6 * S, AY, Arrow, 1.6f * S);
	DrawLine(AX + 1 * S, AY + 5 * S, AX + 6 * S, AY, Arrow, 1.6f * S);
	AddHitBox(FVector2D(X, Y), FVector2D(CW, CH), Id, true);
}

void ATitleHUD::DrawMazeIcon(float CX, float CY, float Size)
{
    const float U = Size / 6.f;
    const FLinearColor C = ExperienceUI::Paper.CopyWithNewOpacity(0.8f);
    auto Wall = [&](float X0, float Y0, float X1, float Y1)
    { DrawLine(CX + X0*U, CY + Y0*U, CX + X1*U, CY + Y1*U, C, 1.7f*S); };
    Wall(-3,-3,3,-3); Wall(3,-3,3,3); Wall(3,3,-3,3); Wall(-3,3,-3,-3);
    Wall(-3,-1,1,-1); Wall(-1,1,3,1); Wall(1,-1,1,0);
    Dot(CX-2*U,CY-2*U,1.8f*S,ExperienceUI::Mint);
}

void ATitleHUD::DrawFlightIcon(float CX, float CY, float Size)
{
    const FVector2D P[] = { {CX,CY-Size*0.46f}, {CX-Size*0.43f,CY+Size*0.26f},
        {CX,CY+Size*0.1f}, {CX+Size*0.43f,CY+Size*0.26f} };
    for (int32 I=0; I<4; ++I)
    { DrawLine(P[I].X,P[I].Y,P[(I+1)%4].X,P[(I+1)%4].Y,ExperienceUI::Paper,1.7f*S); }
    DrawLine(CX,CY+Size*0.23f,CX,CY+Size*0.42f,ExperienceUI::Mint,2*S);
}

void ATitleHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (SelectedId != BoxName)
	{
		SetPreview(BoxName);
		return;
	}
	if (BoxName == TEXT("Maze"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::MazeMapName);
	}
	else if (BoxName == TEXT("Flight"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::FlightMapName);
	}
	else if (BoxName == TEXT("LightBox"))
	{
		UGameplayStatics::OpenLevel(this, ATitleGameMode::LightBoxMapName);
	}
}

void ATitleHUD::SetPreview(FName Id)
{
	SelectedId = Id;
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (ATitlePawn* TitlePawn = Cast<ATitlePawn>(PC->GetPawn()))
		{
			const bool bMaze = Id == TEXT("Maze");
			const bool bLightBox = Id == TEXT("LightBox");
			TitlePawn->ShipPivot->SetVisibility(!bMaze && !bLightBox, true);
			TitlePawn->MazePreview->SetVisibility(bMaze, true);
			TitlePawn->LightBoxPreview->SetVisibility(bLightBox, true);
		}
	}
}

void ATitleHUD::DrawLightBoxIcon(float CX, float CY, float Size)
{
    const float T = GetWorld()->GetTimeSeconds();
    for (int32 I=0; I<8; ++I)
    {
        const float A=I*2.4f+T*0.2f, R=Size*0.4f*FMath::Sqrt((I+1.f)/9.f);
        Dot(CX+FMath::Cos(A)*R,CY+FMath::Sin(A)*R, (I%3==0?1.8f:1.2f)*S,
            I==0?ExperienceUI::Paper:ExperienceUI::Mint);
    }
}
