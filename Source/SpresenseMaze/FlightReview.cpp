#include "FlightPawn.h"
#include "FlightCourse.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

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
