#include "FlightPawn.h"
#include "FlightCourse.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"

void AFlightPawn::TickModelReview(float Dt)
{
#if !UE_BUILD_SHIPPING
	AutoShotTime += Dt;
	if (!FindObjectFast<UProceduralMeshComponent>(this,TEXT("ReviewDrone")))
	{
		S=20000; Off=Vel=FVector2D::ZeroVector; Course->ResetFrom(S);
		State=EFlightState::Paused; UpdateShip(0);
		if (auto* PC=Cast<APlayerController>(GetController())) { if (PC->GetHUD()) { PC->GetHUD()->bShowHUD=false; } }
		const TCHAR* Names[]={TEXT("ReviewDrone"),TEXT("ReviewTurret"),TEXT("ReviewBoss")};
		for (int32 Kind=0; Kind<3; ++Kind)
		{
			auto* M=NewObject<UProceduralMeshComponent>(this,Names[Kind]);
			M->SetupAttachment(Root); M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetRelativeRotation(Course->Path.Frame(S).Rotation());
			M->RegisterComponent(); AddInstanceComponent(M);
			auto* Hull=FlightMaterials::Surface(this,FlightPalette::Graphite,0.48f,0.38f);
			auto* Armor=FlightMaterials::Surface(this,FlightPalette::HostileArmor,0.32f,0.32f);
			auto* Glow=FlightMaterials::Glow(this,FlightPalette::Coral*(Kind==2?3.2f:2.8f));
			const FFlightMesh& H=Kind==0?FlightModels::DroneHull():Kind==1?FlightModels::TurretHull():FlightModels::BossHull();
			const FFlightMesh& A=Kind==0?FlightModels::DroneArmor():Kind==1?FlightModels::TurretArmor():FlightModels::BossArmor();
			const FFlightMesh& G=Kind==0?FlightModels::DroneGlow():Kind==1?FlightModels::TurretGlow():FlightModels::BossGlow();
			H.Upload(M,0,Hull); A.Upload(M,1,Armor); G.Upload(M,2,Glow);
			M->SetVisibility(false);
			UE_LOG(LogTemp,Display,TEXT("FlightModelReview: %s %d triangles"),Names[Kind],(H.Tris.Num()+A.Tris.Num()+G.Tris.Num())/3);
		}
		int32 ShipTriangles=0;
		for (int32 I=0; I<Parts.Body->GetNumSections(); ++I) { ShipTriangles+=Parts.Body->GetProcMeshSection(I)->ProcIndexBuffer.Num()/3; }
		UE_LOG(LogTemp,Display,TEXT("FlightModelReview: player body %d triangles"),ShipTriangles);
	}
	struct FView { float At; int32 Kind; FVector Camera, Look; const TCHAR* Name; };
	static const FView Views[]={
		{2.5f,-1,{-1250,-1050,650},{-20,0,10},TEXT("01_player_rear")},
		{4.f,-1,{1350,-950,600},{-20,0,10},TEXT("02_player_front")},
		{5.5f,0,{680,-620,390},{0,0,5},TEXT("03_drone_front")},
		{7.f,0,{-650,-540,380},{0,0,5},TEXT("04_drone_rear")},
		{8.5f,1,{640,-490,330},{40,0,0},TEXT("05_turret")},
		{10.f,2,{3600,-2600,1700},{-40,0,30},TEXT("06_boss_front")},
		{11.5f,2,{-3500,-2600,1700},{-40,0,30},TEXT("07_boss_rear")}
	};
	if (AutoShotStep>=UE_ARRAY_COUNT(Views))
	{
		if (AutoShotTime>12.2f) { FPlatformMisc::RequestExit(false); }
		return;
	}
	const FView& View=Views[AutoShotStep];
	ShipPivot->SetVisibility(View.Kind<0,true);
	FindObjectFast<UProceduralMeshComponent>(this,TEXT("ReviewDrone"))->SetVisibility(View.Kind==0);
	FindObjectFast<UProceduralMeshComponent>(this,TEXT("ReviewTurret"))->SetVisibility(View.Kind==1);
	FindObjectFast<UProceduralMeshComponent>(this,TEXT("ReviewBoss"))->SetVisibility(View.Kind==2);
	ShipRoll=-5; ShipPitch=2; AimYaw=ShipYaw; AimPitch=ShipPitch; UpdateShip(Dt);
	if (View.Kind>=0) { EngineLight->SetIntensity(0); }
	const FPathFrame Fr=Course->Path.Frame(S);
	const FVector Pos=Fr.ToWorld(View.Camera.X,View.Camera.Y,View.Camera.Z);
	const FVector Target=Fr.ToWorld(View.Look.X,View.Look.Y,View.Look.Z);
	Camera->SetWorldLocationAndRotation(Pos,FRotationMatrix::MakeFromXZ(Target-Pos,FVector::UpVector).Rotator());
	Camera->SetFieldOfView(42);
	if (AutoShotTime>=View.At)
	{
		const FString Dir=FPaths::ProjectSavedDir()/TEXT("ModernAirframes");
		IFileManager::Get().MakeDirectory(*Dir,true);
		FScreenshotRequest::RequestScreenshot(Dir/FString(View.Name)+TEXT(".png"),false,false);
		++AutoShotStep;
	}
#endif
}

void AFlightPawn::TickUpgradeReview(float Dt)
{
#if !UE_BUILD_SHIPPING
	AutoShotTime += Dt;
	AutoRoll = AutoPitch = 0;
	InvulnerableUntil = GetWorld()->GetTimeSeconds() + 10.f;
	struct FStep { float At; uint8 Op; double S; const TCHAR* Name; };
	static const FStep Steps[] = {
		{0.4f,1,0,nullptr},
		{4.1f,2,35000,nullptr}, {5.2f,0,0,TEXT("01_lighthouse")},
		{5.3f,2,117000,nullptr}, {6.4f,0,0,TEXT("02_harbour")},
		{6.5f,2,330000,nullptr}, {7.6f,0,0,TEXT("03_viaduct")},
		{7.7f,2,395000,nullptr}, {8.8f,0,0,TEXT("04_waterfall")},
		{8.9f,2,525000,nullptr}, {10.f,0,0,TEXT("05_hangar")},
		{10.1f,2,574000,nullptr}, {11.2f,0,0,TEXT("06_radar")},
		{11.3f,2,92000,nullptr}, {13.3f,0,0,TEXT("07_attack_warning")},
		{13.4f,3,0,nullptr}, {13.8f,0,0,TEXT("08_paused")},
		{14.f,2,11500,nullptr}, {14.2f,6,0,nullptr}, {14.3f,0,0,TEXT("09_score_check")},
		{14.6f,5,0,nullptr}, {14.9f,0,0,TEXT("10_result_preview")},
		{15.6f,4,0,nullptr}
	};
	while (AutoShotStep < UE_ARRAY_COUNT(Steps) && AutoShotTime >= Steps[AutoShotStep].At)
	{
		const FStep& Step = Steps[AutoShotStep++];
		switch (Step.Op)
		{
		case 0:
		{
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("FlightUpgradeReview");
			IFileManager::Get().MakeDirectory(*Dir, true);
			FScreenshotRequest::RequestScreenshot(Dir / FString(Step.Name) + TEXT(".png"), true, false);
			break;
		}
		case 1: RequestNeutralReset(); break;
		case 2:
			S = Step.S; Off = Vel = FVector2D::ZeroVector;
			Course->ResetFrom(S); State = EFlightState::Playing;
			UpdateCamera(0, true); break;
		case 3: TogglePause(); break;
		case 6:
		{
			ensureMsgf(Course->Score.RingPoints >= 100 && Course->RingsPassed >= 1, TEXT("Flight review: real ring crossing must award points"));
			Course->CommitCheckpoint();
			const int32 BeforeDamage = Course->Score.DamageCount;
			InvulnerableUntil = 0; ApplyDamage(12, GetActorLocation());
			ensureMsgf(Course->Score.DamageCount == BeforeDamage + 1 && Course->Score.Combo == 0, TEXT("Flight review: damage must break combo"));
			Course->ResetFrom(S);
			ensureMsgf(Course->Score.RingPoints >= 100 && Course->Score.DamageCount == BeforeDamage + 1, TEXT("Flight review: checkpoint must retain damage and committed points"));
			UE_LOG(LogTemp, Display, TEXT("FlightUpgradeReview: real ring, damage and checkpoint checks completed"));
			break;
		}
		case 5:
			// Synthetic result solely for layout QA. CompleteRun(true) cannot save.
			Shield = MaxShield; HitFlash = HealFlash = 0;
			Course->Score = FFlightScore();
			for (int32 I = 0; I < 21; ++I) { Course->Score.Award(100, true); }
			for (int32 I = 0; I < 10; ++I) { Course->Score.Award(250, false); }
			Course->Score.Grazes = 8; Course->Score.FlyingPoints = 400;
			Course->Kills = 10; Course->RingsPassed = 21;
			Course->CompleteRun(true); State = EFlightState::Cleared; ClearAt = -1;
			break;
		default: FPlatformMisc::RequestExit(false); break;
		}
	}
#endif
}


void AFlightPawn::TickHudReview(float Dt)
{
#if !UE_BUILD_SHIPPING
	AutoShotTime += Dt;
	AutoRoll = AutoPitch = 0;
	InvulnerableUntil = GetWorld()->GetTimeSeconds()+10;
	static float PauseDodge=0, PauseCooldown=0;
	struct FStep { float At; uint8 Op; double Arg; const TCHAR* Name; };
	static const FStep Steps[]={
		{2.5f,0,0,TEXT("01_ready")},{2.7f,1,0,nullptr},{3.8f,0,0,TEXT("02_countdown")},
		{9.5f,0,0,TEXT("03_sea_combo")},{9.6f,2,520000,nullptr},{10.8f,0,0,TEXT("04_base")},
		{11.f,6,0,nullptr},{11.14f,0,0,TEXT("05_quick_dodge")},
		{11.18f,3,0,nullptr},{11.8f,7,0,nullptr},{12.f,0,0,TEXT("06_pause")},
		{12.1f,2,646000,nullptr},{14.f,0,0,TEXT("07_boss")},
		{14.2f,8,0,nullptr},{14.6f,0,0,TEXT("08_retry")},
		{15.2f,5,0,nullptr},{15.6f,0,0,TEXT("09_result_preview")},{16.5f,4,0,nullptr}
	};
	while (AutoShotStep<UE_ARRAY_COUNT(Steps) && AutoShotTime>=Steps[AutoShotStep].At)
	{
		const FStep& Step=Steps[AutoShotStep++];
		switch (Step.Op)
		{
		case 0:
		{
			const FString Dir=FPaths::ProjectSavedDir()/TEXT("ModernFlightHUD")/(FParse::Param(FCommandLine::Get(),TEXT("HudWide"))?TEXT("Wide"):TEXT("Standard"));
			IFileManager::Get().MakeDirectory(*Dir,true);
			FScreenshotRequest::RequestScreenshot(Dir/FString(Step.Name)+TEXT(".png"),true,false);
			break;
		}
		case 1: RequestNeutralReset(); break;
		case 2:
			S=Step.Arg; Off=Vel=FVector2D::ZeroVector; DodgeTime=DodgeCooldown=0;
			Course->ResetFrom(S); State=EFlightState::Playing; UpdateCamera(0,true); break;
		case 3:
			TogglePause(); PauseDodge=DodgeTime; PauseCooldown=DodgeCooldown; break;
		case 6:
		{
			ensureMsgf(RequestDodge(1),TEXT("HUD review: dodge must start"));
			ensureMsgf(!RequestDodge(-1),TEXT("HUD review: cooldown must reject repeated dodge"));
			const float Before=Shield;
			InvulnerableUntil=0; ApplyDamage(12,GetActorLocation());
			ensureMsgf(Shield==Before,TEXT("HUD review: dodge must protect briefly"));
			break;
		}
		case 7:
			ensureMsgf(DodgeTime==PauseDodge && DodgeCooldown==PauseCooldown,TEXT("HUD review: pause must freeze dodge and cooldown"));
			ensureMsgf(FMath::Abs(Off.X)<=AFlightCourse::LimitX,TEXT("HUD review: dodge must stay inside course"));
			UE_LOG(LogTemp,Display,TEXT("FlightHudReview: dodge, cooldown, protection, bounds and pause checks completed"));
			break;
		case 8:
			State=EFlightState::Playing; DodgeTime=0; InvulnerableUntil=0; ApplyDamage(200,GetActorLocation()); break;
		case 5:
			Shield=MaxShield; HitFlash=HealFlash=0; DodgeTime=DodgeCooldown=0;
			Course->Score=FFlightScore();
			for (int32 I=0; I<21; ++I) { Course->Score.Award(100,true); }
			for (int32 I=0; I<10; ++I) { Course->Score.Award(250,false); }
			Course->Score.Grazes=8; Course->Score.FlyingPoints=400; Course->Kills=10; Course->RingsPassed=21;
			Course->CompleteRun(true); State=EFlightState::Cleared; ClearAt=-1;
			break;
		default: FPlatformMisc::RequestExit(false); break;
		}
	}
#endif
}
