#include "LightBoxGame.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SViewport.h"
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
#include "SceneView.h"
#include "SceneViewExtension.h"
#if PLATFORM_ANDROID
#include "ClearQuad.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#endif

namespace
{
/** Atelier-only clear colour; background brightness never adds an offset to particle colours.
 * Mobile FXAA also avoids repeating textured deformation for velocity. */
class FLightBoxAtelierView final : public FWorldSceneViewExtension
{
public:
    FLightBoxAtelierView(const FAutoRegister& AutoReg, UWorld* World) : FWorldSceneViewExtension(AutoReg, World) {}
    virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        View.BackgroundColor = FLinearColor::FromSRGBColor(FColor(27, 42, 70));
#if PLATFORM_ANDROID
        View.AntiAliasingMethod = Family.EngineShowFlags.AntiAliasing ? AAM_FXAA : AAM_None;
        View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::SpatialUpscale;
#endif
    }
#if PLATFORM_ANDROID
    virtual void PostRenderBasePassMobile_RenderThread(FRHICommandList& RHICmdList, FSceneView& View) override
    {
        // Packaged mobile rendering clears to black, ignoring BackgroundColor. Draw only at
        // far depth before translucency: one quad, no depth writes, no colour offset on grains.
        FClearQuadCallbacks Callbacks;
        Callbacks.PSOModifier = [](FGraphicsPipelineStateInitializer& PSO)
        { PSO.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI(); };
        DrawClearQuad(RHICmdList, View.BackgroundColor, MoveTemp(Callbacks));
    }
#endif
};
}

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
	for (int32 I = 0; I < int32(ELightBoxSensor::Count); ++I) { bSensorOn[I] = FLightBoxSensorReader::IsEnabled(ELightBoxSensor(I)); }
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
	{
		ULightBoxDirector* Director=GetGameInstance()->GetSubsystem<ULightBoxDirector>();
		bool bForce=false;
#if !UE_BUILD_SHIPPING
		bForce=FParse::Param(FCommandLine::Get(),TEXT("LightBoxCalibrate"));
#endif
		// Measure this phone while the open-time readiness check runs; later launches reuse the result.
		if(bForce||(!bReview&&Director->NeedsCalibration())){Calibration=MakeUnique<FCalibrationRun>();Director->Status=TEXT("端末の性能を確認しています（初回のみ・数秒）");}
	}
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
	AtelierView = FSceneViewExtensions::NewExtension<FLightBoxAtelierView>(GetWorld());
	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this, TEXT("LightPostProcess"));
	PostProcess = PP;
	PP->SetupAttachment(SceneRoot); PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMethod=true;PP->Settings.AutoExposureMethod=AEM_Manual;
	PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP->Settings.AutoExposureApplyPhysicalCameraExposure=false;
	PP->Settings.bOverride_AutoExposureBias=true;PP->Settings.AutoExposureBias=0;
	// The sculpture is emissive/unlit: SSAO cannot shade it, but still costs a full mobile pass.
	PP->Settings.bOverride_AmbientOcclusionIntensity=true;PP->Settings.AmbientOcclusionIntensity=0;
	PP->Settings.bOverride_AmbientOcclusionRadius=true;PP->Settings.AmbientOcclusionRadius=35;
	PP->Settings.bOverride_BloomIntensity = true; PP->Settings.BloomIntensity = 0.7f;
	PP->Settings.bOverride_VignetteIntensity = true; PP->Settings.VignetteIntensity = BaseVignette;
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
    PointCloud->Assemble(Budgeted(Style));ObjectBurst=0;
    // One stable particle skin; the point renderer samples the continuous clip.
    bPlayingStory=false;
    BaseBloom=Style.Bloom*.55f;if(PostProcess)PostProcess->Settings.BloomIntensity=BaseBloom;
    int32 Width=16,Height=9;if(auto* PC=Cast<APlayerController>(GetController()))PC->GetViewportSize(Width,Height);
    LastAspect=float(Width)/FMath::Max(1,Height);FBox NextBounds=FLightBoxMotionSystem::Bounds(Style);
    for(const auto& Pose:LightBoxAnimation::Prepare(Style))NextBounds+=FLightBoxMotionSystem::Bounds(Pose);
    const bool KeepView=!NewCamera&&CameraBounds.IsValid&&NextBounds.IsValid&&CameraBounds.Min.Equals(NextBounds.Min,.01)&&CameraBounds.Max.Equals(NextBounds.Max,.01);
    CameraBounds=NextBounds;if(KeepView)return;
    ViewAngles=FVector2D::ZeroVector;ViewDistance=1;ViewOrbitSpeed=Style.CameraOrbit;bViewDragging=false;LastPinchDistance=0;
    CameraStart=Camera->GetRelativeTransform();CameraTarget=FLightBoxMotionSystem::CameraForBounds(Style,CameraBounds,LastAspect);CameraBlendAge=0;CameraBlendSeconds=Style.AssembleSeconds;
}

void ALightBoxPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	AtelierView.Reset();
	Sensors.Stop();
	if (GetGameInstance()) { GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Cancel(); }
	Super::EndPlay(Reason);
}

void ALightBoxPawn::SetSensorOn(ELightBoxSensor S, bool bOn)
{
    if (S >= ELightBoxSensor::Count) { return; }
    bSensorOn[int32(S)] = bOn;
    FLightBoxSensorReader::SetEnabled(S, bOn);
    FLightBoxSensorReader::ApplySwitches(Felt, bSensorOn);
}

void ALightBoxPawn::Pulse(const FVector& Dir, float Strength, float Lead)
{
	Strength = FMath::Clamp(Strength, 0.f, 1.f);
	Simulation.Impulse(GetGameInstance()->GetSubsystem<ULightBoxDirector>()->GetStyle(), Strength);
	if (PointCloud) { PointCloud->AddWave(Dir, Strength, Lead); }
	ObjectBurst = FMath::Max(ObjectBurst, Strength);
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
    if (FSlateApplication::IsInitialized())
    {
        if (const auto User = FSlateApplication::Get().GetUser(0))
        {
            for (const auto& Captor : User->GetCaptorWidgets())
            {
                if (Captor->GetType() != FName(TEXT("SViewport"))) { Blocked = true; break; }
            }
        }
    }
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
		if(!bHoldMoved)WarmPoint=FVector2D(HoldOrigin.X/Width,HoldOrigin.Y/Height);
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
	// The room's light changes how strongly the motes shine, never where they go:
	// in the dark they glow like fireflies, in bright light they fade back.
	const float Gain = FMath::Clamp(1.f + .65f*S.Darkness - .45f*S.Brightness, .45f, 1.8f);
	LightGain = FMath::FInterpTo(LightGain, Gain, Dt, 4.f);
	const float Target = BaseBloom * FMath::Max(.2f, 1.f + .25f*S.Warmth + 1.4f*S.Darkness - .6f*S.Brightness);
	FPostProcessSettings& P = PostProcess->Settings;
	P.BloomIntensity = FMath::FInterpTo(P.BloomIntensity, Target, Dt, 4.f);
	P.bOverride_ColorGain = true;
	// No sensor recolours the sculpture: warmth lifts the grains under the finger (shader) and glows in the
	// overlay, a nearing hand raises the aurora. Only the brightness follows the room's light.
	P.ColorGain = FVector4(LightGain, LightGain, LightGain, 1);
    // Additive grading lifts black and desaturates every particle, even at rest.
    // The ambience edge light supplies the background response without a global colour offset.
    P.bOverride_ColorOffset = false;
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = FMath::FInterpTo(P.VignetteIntensity, FMath::Clamp(BaseVignette + .55f * S.Darkness - .2f * S.Brightness, 0.f, 1.f), Dt, 4.f);
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
	if (!Calibration.IsValid() && Director->bCalibrationRequested) { Calibration=MakeUnique<FCalibrationRun>(); Director->Status=TEXT("端末の性能を測り直しています（数秒）"); }
	if (Calibration.IsValid()) { TickCalibration(); }
	else if (AppliedRevision != Director->GetRevision()) { AppliedRevision = Director->GetRevision(); ApplyScene(Style); }
	if (SourceEpoch != Attitude->GetSourceEpoch())
	{
		SourceEpoch = Attitude->GetSourceEpoch(); Attitude->ResetNeutral();
		LastYaw = 0; LastSample = Att.SampleCount; bShakeArmed = false; ShakeCooldown = 0.7f;
	}
	FVector2D Tilt(FMath::Clamp(Att.RollDeg / 30.f, -1.f, 1.f), FMath::Clamp(Att.PitchDeg / 30.f, -1.f, 1.f));
	// The IMU is a dock sensor like the others: switched off, the phone's lean moves nothing.
	if (!bSensorOn[int32(ELightBoxSensor::Tilt)]) { Tilt = FVector2D::ZeroVector; }
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
			// A harder shake sends a stronger wave out from the sculpture's centre.
			Pulse(FVector::ZeroVector, FMath::Clamp((G - Style.ShakeThreshold) / 1.5f, 0.f, 1.f) * .7f + .3f); bShakeArmed = false; ShakeCooldown = 0.6f;
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
#if !UE_BUILD_SHIPPING
	if (ReviewSensor.IsEmpty()) { FParse::Value(FCommandLine::Get(), TEXT("LightBoxReviewSensor="), ReviewSensor); if (ReviewSensor.IsEmpty()) { ReviewSensor = TEXT("-"); } }
	if (ReviewSensor != TEXT("-"))
	{
        // Review input is independent of saved preferences; never write these switches to config.
        for (bool& On : bSensorOn) { On = true; }
		Felt.bHasMic = Felt.bHasLight = Felt.bHasNear = Felt.bHasMagnet = Felt.bHasTilt = true;
		if (ReviewSensor == TEXT("breath")) { Felt.Breath = .9f; }
		else if (ReviewSensor == TEXT("clap")) { if (!bReviewClapped && !UIShotName.IsEmpty() && FPlatformTime::Seconds() >= UIShotAt - .42) { Felt.bClap = bReviewClapped = true; Felt.ClapStrength = 1.f; } }
		else if (ReviewSensor == TEXT("dark")) { Felt.Darkness = 1.f; }
		else if (ReviewSensor == TEXT("bright")) { Felt.Brightness = 1.f; }
		else if (ReviewSensor == TEXT("warmth")) { Felt.Warmth = .95f; WarmPoint = FVector2D(.62f, .6f); }
		else if (ReviewSensor == TEXT("hand")) { Felt.Near = 1.f; bSensorOn[int32(ELightBoxSensor::Hand)] = true; }
		else if (ReviewSensor == TEXT("magnet")) { Felt.Magnet = .9f; Felt.MagnetScreen = FVector2D(.8f, .6f); }
	}
#endif
    // The phone's motion input pauses while a dialog holds focus; once seen, tilt stays available.
    bTiltSeen |= Att.bAvailable; Felt.bHasTilt = bTiltSeen || Felt.bHasTilt;
    Felt.Tilt = FMath::Clamp(float(Tilt.Size()), 0.f, 1.f); Felt.RollDeg = Att.RollDeg; Felt.PitchDeg = Att.PitchDeg;
    FLightBoxSensorReader::ApplySwitches(Felt, bSensorOn);
	const FLightBoxSensorReading& Sense = Felt;
    // Clap: the sound rings leave the microphone now; the particle wave travels the same way (away from the mic)
    // and its front reaches the sculpture's centre when the rings reach the screen centre. Quick claps overlap.
    ClapCooldown = FMath::Max(0.f, ClapCooldown - Dt);
    if (Sense.bClap && ClapCooldown > 0) { Felt.bClap = false; }
	if (Sense.bClap)
	{
		ClapStartedAt = FPlatformTime::Seconds(); ClapStrength = Sense.ClapStrength; ClapCooldown = .15f;
		const float Lead = PointCloud ? FMath::Max(0.f, FLightBoxSensorReader::ClapTravelSeconds - PointCloud->Reach() / PointCloud->WaveSpeed()) : 0.f;
		Pulse(-float(MicSide) * Camera->GetRightVector(), ClapStrength, Lead);
	}
	// With the hand sensor on, a covering hand is the hand's effect, not darkness.
	if (Felt.Near > 0.f) { Felt.Darkness *= 1.f - Felt.Near; }
	FLightBoxEnvironment Env = FLightBoxSensorReader::ToEnvironment(Sense, Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector());
	// Breath blows across the screen away from the microphone edge, as the wind streaks do.
	Env.Wind = -float(MicSide) * Camera->GetRightVector() * Sense.Breath;
	Simulation.Step(Dt,Tilt,Twist,Style,Env);
	// Sensor forces on the sculpture's grains, in the directions the overlay shows them:
	// breath blows away from the microphone edge (gusting slowly), a magnet draws the near side out,
	// and a resting warm finger lifts the grains under it.
	const FVector2D CloudTilt = Tilt;
	{
		const FVector Wind = -float(MicSide) * Camera->GetRightVector();
		const float Gust = Sense.Breath * (.85f + .15f * FMath::Sin(Elapsed * 1.3f));
		const FVector Pull = (Camera->GetRightVector() * Sense.MagnetScreen.X + Camera->GetUpVector() * Sense.MagnetScreen.Y).GetSafeNormal();
		FVector HeatAt = FVector::ZeroVector;
		if (Sense.Warmth > .001f && PointCloud)
		{
			// Where the finger rests, on the plane through the sculpture facing the camera.
			HeatAt = PointCloud->WorldCentre();
			FVector From, Dir;
			if (APlayerController* WPC = Cast<APlayerController>(GetController()))
			{
				int32 W = 0, H = 0; WPC->GetViewportSize(W, H);
				if (W > 0 && WPC->DeprojectScreenPositionToWorld(WarmPoint.X * W, WarmPoint.Y * H, From, Dir))
				{
					const FVector N = Camera->GetForwardVector(); const double D = FVector::DotProduct(Dir, N);
					if (FMath::Abs(D) > .01) { HeatAt = From + Dir * (FVector::DotProduct(HeatAt - From, N) / D); }
				}
			}
		}
		if (PointCloud) { PointCloud->SetSensorForces(Wind, Gust, Pull, Pull.IsNearlyZero() ? 0.f : Sense.Magnet, HeatAt, Sense.Warmth); }
	}
    bPlayingStory=!Style.Story.IsEmpty()&&!PointCloud->IsPreparing()&&PointCloud->Progress()>=1;
    PointCloud->Advance(DeltaSeconds,CloudTilt,Camera->GetRightVector(),Camera->GetUpVector(),&Simulation);
	ApplySensorLook(Dt);
#if !UE_BUILD_SHIPPING
	if (!bAutoShot && GetWorld()->GetTimeSeconds() > 3 && FParse::Param(FCommandLine::Get(), TEXT("LightBoxAutoShot")))
	{
		bAutoShot = true;
		FString Panel, Filename = ReviewSensor.Len() > 1 ? TEXT("03_sensor_") + ReviewSensor + TEXT(".png") : FString(TEXT("03_lightbox.png"));
		if (FParse::Value(FCommandLine::Get(), TEXT("LightBoxReviewPanel="), Panel))
		{
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (ALightBoxHUD* HUD = Cast<ALightBoxHUD>(PC->GetHUD()))
				{
					if (Panel == TEXT("menu")) { HUD->OpenMenu(); Filename = TEXT("03_lightbox_menu.png"); }
					else if (Panel == TEXT("examples")) { HUD->OpenExamples(); Filename = TEXT("03_lightbox_examples.png"); }
					else if (Panel == TEXT("words")) { HUD->OpenWords(); Filename = TEXT("03_lightbox_words.png"); }
					else if (Panel == TEXT("settings")) { HUD->OpenSettings(1); Filename = TEXT("03_lightbox_settings.png"); }
                    else if (Panel == TEXT("auth")) { HUD->OpenSettings(0); Filename = TEXT("03_lightbox_auth.png"); }
                    else if (Panel == TEXT("budget")) { HUD->OpenSettings(2); Filename = TEXT("03_lightbox_budget.png"); }
					else if (Panel == TEXT("sensors")) { HUD->ReviewSensors(); Filename = TEXT("03_lightbox_sensors.png"); }
					else if (Panel == TEXT("keyboard")) { HUD->ReviewKeyboard(); Filename = TEXT("03_lightbox_keyboard.png"); }
					else if (Panel == TEXT("journal")) { HUD->OpenJournal(); Filename = TEXT("03_lightbox_journal.png"); }
				}
			}
		}
		UIShotName=Filename;UIShotAt=FPlatformTime::Seconds()+.65;
	}
	if(!UIShotName.IsEmpty()&&FPlatformTime::Seconds()>=UIShotAt){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("AutoShots")/UIShotName,true,false);UIShotName.Reset();}
#endif
}

FLightBoxStyle ALightBoxPawn::Budgeted(const FLightBoxStyle& Style) const
{
    // Never draw more grains than this phone measured it can keep smooth (calibration itself is exempt).
    FLightBoxStyle Out=Style;
    if(!Calibration.IsValid())Out.PointCount=FMath::Min(Out.PointCount,GetGameInstance()->GetSubsystem<ULightBoxDirector>()->PointBudget);
    return Out;
}

void ALightBoxPawn::TickCalibration()
{
    using namespace LightBoxPerformance;
    auto& C=*Calibration;const double Now=FPlatformTime::Seconds();
    const float Ms=C.LastFrame>0?float(Now-C.LastFrame)*1000.f:0.f;C.LastFrame=Now;
    ULightBoxDirector* Director=GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    if(!C.bAssembling){
        if(C.Level<Levels().Num()){ApplyScene(Scene(Levels()[C.Level]));C.bAssembling=true;C.FormedAt=0;C.Frames.Reset();return;}
        // Done: choose, remember, and show the user's scene again.
        const int32 Budget=Choose(C.Samples);FString Detail;
        for(const auto& S:C.Samples)Detail+=FString::Printf(TEXT("%s%dk %.0ffps"),Detail.IsEmpty()?TEXT(""):TEXT(" / "),S.Count/1024,S.Fps);
        Director->SetPointBudget(Budget,Detail);Calibration.Reset();AppliedContent.Reset();AppliedRevision=-1;return;
    }
    if(PointCloud->IsPreparing()||PointCloud->Progress()<1)return;
    if(C.FormedAt==0){C.FormedAt=Now;return;}
    if(Now-C.FormedAt<.6)return; // upload and shader warm-up
    C.Frames.Add(Ms);
    if(Now-C.FormedAt<.6+1.6)return;
    const FSample S=Measure(C.Frames,Levels()[C.Level]);C.Samples.Add(S);C.bAssembling=false;
    UE_LOG(LogTemp,Display,TEXT("LightBox calibration: %d grains %.1f fps p95 %.1f ms"),S.Count,S.Fps,S.P95Ms);
    // Stop once 30 fps is no longer held: larger counts would only be slower.
    C.Level=Holds(S,30)?C.Level+1:Levels().Num();
}
