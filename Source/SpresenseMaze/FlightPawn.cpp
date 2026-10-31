#include "FlightPawn.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "FlightCourse.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TitleGameMode.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

AFlightPawn::AFlightPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	ShipPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipPivot"));
	ShipPivot->SetupAttachment(Root);
	ShipPivot->SetUsingAbsoluteRotation(true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->FieldOfView = 80.f;

	EngineLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EngineLight"));
	EngineLight->SetupAttachment(ShipPivot);
	EngineLight->SetRelativeLocation(FVector(-470.f, 0.f, -20.f));
	EngineLight->SetIntensity(9000.f);
	EngineLight->SetAttenuationRadius(700.f);
	EngineLight->SetLightColor(FLinearColor(0.35f, 0.75f, 1.f));
	EngineLight->SetCastShadows(false);

	Trail = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Root);
	Trail->SetUsingAbsoluteLocation(true);
	Trail->SetUsingAbsoluteRotation(true);
	Trail->SetUsingAbsoluteScale(true);
	Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trail->SetCastShadow(false);
	Trail->SetMobility(EComponentMobility::Movable);

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AFlightPawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		PC->bEnableClickEvents = true;
		PC->bEnableTouchEvents = true;
	}

	if (TActorIterator<AFlightCourse> It(GetWorld()); It)
	{
		Course = *It;
	}
	if (!Course)
	{
		Course = GetWorld()->SpawnActor<AFlightCourse>();
	}

	FlightModels::BuildShip(this, ShipPivot, Parts);
	AileronBaseL = Parts.AileronL ? Parts.AileronL->GetRelativeRotation() : FRotator::ZeroRotator;
	AileronBaseR = Parts.AileronR ? Parts.AileronR->GetRelativeRotation() : FRotator::ZeroRotator;

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Trail->SetStaticMesh(Cube);
	}
	TrailMID = FlightMaterials::Additive(this, FLinearColor(0.3f, 0.9f, 1.f) * 3.f);
	Trail->SetMaterial(0, TrailMID);
	TrailSegs.SetNum(120);
	for (int32 i = 0; i < TrailSegs.Num(); ++i)
	{
		Trail->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -1.e6f), FVector(0.001f)), true);
	}

	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		Attitude->EnsureSprBox();
		SeenEpoch = Attitude->GetSourceEpoch();
	}

#if !UE_BUILD_SHIPPING
	bAutoShot = FParse::Param(FCommandLine::Get(), TEXT("FlightAutoShot"));
#endif

	// Start paused: the player settles the grip and confirms 姿勢リセット.
	State = EFlightState::AwaitNeutral;
	S = 0.0;
	UpdateShip(0.f);
	UpdateCamera(0.f, true);
}

FVector AFlightPawn::NoseDir() const
{
	const float P = FMath::DegreesToRadians(ShipPitch);
	const float Y = FMath::DegreesToRadians(ShipYaw);
	return FVector(FMath::Cos(P) * FMath::Cos(Y), FMath::Cos(P) * FMath::Sin(Y), FMath::Sin(P));
}

FVector AFlightPawn::AimPoint(float Distance) const
{
	return FVector(0.f, Off.X, Off.Y) + NoseDir() * Distance;
}

FVector AFlightPawn::PathToWorld(double PS, const FVector2D& POff) const
{
	return Course ? Course->Path.Frame(PS).ToWorld(0, POff.X, POff.Y) : FVector::ZeroVector;
}

FVector AFlightPawn::GetCameraLocation() const
{
	return Camera->GetComponentLocation();
}

bool AFlightPawn::IsInvulnerable() const
{
	return GetWorld()->GetTimeSeconds() < InvulnerableUntil;
}

void AFlightPawn::ShowBanner(const FString& Text, float Seconds)
{
	Banner = Text;
	BannerTime = Seconds;
}

void AFlightPawn::Vibrate(float Intensity, float Duration)
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->PlayDynamicForceFeedback(Intensity, Duration, true, true, true, true);
	}
}

void AFlightPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float Now = GetWorld()->GetTimeSeconds();
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	if (!Course || !Attitude)
	{
		return;
	}

	// A new input source has its own neutral pose: stop and ask for a re-centre
	// instead of letting the ship swerve toward whatever the new sensor reads.
	const int32 Epoch = Attitude->GetSourceEpoch();
	if (Epoch != SeenEpoch)
	{
		SeenEpoch = Epoch;
		if (State == EFlightState::Playing || State == EFlightState::Countdown || State == EFlightState::Paused)
		{
			State = EFlightState::AwaitNeutral;
			bInputLost = true;
		}
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->WasInputKeyJustPressed(EKeys::P) || PC->WasInputKeyJustPressed(EKeys::Escape))
		{
			TogglePause();
		}
		if (PC->WasInputKeyJustPressed(EKeys::R))
		{
			RequestNeutralReset();
		}
	}

	if (bAutoShot)
	{
		TickAutoShot(Dt);
	}

	switch (State)
	{
	case EFlightState::AwaitNeutral:
		UpdateInput(Dt, false);
		break;
	case EFlightState::Countdown:
		UpdateInput(Dt, false);
		CountdownLeft -= Dt;
		if (CountdownLeft <= 0.f)
		{
			State = EFlightState::Playing;
		}
		break;
	case EFlightState::Playing:
	{
		UpdateInput(Dt, true);
		const EFlightSection Before = Course->SectionAt(S);
		S += AFlightCourse::ShipSpeed * Dt;
		const EFlightSection After = Course->SectionAt(S);
		if (After != Before)
		{
			ShowBanner(After == EFlightSection::Boss ? TEXT("警告：大型機接近") : AFlightCourse::SectionName(After), 2.6f);
		}
		while (CheckpointIndex + 1 < Course->Checkpoints.Num() && S >= Course->Checkpoints[CheckpointIndex + 1])
		{
			++CheckpointIndex;
		}

		// Automatic fire, alternating guns that converge on the far reticle.
		FireTimer -= Dt;
		while (FireTimer <= 0.f)
		{
			FireTimer += FireInterval;
			bNextGunLeft = !bNextGunLeft;
			const FVector GunLocal = bNextGunLeft ? Parts.GunL : Parts.GunR;
			const FQuat ShipQ = FRotator(ShipPitch, ShipYaw, ShipRoll).Quaternion();
			const FVector GunPath = FVector(0.f, Off.X, Off.Y) + ShipQ.RotateVector(GunLocal);
			const FVector Dir = (AimPoint(AimDistance) - GunPath).GetSafeNormal();
			Course->FireBolt(S + GunPath.X, FVector2D(GunPath.Y, GunPath.Z), Dir, AFlightCourse::ShipSpeed);
		}

		Course->Simulate(Dt, this);
		if (ClearAt > 0.f && Now >= ClearAt)
		{
			ClearAt = -1.f;
			State = EFlightState::Cleared;
		}
		break;
	}
	case EFlightState::Destroyed:
		if (Now - DestroyedTime > 2.6f)
		{
			Respawn();
		}
		break;
	case EFlightState::Cleared:
		// Victory lap: keep cruising, no more threats.
		UpdateInput(Dt, true);
		S += AFlightCourse::ShipSpeed * 0.8 * Dt;
		break;
	default:
		break;
	}

	if (State != EFlightState::Paused)
	{
		Course->TickEffects(Dt);
		UpdateTrail(Dt);
	}
	UpdateShip(Dt);
	UpdateCamera(Dt, false);

	HitFlash = FMath::Max(HitFlash - Dt * 2.5f, 0.f);
	HealFlash = FMath::Max(HealFlash - Dt * 1.5f, 0.f);
	BannerTime = FMath::Max(BannerTime - Dt, 0.f);
}

void AFlightPawn::UpdateInput(float Dt, bool bSteer)
{
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	const FAttitudeState& Att = Attitude->GetState();

	auto Map = [this](float A)
	{
		const float Mag = FMath::Abs(A);
		if (Mag < DeadZoneDeg)
		{
			return 0.f;
		}
		return FMath::Sign(A) * FMath::Clamp((Mag - DeadZoneDeg) / (MaxTiltDeg - DeadZoneDeg), 0.f, 1.f);
	};
	const float PitchSign = bInvertPitch ? 1.f : -1.f;
	const float RollDeg = bAutoShot ? AutoRoll : Att.RollDeg;
	const float PitchDeg = bAutoShot ? AutoPitch : Att.PitchDeg;
	const float YawDeg = bAutoShot ? 0.f : Att.YawDeg;
	const bool bHasInput = Att.bAvailable || bAutoShot;
	CommandX = bHasInput ? Map(RollDeg) : 0.f;
	CommandZ = bHasInput ? PitchSign * Map(PitchDeg) : 0.f;

	// The model shows the applied attitude directly: roll as bank, pitch on the
	// nose (limited so the line of fire stays useful), yaw clamped to +-15.
	const float TargetRoll = FMath::Clamp(RollDeg, -75.f, 75.f);
	const float TargetPitch = FMath::Clamp(PitchSign * PitchDeg, -NosePitchLimitDeg, NosePitchLimitDeg);
	const float TargetYaw = FMath::Clamp(YawDeg, -YawLimitDeg, YawLimitDeg);
	const float Response = 14.f;
	ShipRoll = FMath::FInterpTo(ShipRoll, TargetRoll, Dt, Response);
	ShipPitch = FMath::FInterpTo(ShipPitch, TargetPitch, Dt, Response);
	ShipYaw = FMath::FInterpTo(ShipYaw, TargetYaw, Dt, Response);

	if (!bSteer)
	{
		Vel = FVector2D::ZeroVector;
		return;
	}
	const FVector2D Target(CommandX * LateralSpeed, CommandZ * VerticalSpeed);
	Vel = FMath::Vector2DInterpTo(Vel, Target, Dt, 5.5f);
	Off += Vel * Dt;
	if (FMath::Abs(Off.X) > AFlightCourse::LimitX)
	{
		Off.X = FMath::Sign(Off.X) * AFlightCourse::LimitX;
		Vel.X = 0.f;
	}
	if (FMath::Abs(Off.Y) > AFlightCourse::LimitZ)
	{
		Off.Y = FMath::Sign(Off.Y) * AFlightCourse::LimitZ;
		Vel.Y = 0.f;
	}
}

void AFlightPawn::UpdateShip(float Dt)
{
	if (!Course)
	{
		return;
	}
	const FPathFrame Fr = Course->Path.Frame(S);
	const FQuat Q = Fr.Rotation() * FRotator(ShipPitch, ShipYaw, ShipRoll).Quaternion();
	SetActorLocation(Fr.ToWorld(0, Off.X, Off.Y));
	ShipPivot->SetWorldRotation(Q);

	// Control surfaces follow the stick, not the airframe.
	const float Aileron = CommandX * 20.f;
	const float Elevator = -CommandZ * 16.f;
	if (Parts.AileronL) { Parts.AileronL->SetRelativeRotation(AileronBaseL + FRotator(Aileron, 0.f, 0.f)); }
	if (Parts.AileronR) { Parts.AileronR->SetRelativeRotation(AileronBaseR + FRotator(-Aileron, 0.f, 0.f)); }
	if (Parts.ElevatorL) { Parts.ElevatorL->SetRelativeRotation(FRotator(Elevator, 0.f, 0.f)); }
	if (Parts.ElevatorR) { Parts.ElevatorR->SetRelativeRotation(FRotator(Elevator, 0.f, 0.f)); }

	const float T = GetWorld()->GetTimeSeconds();
	const float Flicker = 0.85f + 0.15f * FMath::Sin(T * 47.f) * FMath::Sin(T * 31.f);
	if (Parts.PlumeGlow)
	{
		const float Boost = State == EFlightState::Playing || State == EFlightState::Cleared ? 1.f : 0.45f;
		Parts.PlumeGlow->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.7f, 1.f) * 1.6f * Flicker * Boost);
	}
	if (Parts.TrimGlow)
	{
		const FLinearColor Base(0.25f, 0.85f, 1.f);
		const FLinearColor Hit(1.f, 0.25f, 0.2f);
		const float Blink = IsInvulnerable() && FMath::Fmod(T, 0.16f) < 0.08f ? 0.2f : 1.f;
		Parts.TrimGlow->SetVectorParameterValue(TEXT("Color"),
			FMath::Lerp(Base, Hit, HitFlash) * (6.f + 10.f * HealFlash) * Blink);
	}
	EngineLight->SetIntensity(9000.f * Flicker);
}

void AFlightPawn::UpdateCamera(float Dt, bool bSnap)
{
	if (!Course)
	{
		return;
	}
	// Chase camera: trails the ship's offset with lag but never rolls, so the
	// horizon stays level however hard the player banks.
	const FVector TargetOff(0.f, Off.X * 0.7f, Off.Y * 0.7f);
	const FVector TargetLook(0.f, Off.X * 0.9f, Off.Y * 0.9f);
	if (bSnap)
	{
		CamOffsetSmoothed = TargetOff;
		CamLookSmoothed = TargetLook;
	}
	else
	{
		CamOffsetSmoothed = FMath::VInterpTo(CamOffsetSmoothed, TargetOff, Dt, 5.f);
		CamLookSmoothed = FMath::VInterpTo(CamLookSmoothed, TargetLook, Dt, 7.f);
	}
	const FVector CamPos = Course->Path.Frame(S - 1150.0).ToWorld(0, CamOffsetSmoothed.Y, CamOffsetSmoothed.Z + 330.0);
	const FVector Look = Course->Path.Frame(S + 1800.0).ToWorld(0, CamLookSmoothed.Y, CamLookSmoothed.Z + 40.0);
	const FRotator Rot = FRotationMatrix::MakeFromXZ(Look - CamPos, FVector::UpVector).Rotator();
	Camera->SetWorldLocationAndRotation(CamPos, FRotator(Rot.Pitch, Rot.Yaw, 0.f));
}

void AFlightPawn::UpdateTrail(float Dt)
{
	const FTransform ShipXf = ShipPivot->GetComponentTransform();
	const FVector TipL = ShipXf.TransformPosition(Parts.WingTipL);
	const FVector TipR = ShipXf.TransformPosition(Parts.WingTipR);
	TrailTime = FMath::Max(TrailTime - Dt, 0.f);
	if (TrailTime > 0.f && bHaveLastTips)
	{
		for (const TPair<FVector, FVector>& Seg : { TPair<FVector, FVector>(LastTipL, TipL), TPair<FVector, FVector>(LastTipR, TipR) })
		{
			FTrailSeg& T = TrailSegs[NextTrail];
			NextTrail = (NextTrail + 1) % TrailSegs.Num();
			T.A = Seg.Key;
			T.B = Seg.Value;
			T.Age = 0.f;
			T.Life = 0.35f + 0.35f * TrailStrength;
			T.bActive = true;
		}
	}
	LastTipL = TipL;
	LastTipR = TipR;
	bHaveLastTips = true;

	// Segments are left in world space; the ship flies away from them, which
	// is what draws the streak.
	TArray<FTransform> Xf;
	Xf.SetNum(TrailSegs.Num());
	for (int32 i = 0; i < TrailSegs.Num(); ++i)
	{
		FTrailSeg& T = TrailSegs[i];
		if (T.bActive)
		{
			T.Age += Dt;
			T.bActive = T.Age < T.Life;
		}
		if (!T.bActive)
		{
			Xf[i] = FTransform(FQuat::Identity, FVector(0, 0, -1.e6f), FVector(0.001f));
			continue;
		}
		const FVector D = T.B - T.A;
		const float Len = FMath::Max(D.Size(), 1.f);
		const float K = (1.f - T.Age / T.Life) * (0.5f + TrailStrength);
		Xf[i] = FTransform(FRotationMatrix::MakeFromX(D).ToQuat(), (T.A + T.B) * 0.5f,
			FVector(Len / 100.f, 0.07f * K, 0.07f * K));
	}
	Trail->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
	if (TrailMID)
	{
		TrailMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.9f, 1.f) * (1.5f + 3.f * TrailStrength));
	}
}

void AFlightPawn::ApplyDamage(float Amount, const FVector& FromWorld)
{
	if (State != EFlightState::Playing || IsInvulnerable())
	{
		return;
	}
	Shield = FMath::Max(Shield - Amount, 0.f);
	HitFlash = 1.f;
	InvulnerableUntil = GetWorld()->GetTimeSeconds() + 1.f;
	Course->PlayCue(FlightCue::Damage);
	Vibrate(0.8f, 0.18f);
	if (Shield <= 0.f)
	{
		State = EFlightState::Destroyed;
		DestroyedTime = GetWorld()->GetTimeSeconds();
		Course->SpawnExplosion(GetActorLocation(), 2.4f);
		Course->PlayCue(FlightCue::BigBoom);
		ShipPivot->SetVisibility(false, true);
		Vibrate(1.f, 0.5f);
	}
}

void AFlightPawn::OnRingPassed(int32 Streak, bool bShieldBonus)
{
	TrailStrength = FMath::Clamp(0.25f + Streak * 0.12f, 0.f, 1.f);
	TrailTime = 0.5f + FMath::Min(Streak, 10) * 0.08f;
	if (bShieldBonus)
	{
		Shield = FMath::Min(MaxShield, Shield + 15.f);
		HealFlash = 1.f;
		ShowBanner(TEXT("シールド回復"), 1.6f);
	}
	Vibrate(0.25f, 0.06f);
}

void AFlightPawn::OnRingMissed()
{
	TrailStrength = 0.f;
}

void AFlightPawn::OnBossDefeated()
{
	ClearAt = GetWorld()->GetTimeSeconds() + 2.8f;
	ShowBanner(TEXT("大型機撃破"), 2.6f);
	Vibrate(1.f, 0.6f);
}

void AFlightPawn::TickAutoShot(float Dt)
{
	AutoShotTime += Dt;
	AutoRoll = FMath::Sin(AutoShotTime * 0.9f) * 24.f;
	AutoPitch = FMath::Sin(AutoShotTime * 0.6f) * 10.f;
	InvulnerableUntil = GetWorld()->GetTimeSeconds() + 10.f;
	auto Shot = [](const TCHAR* Name)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("AutoShots") / FString(Name) + TEXT(".png");
		FScreenshotRequest::RequestScreenshot(File, true, false);
	};
	auto Jump = [this](double ToS)
	{
		S = ToS;
		Off = Vel = FVector2D::ZeroVector;
		Course->ResetFrom(S);
		UpdateCamera(0.f, true);
	};
	struct FStep { float At; int32 Op; double Arg; const TCHAR* Name; };
	static const FStep Steps[] = {
		{ 2.5f, 0, 0, TEXT("01_await") },
		{ 2.7f, 1, 0, nullptr },           // neutral reset -> countdown
		{ 3.8f, 0, 0, TEXT("02_countdown") },
		{ 9.5f, 0, 0, TEXT("03_sea") },
		{ 9.6f, 2, 296000.0, nullptr },
		{ 14.f, 0, 0, TEXT("04_canyon") },
		{ 14.1f, 2, 518000.0, nullptr },
		{ 19.f, 0, 0, TEXT("05_skybase") },
		{ 19.1f, 2, 646000.0, nullptr },
		{ 27.f, 0, 0, TEXT("06_boss") },
		{ 27.1f, 3, 0, nullptr },          // pause
		{ 28.f, 0, 0, TEXT("07_paused") },
		{ 29.f, 4, 0, nullptr },           // quit
	};
	while (AutoShotStep < UE_ARRAY_COUNT(Steps) && AutoShotTime >= Steps[AutoShotStep].At)
	{
		const FStep& St = Steps[AutoShotStep++];
		switch (St.Op)
		{
		case 0: Shot(St.Name); break;
		case 1: RequestNeutralReset(); break;
		case 2: Jump(St.Arg); break;
		case 3: TogglePause(); break;
		default: FPlatformMisc::RequestExit(false); break;
		}
	}
}

void AFlightPawn::Respawn()
{
	S = Course->Checkpoints.IsValidIndex(CheckpointIndex) ? Course->Checkpoints[CheckpointIndex] : 0.0;
	Off = Vel = FVector2D::ZeroVector;
	Shield = MaxShield;
	Course->ResetFrom(S);
	ShipPivot->SetVisibility(true, true);
	InvulnerableUntil = GetWorld()->GetTimeSeconds() + 3.f;
	UpdateCamera(0.f, true);
	ShowBanner(TEXT("チェックポイントから再開"), 2.2f);
	BeginCountdown(2.5f);
}

void AFlightPawn::BeginCountdown(float Seconds)
{
	State = EFlightState::Countdown;
	CountdownLeft = Seconds;
}

void AFlightPawn::RequestNeutralReset()
{
	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		Attitude->ResetNeutral();
		SeenEpoch = Attitude->GetSourceEpoch();
		if (!Attitude->GetState().bAvailable)
		{
			return;
		}
	}
	ShipRoll = ShipPitch = ShipYaw = 0.f;
	if (State == EFlightState::AwaitNeutral || State == EFlightState::Paused)
	{
		bInputLost = false;
		BeginCountdown(3.f);
	}
}

void AFlightPawn::TogglePause()
{
	if (State == EFlightState::Playing || State == EFlightState::Countdown)
	{
		State = EFlightState::Paused;
	}
	else if (State == EFlightState::Paused)
	{
		BeginCountdown(2.f);
	}
}

void AFlightPawn::RestartCourse()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void AFlightPawn::ReturnToTop()
{
	UGameplayStatics::OpenLevel(this, ATitleGameMode::TitleMapName);
}
