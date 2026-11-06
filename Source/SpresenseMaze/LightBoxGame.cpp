#include "LightBoxGame.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "LightBoxDirector.h"
#include "LightBoxMotion.h"
#include "LightBoxBatchReview.h"
#include "LightBoxPointCloud.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "TitleGameMode.h"
#include "UnrealClient.h"

ALightBoxGameMode::ALightBoxGameMode()
{
	DefaultPawnClass = ALightBoxPawn::StaticClass(); HUDClass = ALightBoxHUD::StaticClass();
}

ALightBoxPawn::ALightBoxPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene")); RootComponent = SceneRoot;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(SceneRoot);
	Camera->SetRelativeLocation(FVector(0, -1150, 740));
	Camera->SetRelativeRotation((FVector(0, 0, 140) - Camera->GetRelativeLocation()).Rotation());
	Camera->FieldOfView = 58;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	PointCloud=CreateDefaultSubobject<ULightBoxPointCloud>(TEXT("ParticleSculpture"));PointCloud->SetupAttachment(SceneRoot);
}

void ALightBoxPawn::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true; PC->bEnableClickEvents = true; PC->bEnableTouchEvents = true;
	}
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	Attitude->EnsureSprBox(); Attitude->ResetNeutral(); SourceEpoch = Attitude->GetSourceEpoch();
	LastYaw = Attitude->GetState().YawDeg;
	ConfigureParticles();
	bool bReview=false;
#if !UE_BUILD_SHIPPING
	// Reviews and benchmarks must stay deterministic: no microphone, light or magnet input.
	for(const TCHAR* Key:{TEXT("LightBoxBatch="),TEXT("LightBoxScene="),TEXT("LightBoxReviewTime="),TEXT("PointCloudPhase="),TEXT("PointCloudReport=")})
		bReview|=FCString::Stristr(FCommandLine::Get(),Key)!=nullptr;
	bReview|=FParse::Param(FCommandLine::Get(),TEXT("LightBoxAutoShot"))||FParse::Param(FCommandLine::Get(),TEXT("LightBoxNoSensors"));
#endif
	if(!bReview){Sensors.Start();GetGameInstance()->GetSubsystem<ULightBoxDirector>()->BeginReadiness();}
#if !UE_BUILD_SHIPPING
	FString BatchDirectory;
	if(FParse::Value(FCommandLine::Get(),TEXT("LightBoxBatch="),BatchDirectory))BatchReview=MakeShared<FLightBoxBatchReview>(BatchDirectory);
	FString SceneFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("LightBoxScene="), SceneFile))
	{
		FString Json, Error; FLightBoxStyle Review;
		if (FFileHelper::LoadFileToString(Json, *SceneFile) && FLightBoxStyle::Parse(Json, Review, Error))
		{ GetGameInstance()->GetSubsystem<ULightBoxDirector>()->ApplyScene(Review); }
		else UE_LOG(LogTemp,Warning,TEXT("LightBox review scene rejected: %s"),*Error);
	}
#endif
}

void ALightBoxPawn::ConfigureParticles()
{
	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this, TEXT("LightPostProcess"));
	PostProcess = PP;
	PP->SetupAttachment(SceneRoot); PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMethod=true;PP->Settings.AutoExposureMethod=AEM_Manual;
	PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP->Settings.AutoExposureApplyPhysicalCameraExposure=false;
	PP->Settings.bOverride_AutoExposureBias=true;PP->Settings.AutoExposureBias=0;
	PP->Settings.bOverride_AmbientOcclusionIntensity=true;PP->Settings.AmbientOcclusionIntensity=.65f;
	PP->Settings.bOverride_AmbientOcclusionRadius=true;PP->Settings.AmbientOcclusionRadius=35;
	PP->Settings.bOverride_BloomIntensity = true; PP->Settings.BloomIntensity = 0.7f;
	PP->Settings.bOverride_VignetteIntensity = true; PP->Settings.VignetteIntensity = 0.35f;
	PP->RegisterComponent();
}

void ALightBoxPawn::ApplyScene(const FLightBoxStyle& Style)
{
	const FString Content=Style.RenderContentJson();
    const FString CameraContent=Style.CameraContentJson();const bool NewCamera=CameraContent!=AppliedCamera;AppliedCamera=CameraContent;
	if(Content==AppliedContent){
		if(!NewCamera)return;
		// Changing only the camera must not restart flight, reset live motes or scatter a formed sculpture.
		ViewAngles=FVector2D::ZeroVector;ViewDistance=1;ViewOrbitSpeed=Style.CameraOrbit;bViewDragging=false;LastPinchDistance=0;
		CameraStart=Camera->GetRelativeTransform();CameraBlendAge=0;CameraBlendSeconds=.6f;return;
	}
	AppliedContent=Content;
    if(Simulation.Motes.IsEmpty()||Simulation.Motes.Num()!=Style.Count)Simulation.Reset(Style);
    PointCloud->Assemble(Style);ObjectBurst=0;
    BaseBloom=Style.Bloom*.55f;if(PostProcess)PostProcess->Settings.BloomIntensity=BaseBloom;
    int32 Width=16,Height=9;if(auto* PC=Cast<APlayerController>(GetController()))PC->GetViewportSize(Width,Height);
    LastAspect=float(Width)/FMath::Max(1,Height);const FBox NextBounds=FLightBoxMotionSystem::Bounds(Style);
    const bool KeepView=!NewCamera&&CameraBounds.IsValid&&NextBounds.IsValid&&CameraBounds.Min.Equals(NextBounds.Min,.01)&&CameraBounds.Max.Equals(NextBounds.Max,.01);
    CameraBounds=NextBounds;if(KeepView)return;
    ViewAngles=FVector2D::ZeroVector;ViewDistance=1;ViewOrbitSpeed=Style.CameraOrbit;bViewDragging=false;LastPinchDistance=0;
    CameraStart=Camera->GetRelativeTransform();CameraTarget=FLightBoxMotionSystem::CameraForBounds(Style,CameraBounds,LastAspect);CameraBlendAge=0;CameraBlendSeconds=Style.AssembleSeconds;
}

void ALightBoxPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	Sensors.Stop();
	if (GetGameInstance()) { GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Cancel(); }
	Super::EndPlay(Reason);
}

void ALightBoxPawn::Pulse()
{
	Simulation.Impulse(GetGameInstance()->GetSubsystem<ULightBoxDirector>()->GetStyle());
	if(PointCloud)PointCloud->ScatterPulse();
	ObjectBurst = 1;
}

void ALightBoxPawn::ToggleCameraOrbit()
{
	ViewOrbitSpeed=IsCameraOrbiting()?0:12;
	GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Status=IsCameraOrbiting()?TEXT("ゆっくり見回しています"):TEXT("カメラを止めました");
}

void ALightBoxPawn::ResetCameraView()
{
	ViewAngles=FVector2D::ZeroVector;ViewDistance=1;ViewOrbitSpeed=0;
	CameraStart=Camera->GetRelativeTransform();CameraBlendAge=0;CameraBlendSeconds=.45f;
	GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Status=TEXT("正面の視点に戻しました");
}

void ALightBoxPawn::UpdateCameraInput(APlayerController* PC,bool Blocked)
{
	if(Blocked||!PC){bViewDragging=false;LastPinchDistance=0;HoldStarted=-1;bHoldWarm=false;return;}
	int32 Width,Height;PC->GetViewportSize(Width,Height);if(Width<=0||Height<=0)return;
	float X=0,Y=0,X2=0,Y2=0;bool Down=false,Down2=false;
	PC->GetInputTouchState(ETouchIndex::Touch1,X,Y,Down);PC->GetInputTouchState(ETouchIndex::Touch2,X2,Y2,Down2);
#if !PLATFORM_ANDROID
	if(!Down&&PC->IsInputKeyDown(EKeys::RightMouseButton))Down=PC->GetMousePosition(X,Y);
	const float Wheel=PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);if(Wheel!=0)ViewDistance=FMath::Clamp(ViewDistance*FMath::Pow(.85f,Wheel),.55f,2.5f);
#endif
	// Reserve the SDK control at left, menu at top and microphone/input at bottom.
	auto InScene=[&](float A,float B){return A>Width*.23f&&A<Width*.96f&&B>Height*.16f&&B<Height*.77f;};
	if(Down&&Down2&&InScene(X,Y)&&InScene(X2,Y2)){
		const float Distance=FVector2D::Distance(FVector2D(X,Y),FVector2D(X2,Y2));
		if(LastPinchDistance>8&&Distance>8)ViewDistance=FMath::Clamp(ViewDistance*LastPinchDistance/Distance,.55f,2.5f);
		LastPinchDistance=Distance;bViewDragging=false;HoldStarted=-1;bHoldWarm=false;return;
	}
	if(LastPinchDistance>0){LastPinchDistance=0;bViewDragging=false;return;}
	if(Down&&InScene(X,Y)){
		const FVector2D Touch(X,Y);
		// A resting palm or finger warms the light; moving it turns the camera instead.
		const double Now=FPlatformTime::Seconds();
		if(HoldStarted<0){HoldStarted=Now;HoldOrigin=Touch;bHoldMoved=false;}
		if(FVector2D::Distance(Touch,HoldOrigin)>Height*.015f)bHoldMoved=true;
		bHoldWarm=!bHoldMoved&&Now-HoldStarted>.5;
		if(bViewDragging&&!bHoldWarm){const FVector2D Delta=Touch-LastViewTouch;ViewAngles.X=FMath::UnwindDegrees(ViewAngles.X-Delta.X/Height*180);
			const float Pitch=GetGameInstance()->GetSubsystem<ULightBoxDirector>()->GetStyle().CameraPitch;
			ViewAngles.Y=FMath::Clamp(ViewAngles.Y+Delta.Y/Height*140,10.f-Pitch,85.f-Pitch);if(Delta.SizeSquared()>1)ViewOrbitSpeed=0;}
		LastViewTouch=Touch;bViewDragging=true;
	}else{bViewDragging=false;HoldStarted=-1;bHoldWarm=false;}
}

void ALightBoxPawn::UpdateCameraView(const FLightBoxStyle& Style,float Dt)
{
	ViewAngles.X=FMath::UnwindDegrees(ViewAngles.X+ViewOrbitSpeed*Dt);CameraBlendAge+=Dt;
#if !UE_BUILD_SHIPPING
	float ReviewPhase;if(FParse::Value(FCommandLine::Get(),TEXT("PointCloudPhase="),ReviewPhase))CameraBlendAge=CameraBlendSeconds;
#endif
	if(auto* PC=Cast<APlayerController>(GetController())){int32 W,H;PC->GetViewportSize(W,H);if(H>0)LastAspect=float(W)/H;}
	CameraTarget=FLightBoxMotionSystem::CameraForBounds(Style,CameraBounds,LastAspect,ViewAngles,ViewDistance);
	const float A=FMath::SmoothStep(0.f,1.f,FMath::Clamp(CameraBlendAge/FMath::Max(.01f,CameraBlendSeconds),0.f,1.f));
	FTransform C;C.Blend(CameraStart,CameraTarget,A);Camera->SetRelativeTransform(C);
}

void ALightBoxPawn::ApplySensorLook(float Dt)
{
	if (!PostProcess) { return; }
	const FLightBoxSensorReading& S = Felt;
	// Warmth tints toward candle light; darkness lets the motes glow, bright light washes them out.
	const float Target = BaseBloom * FMath::Max(.2f, 1.f + .7f*S.Warmth + .5f*S.Darkness - .35f*S.Brightness);
	FPostProcessSettings& P = PostProcess->Settings;
	P.BloomIntensity = FMath::FInterpTo(P.BloomIntensity, Target, Dt, 4.f);
	P.bOverride_ColorGain = true;
	P.ColorGain = FMath::Lerp(FVector4(1,1,1,1), FVector4(1.18,1.0,.78,1), double(S.Warmth));
}

void ALightBoxPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if(BatchReview.IsValid()){BatchReview->Tick(*this);return;}
#endif
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.1f); Elapsed += Dt;
#if !UE_BUILD_SHIPPING
	float ReviewTime=-1;if(FParse::Value(FCommandLine::Get(),TEXT("LightBoxReviewTime="),ReviewTime)&&ReviewTime>=0) Elapsed=ReviewTime;
#endif
	ULightBoxDirector* Director = GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	Director->PollVoice();
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	const FAttitudeState& Att = Attitude->GetState();
	const FLightBoxStyle& Style = Director->GetStyle();
	if (AppliedRevision != Director->GetRevision()) { AppliedRevision = Director->GetRevision(); ApplyScene(Style); }
	if (SourceEpoch != Attitude->GetSourceEpoch())
	{
		SourceEpoch = Attitude->GetSourceEpoch(); Attitude->ResetNeutral();
		LastYaw = 0; LastSample = Att.SampleCount; bShakeArmed = false; ShakeCooldown = 0.7f;
	}
	FVector2D Tilt(FMath::Clamp(Att.RollDeg / 30.f, -1.f, 1.f), FMath::Clamp(Att.PitchDeg / 30.f, -1.f, 1.f));
	const float Twist = Dt > 0 ? FMath::Clamp(FMath::FindDeltaAngleDegrees(LastYaw, Att.YawDeg) / (Dt * 120), -1.f, 1.f) : 0;
	LastYaw = Att.YawDeg;
	ShakeCooldown = FMath::Max(0.f, ShakeCooldown - Dt);
	if (Att.bAvailable && Att.SampleCount != LastSample)
	{
		LastSample = Att.SampleCount;
		const float G = Att.Accel.Size() / 9.80665f;
		if (G < 1.15f) { bShakeArmed = true; }
		if (bShakeArmed && G > Style.ShakeThreshold && ShakeCooldown <= 0)
		{
			Pulse(); bShakeArmed = false; ShakeCooldown = 0.6f;
		}
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		const ALightBoxHUD* HUD = Cast<ALightBoxHUD>(PC->GetHUD());
		const bool bDialog = HUD && HUD->HasDialog();
		UpdateCameraInput(PC,bDialog);
		if (!bDialog)
		{
			if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) { Pulse(); }
			if (PC->WasInputKeyJustPressed(EKeys::One)) { Director->SelectPreset(0); }
			if (PC->WasInputKeyJustPressed(EKeys::Two)) { Director->SelectPreset(1); }
			if (PC->WasInputKeyJustPressed(EKeys::Three)) { Director->SelectPreset(2); }
			if (PC->WasInputKeyJustPressed(EKeys::R)) { Attitude->ResetNeutral(); }
			if (PC->WasInputKeyJustPressed(EKeys::V)) { if (ALightBoxHUD* H = Cast<ALightBoxHUD>(PC->GetHUD())) { H->OpenWords(); } }
			if (PC->WasInputKeyJustPressed(EKeys::Escape)) { UGameplayStatics::OpenLevel(this, ATitleGameMode::TitleMapName); }
		}
	}
	UpdateCameraView(Style,DeltaSeconds);
	APlayerController* SensePC = Cast<APlayerController>(GetController());
	if (SensePC) { if (const ALightBoxHUD* LH = Cast<ALightBoxHUD>(SensePC->GetHUD()); LH && LH->HasDialog()) { SensePC = nullptr; } }  // typing must not trigger the PC stand-in keys
	Felt = FLightBoxSensorReader::ApplyGains(Sensors.Update(Dt, SensePC, bHoldWarm), Style.Sensors);
	const FLightBoxSensorReading& Sense = Felt;
	if (Sense.bClap && ShakeCooldown <= 0) { Pulse(); ShakeCooldown = 0.6f; }
	if (Sense.bNearReleased && PointCloud) { PointCloud->ScatterPulse(); }
	const FLightBoxEnvironment Env = FLightBoxSensorReader::ToEnvironment(Sense, Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector());
	Simulation.Step(Dt,Tilt,Twist,Style,Env);
	// The sculpture leans away from a breath and flutters with it, through the tilt path.
	const FVector2D CloudTilt = Tilt + FVector2D(FMath::Sin(Elapsed*9.f)*.18f, .55f + FMath::Sin(Elapsed*6.3f)*.15f) * Sense.Breath;
    PointCloud->Advance(DeltaSeconds,CloudTilt,Camera->GetRightVector(),Camera->GetUpVector(),&Simulation);
	ApplySensorLook(Dt);
#if !UE_BUILD_SHIPPING
	if (!bAutoShot && GetWorld()->GetTimeSeconds() > 3 && FParse::Param(FCommandLine::Get(), TEXT("LightBoxAutoShot")))
	{
		bAutoShot = true;
		FString Panel, Filename = TEXT("03_lightbox.png");
		if (FParse::Value(FCommandLine::Get(), TEXT("LightBoxReviewPanel="), Panel))
		{
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (ALightBoxHUD* HUD = Cast<ALightBoxHUD>(PC->GetHUD()))
				{
					if (Panel == TEXT("menu")) { HUD->OpenMenu(); Filename = TEXT("03_lightbox_menu.png"); }
					else if (Panel == TEXT("examples")) { HUD->OpenExamples(); Filename = TEXT("03_lightbox_examples.png"); }
					else if (Panel == TEXT("words")) { HUD->OpenWords(); Filename = TEXT("03_lightbox_words.png"); }
					else if (Panel == TEXT("settings")) { HUD->OpenSettings(); Filename = TEXT("03_lightbox_settings.png"); }
					else if (Panel == TEXT("journal")) { HUD->OpenJournal(); Filename = TEXT("03_lightbox_journal.png"); }
				}
			}
		}
		UIShotName=Filename;UIShotAt=FPlatformTime::Seconds()+.65;
	}
	if(!UIShotName.IsEmpty()&&FPlatformTime::Seconds()>=UIShotAt){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("AutoShots")/UIShotName,true,false);UIShotName.Reset();}
#endif
}
