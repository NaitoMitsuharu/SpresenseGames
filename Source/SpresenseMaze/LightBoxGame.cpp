#include "LightBoxGame.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "LightBoxDirector.h"
#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"
#include "LightBoxBatchReview.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	Motes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CoolMotes")); Motes->SetupAttachment(SceneRoot);
	WarmMotes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WarmMotes")); WarmMotes->SetupAttachment(SceneRoot);
	for (UInstancedStaticMeshComponent* Mesh : { Motes.Get(), WarmMotes.Get() })
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false);
		Mesh->SetMobility(EComponentMobility::Movable);
	}
	AutoPossessPlayer = EAutoReceiveInput::Player0;
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
	BuildChamber(); Simulation.Reset();
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LightBox/M_LightGlow.M_LightGlow"));
	CoolMaterial = UMaterialInstanceDynamic::Create(GlowMaterial, this);
	WarmMaterial = UMaterialInstanceDynamic::Create(GlowMaterial, this);
	Motes->SetStaticMesh(Sphere); Motes->SetMaterial(0, CoolMaterial);
	WarmMotes->SetStaticMesh(Sphere); WarmMotes->SetMaterial(0, WarmMaterial);
	ParticleMeshes.Add(Motes); ParticleMeshes.Add(WarmMotes);
	ParticleMaterials.Add(CoolMaterial); ParticleMaterials.Add(WarmMaterial);
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	for (int32 I = 0; I < 8; ++I)
	{
		if (I >= 2)
		{
			auto* C = NewObject<UInstancedStaticMeshComponent>(this);
			C->SetupAttachment(SceneRoot); C->SetStaticMesh(Sphere); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
			C->SetMobility(EComponentMobility::Movable); C->RegisterComponent();
			auto* M = UMaterialInstanceDynamic::Create(GlowMaterial, this); C->SetMaterial(0, M);
			ParticleMeshes.Add(C); ParticleMaterials.Add(M);
		}
		auto* Tail = NewObject<UInstancedStaticMeshComponent>(this);
		Tail->SetupAttachment(SceneRoot); Tail->SetStaticMesh(Cone); Tail->SetCollisionEnabled(ECollisionEnabled::NoCollision); Tail->SetCastShadow(false);
		Tail->SetMobility(EComponentMobility::Movable); Tail->SetMaterial(0, ParticleMaterials[I]); Tail->RegisterComponent(); TailMeshes.Add(Tail);
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
	}
#endif
}

void ALightBoxPawn::BuildChamber()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LightBox/M_LightFloor.M_LightFloor"));
	UMaterialInterface* GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LightBox/M_LightGlow.M_LightGlow"));
	auto Group = [&](const TCHAR* Name, UMaterialInterface* Material, const FLinearColor& Color)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
		C->SetupAttachment(SceneRoot); C->SetStaticMesh(Cube); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false); C->RegisterComponent();
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Material, this);
		if (M) { M->SetVectorParameterValue(TEXT("Color"), Color); C->SetMaterial(0, M); }
		return C;
	};
	UInstancedStaticMeshComponent* Dark = Group(TEXT("Chamber"), Surface, FLinearColor(0.012f, 0.024f, 0.04f));
	FloorMaterial = Cast<UMaterialInstanceDynamic>(Dark->GetMaterial(0));
	Dark->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -6), FVector(6.2, 4.2, 0.12)));
	Backdrop = Group(TEXT("Backdrop"), LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/LightBox/M_LightBackdrop.M_LightBackdrop")), FLinearColor::Black);
	Backdrop->SetMobility(EComponentMobility::Movable);
	BackgroundMaterial = Cast<UMaterialInstanceDynamic>(Backdrop->GetMaterial(0));
	Backdrop->AddInstance(FTransform::Identity);
	UInstancedStaticMeshComponent* Edge = Group(TEXT("LightRim"), GlowMaterial, FLinearColor(0.06f, 0.24f, 0.32f));
	Rim = Edge;
	for (float Z : { 0.f, 350.f })
	{
		for (float Y : { -200.f, 200.f }) { Edge->AddInstance(FTransform(FQuat::Identity, FVector(0, Y, Z), FVector(6, 0.012, 0.012))); }
		for (float X : { -300.f, 300.f }) { Edge->AddInstance(FTransform(FQuat::Identity, FVector(X, 0, Z), FVector(0.012, 4, 0.012))); }
	}
	for (float X : { -300.f, 300.f })
	{
		for (float Y : { -200.f, 200.f }) { Edge->AddInstance(FTransform(FQuat::Identity, FVector(X, Y, 175), FVector(0.012, 0.012, 3.5))); }
	}
	Grid = Group(TEXT("EtchedFloor"), GlowMaterial, FLinearColor(0.012f, 0.05f, 0.07f));
	for (int32 I = -4; I <= 4; ++I)
	{
		Grid->AddInstance(FTransform(FQuat::Identity, FVector(I * 60, 0, 1), FVector(0.008, 3.8, 0.008)));
		if (FMath::Abs(I) <= 2) { Grid->AddInstance(FTransform(FQuat::Identity, FVector(0, I * 60, 1), FVector(5.8, 0.008, 0.008))); }
	}
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
	KeyLight=NewObject<UDirectionalLightComponent>(this,TEXT("GalleryKey")); KeyLight->SetupAttachment(SceneRoot);KeyLight->SetMobility(EComponentMobility::Movable);
	KeyLight->SetForwardShadingPriority(1);KeyLight->SetDynamicShadowCascades(2);KeyLight->SetCastShadows(true);KeyLight->SetDynamicShadowDistanceMovableLight(2600);KeyLight->RegisterComponent();
	FillLight=NewObject<UDirectionalLightComponent>(this,TEXT("GalleryFill"));FillLight->SetupAttachment(SceneRoot);FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetForwardShadingPriority(0);FillLight->SetRelativeRotation(FRotator(-28,135,0));FillLight->SetCastShadows(false);FillLight->RegisterComponent();
	StudioSky=NewObject<USkyLightComponent>(this,TEXT("StudioAmbient"));StudioSky->SetupAttachment(SceneRoot);StudioSky->SetMobility(EComponentMobility::Movable);StudioSky->SourceType=SLS_SpecifiedCubemap;
	StudioSky->SetCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Game/LightBox/T_StudioAmbient.T_StudioAmbient")));StudioSky->SetRealTimeCapture(false);StudioSky->RegisterComponent();
	for(int32 I=0;I<4;++I) {auto* L=NewObject<UPointLightComponent>(this);L->SetupAttachment(SceneRoot);L->SetMobility(EComponentMobility::Movable);L->SetCastShadows(false);L->SetIntensity(0);L->RegisterComponent();AccentLights.Add(L);}

}

void ALightBoxPawn::ApplyScene(const FLightBoxStyle& Style)
{
	if (Simulation.Motes.Num() != Style.Count) { Simulation.Reset(Style.Count); }
	for (UProceduralMeshComponent* C : SceneObjects) { if (C) { C->DestroyComponent(); } }
	SceneObjects.Reset(); ObjectMaterials.Reset(); ObjectBurst = 0; Elapsed=0;GeometryClock=1;
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LightBox/M_LightObject.M_LightObject"));
	if (!Material) { Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LightBox/M_LightSurface.M_LightSurface")); }
	const auto InitialObjects=FLightBoxMotionSystem::ResolveBindings(Style,FLightBoxMotionSystem::Evaluate(Style,0));
	for (const FLightBoxObject& O : InitialObjects)
	{
		auto* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(SceneRoot); C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(O.Shape!=TEXT("water")&&O.Shape!=TEXT("group")); C->RegisterComponent();
		const FLightBoxMesh Mesh = FLightBoxMesh::Build(O);
		C->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, Mesh.Colors, {}, false);
		auto* M = UMaterialInstanceDynamic::Create(Material, this); C->SetMaterial(0, M);
		M->SetVectorParameterValue(TEXT("Color"),O.Shape==TEXT("blossom")?FLinearColor::White:O.Color);M->SetScalarParameterValue(TEXT("Emission"),O.Emission);
		M->SetScalarParameterValue(TEXT("Roughness"),O.Shape==TEXT("water")?.20f:O.Roughness);M->SetScalarParameterValue(TEXT("Metallic"),O.Shape==TEXT("water")?.38f:O.Metallic);
		SceneObjects.Add(C); ObjectMaterials.Add(M);
	}
	if (FloorMaterial) FloorMaterial->SetVectorParameterValue(TEXT("Color"),Style.Floor);
	if (BackgroundMaterial) BackgroundMaterial->SetVectorParameterValue(TEXT("Color"),Style.Background);
	if(KeyLight) {KeyLight->SetLightColor(Style.KeyColor);KeyLight->SetIntensity(Style.KeyIntensity);KeyLight->SetRelativeRotation(FRotator(Style.KeyDirection.X,Style.KeyDirection.Y,Style.KeyDirection.Z));}
	if(StudioSky) {StudioSky->SetLightColor(FMath::Lerp(FLinearColor::White,Style.FillColor,.35f));StudioSky->SetIntensity(Style.Ambient*.85f);}
	if(FillLight) {FillLight->SetLightColor(Style.FillColor);FillLight->SetIntensity(.15f+Style.Ambient*.65f);}
	for(int32 I=0;I<AccentLights.Num();++I) {auto* L=AccentLights[I].Get();L->SetIntensity(0);if(I<Style.Lights.Num()){const auto& A=Style.Lights[I];L->SetRelativeLocation(A.Position);L->SetLightColor(A.Color);L->SetAttenuationRadius(A.Radius);L->SetIntensity(A.Intensity);}}
	if(Rim) Rim->SetVisibility(Style.bShowBox);
	if(Grid) Grid->SetVisibility(Style.bShowBox&&Style.Objects.IsEmpty());
	if(PostProcess) PostProcess->Settings.BloomIntensity=Style.Bloom*.55f;
	int32 Width=16,Height=9;if(auto* PC=Cast<APlayerController>(GetController())) PC->GetViewportSize(Width,Height);
	LastAspect=float(Width)/FMath::Max(1,Height); Camera->SetRelativeTransform(FLightBoxMotionSystem::Camera(Style,LastAspect));
	if(Backdrop) Backdrop->UpdateInstanceTransform(0,FTransform(FRotationMatrix::MakeFromZ(-Camera->GetForwardVector()).ToQuat(),Camera->GetRelativeLocation()+Camera->GetForwardVector()*2800,FVector(140,140,.01)),false,true);

}

void ALightBoxPawn::AnimateScene(float Dt, const FVector2D& Tilt, const FLightBoxStyle& Style)
{
	ObjectBurst=FMath::Max(0.f,ObjectBurst-Dt*1.4f);GeometryClock+=Dt;
	const auto Transforms=FLightBoxMotionSystem::Evaluate(Style,Elapsed,Tilt,ObjectBurst);
	const auto Resolved=FLightBoxMotionSystem::ResolveBindings(Style,Transforms);
	const bool UpdateGeometry=GeometryClock>=1.f/30; if(UpdateGeometry) GeometryClock=0;
	for(int32 I=0;I<SceneObjects.Num();++I) {
		SceneObjects[I]->SetRelativeTransform(Transforms[I]);
		if(UpdateGeometry&&Style.Objects[I].IsDeforming()) {
			const auto Mesh=FLightBoxMesh::Build(Resolved[I],Elapsed);
			SceneObjects[I]->UpdateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Normals,Mesh.UVs,Mesh.Colors,{});
		}
	}

}

void ALightBoxPawn::RenderParticles(const FLightBoxStyle& S)
{
	TArray<FTransform> Body[8], Tails[8];
	const bool Fish=S.ParticleShape==TEXT("fish");
	for (int32 I=0; I<Simulation.Motes.Num(); ++I)
	{
		const FLightMote& P=Simulation.Motes[I]; int32 Bucket=0;
		if (S.ColorMode==TEXT("rainbow")) { Bucket=FMath::Min(7,I*8/FMath::Max(1,S.Count)); }
		else if (S.ColorMode==TEXT("height")) { Bucket=FMath::Clamp(FMath::FloorToInt((P.Position.Z-45)/290*8),0,7); }
		else if (S.ColorMode==TEXT("speed")) { Bucket=FMath::Clamp(FMath::FloorToInt(P.Velocity.Size()/220*8),0,7); }
		else if(S.ColorMode==TEXT("leader")&&I==0) {Bucket=7;}
		else if (S.ColorMode==TEXT("alternate") && I%4==0) { Bucket=7; }
		float Size=(.032f+.016f*FMath::Square(FMath::Sin(Elapsed+P.Phase)))*(1+Simulation.Burst*.5f)*S.ParticleSize;
		if (S.Behavior==TEXT("fire") || S.Behavior==TEXT("fireworks")) { Size*=FMath::Clamp(1-P.Age/3,0.15f,1.f); }
		const FVector Direction=S.Formation!=TEXT("volume")?FVector(1,.10*FMath::Sin(Elapsed*.6),.04*FMath::Cos(Elapsed)) : P.Velocity;
		const FQuat R=Fish&&!Direction.IsNearlyZero()?Direction.Rotation().Quaternion():FQuat::Identity;
		Body[Bucket].Add(FTransform(R,P.Position,Fish?FVector(Size*2.6,Size*.6,Size*.9):FVector(Size)));
		if (Fish)
		{
			Tails[Bucket].Add(FTransform(R*FQuat(FVector::UpVector,FMath::Sin(Elapsed*9+P.Phase)*.30f)*FQuat(FVector::YAxisVector,PI/2),P.Position-R.RotateVector(FVector(Size*130,0,0)),FVector(Size*1.5,Size*.35,Size*1.1)));
		}
	}
	auto Update=[](UInstancedStaticMeshComponent* C,const TArray<FTransform>& Transforms)
	{
		if (C->GetInstanceCount()!=Transforms.Num()) { C->ClearInstances(); C->AddInstances(Transforms,false); }
		else if (Transforms.Num()) { C->BatchUpdateInstancesTransforms(0,Transforms,false,true,false); }
	};
	for (int32 I=0; I<ParticleMeshes.Num(); ++I)
	{
		FLinearColor Color=S.ColorMode==TEXT("rainbow") ? FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(I*255.f/8+Elapsed*9,255)),190,255):FMath::Lerp(S.Color,S.Accent,I/7.f);
		if (ParticleMaterials[I]) { ParticleMaterials[I]->SetVectorParameterValue(TEXT("Color"),Color*S.Glow*(Fish?1.7f:3.3f)*(1+Simulation.Burst)); }
		Update(ParticleMeshes[I],Body[I]); Update(TailMeshes[I],Tails[I]);
	}
}

void ALightBoxPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetGameInstance()) { GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Cancel(); }
	Super::EndPlay(Reason);
}

void ALightBoxPawn::Pulse()
{
	Simulation.Impulse(GetGameInstance()->GetSubsystem<ULightBoxDirector>()->GetStyle());
	ObjectBurst = 1;
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
		if (!bDialog)
		{
			if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) { Pulse(); }
			if (PC->WasInputKeyJustPressed(EKeys::One)) { Director->SelectPreset(0); }
			if (PC->WasInputKeyJustPressed(EKeys::Two)) { Director->SelectPreset(1); }
			if (PC->WasInputKeyJustPressed(EKeys::Three)) { Director->SelectPreset(2); }
			if (PC->WasInputKeyJustPressed(EKeys::R)) { Attitude->ResetNeutral(); }
			if (PC->WasInputKeyJustPressed(EKeys::V)) { if (ALightBoxHUD* H = Cast<ALightBoxHUD>(PC->GetHUD())) { H->OpenWords(); } }
			if (PC->WasInputKeyJustPressed(EKeys::Escape)) { UGameplayStatics::OpenLevel(this, ATitleGameMode::TitleMapName); }
			if (Att.Source == EAttitudeSource::None)
			{
				float X, Y; bool Down; PC->GetInputTouchState(ETouchIndex::Touch1, X, Y, Down);
				int32 Width, Height; PC->GetViewportSize(Width, Height);
				if (Down && Width > 0 && Height > 0 && Y > Height * 0.2 && Y < Height * 0.7 && X > Width * 0.25)
				{
					Tilt = FVector2D(FMath::Clamp((X / Width - 0.5f) * 3, -1.f, 1.f), FMath::Clamp((0.45f - Y / Height) * 4, -1.f, 1.f));
				}
			}
		}
	}
	Simulation.Step(Dt, Tilt, Twist, Style);
	AnimateScene(Dt, Tilt, Style); RenderParticles(Style);
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
