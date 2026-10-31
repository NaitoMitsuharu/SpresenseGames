#include "MazeBallPawn.h"

#include "AttitudeInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "SprBoxSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AMazeBallPawn::AMazeBallPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	RootComponent = BallMesh;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}
	BallMesh->SetSimulatePhysics(true);
	BallMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	BallMesh->SetGenerateOverlapEvents(true); // needed for goal-tile detection
	BallMesh->SetNotifyRigidBodyCollision(true); // needed for wall-hit haptics
	BallMesh->SetLinearDamping(0.3f);
	BallMesh->SetAngularDamping(0.2f);

	ImpactSparks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ImpactSparks"));
	ImpactSparks->SetupAttachment(BallMesh);
	ImpactSparks->SetMobility(EComponentMobility::Movable);
	ImpactSparks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ImpactSparks->SetCastShadow(false);
	ImpactSparks->SetUsingAbsoluteLocation(true);
	ImpactSparks->SetUsingAbsoluteRotation(true);
	ImpactSparks->SetUsingAbsoluteScale(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ImpactSparks->SetStaticMesh(CubeMesh.Object);
	}

	auto SetupFeedbackISM = [this](const TCHAR* Name) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* ISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		ISM->SetupAttachment(BallMesh);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->SetUsingAbsoluteLocation(true);
		ISM->SetUsingAbsoluteRotation(true);
		ISM->SetUsingAbsoluteScale(true);
		if (CubeMesh.Succeeded())
		{
			ISM->SetStaticMesh(CubeMesh.Object);
		}
		return ISM;
	};
	JumpBurst = SetupFeedbackISM(TEXT("JumpBurst"));
	MudSplashes = SetupFeedbackISM(TEXT("MudSplashes"));
	StreakTrail = SetupFeedbackISM(TEXT("StreakTrail"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(BallMesh);
	SpringArm->SetUsingAbsoluteRotation(true); // don't spin with the ball
	// A higher three-quarter view shows the maze as a precision instrument while
	// retaining enough wall height to read depth and jump clearance.
	SpringArm->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	SpringArm->TargetArmLength = 1250.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 5.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->FieldOfView = 72.f;

	// Soft light that follows the ball so it stays visible inside the maze.
	// Absolute location+rotation: repositioned every Tick so it never orbits
	// or spins with the rolling ball.
	BallLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BallLight"));
	BallLight->SetupAttachment(BallMesh);
	BallLight->SetUsingAbsoluteLocation(true);
	BallLight->SetUsingAbsoluteRotation(true);
	BallLight->SetIntensity(900.f);
	BallLight->SetAttenuationRadius(380.f);
	BallLight->SetLightColor(FLinearColor(0.82f, 0.9f, 1.f));
	BallLight->SetCastShadows(false);


	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AMazeBallPawn::BeginPlay()
{
	Super::BeginPlay();

	StartTransform = GetActorTransform();
	LastGroundedLocation = StartTransform.GetLocation();

	BallMesh->OnComponentHit.AddDynamic(this, &AMazeBallPawn::HandleWallHit);

	// Enable clicks/touches for the HUD buttons (home screen, calibration, etc.)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		PC->bEnableClickEvents = true;
		PC->bEnableTouchEvents = true;
	}

	if (UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeGlow.M_MazeGlow")))
	{
		ImpactSparkMID = UMaterialInstanceDynamic::Create(GlowMat, this);
		ImpactSparkMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.28f, 0.025f));
		ImpactSparks->SetMaterial(0, ImpactSparkMID);

		// Cyan matches the project's "things that move you" color grammar
		// (README: boost pads / warps), distinct from the orange wall sparks.
		JumpBurstMID = UMaterialInstanceDynamic::Create(GlowMat, this);
		JumpBurstMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.25f, 0.85f, 1.f));
		JumpBurst->SetMaterial(0, JumpBurstMID);

		MudSplashMID = UMaterialInstanceDynamic::Create(GlowMat, this);
		MudSplashMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.16f, 0.1f, 0.06f));
		MudSplashes->SetMaterial(0, MudSplashMID);

		StreakTrailMID = UMaterialInstanceDynamic::Create(GlowMat, this);
		StreakTrailMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.9f, 1.f));
		StreakTrail->SetMaterial(0, StreakTrailMID);
	}
	ImpactSparks->SetWorldTransform(FTransform::Identity);
	JumpBurst->SetWorldTransform(FTransform::Identity);
	MudSplashes->SetWorldTransform(FTransform::Identity);
	StreakTrail->SetWorldTransform(FTransform::Identity);

	// A plain pearl-ceramic sphere belongs to the instrument instead of reading as
	// a reused sports-ball asset.
	{
		UMaterialInterface* BallMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!BallMat)
		{
			BallMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeTile.M_MazeTile"));
		}
		if (!BallMat && BallMesh->GetStaticMesh())
		{
			BallMat = BallMesh->GetStaticMesh()->GetMaterial(0);
		}
		if (BallMat)
		{
			UMaterialInstanceDynamic* BodyMID = UMaterialInstanceDynamic::Create(BallMat, this);
			const FLinearColor Pearl(0.48f, 0.57f, 0.68f);
			BodyMID->SetVectorParameterValue(TEXT("Color"), Pearl);
			BodyMID->SetVectorParameterValue(TEXT("BaseColor"), Pearl);
			BodyMID->SetScalarParameterValue(TEXT("Roughness"), 0.1f);
			BodyMID->SetScalarParameterValue(TEXT("Metallic"), 0.12f);
			BallMesh->SetMaterial(0, BodyMID);
		}
	}

	// The shared attitude service owns the SPR-BOX link so the maze and the
	// flight game never bind it twice when switching levels.
	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		Attitude->EnsureSprBox(ImuRate);
	}
}

void AMazeBallPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	// A jolt remains available briefly so the maze can consume it even when the
	// 480 Hz IMU sample and the game-physics tick do not land on the same frame.
	MotionShock = FMath::Max(MotionShock - DeltaSeconds * 7.f, 0.f);

	// Keep the follow-light above the ball without inheriting its spin
	if (BallLight)
	{
		BallLight->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 120.f));
	}
	UpdatePool(ImpactSparks, ImpactSparkPool);
	UpdatePool(JumpBurst, JumpBurstPool);
	UpdatePool(MudSplashes, MudSplashPool);
	UpdatePool(StreakTrail, StreakTrailPool);

	// Track solid ground for hole/fall respawn (respawn near where the ball last
	// had footing, not the stage start) and for landing-burst detection.
	const bool bGroundedNow = IsGrounded();
	if (bGroundedNow)
	{
		LastGroundedLocation = GetActorLocation();
	}
	if (bGroundedNow && !bWasGroundedLastTick)
	{
		EmitJumpBurst(/*bLanding=*/true);
	}
	bWasGroundedLastTick = bGroundedNow;

	// Fell into a hole / off the maze -> respawn near the last solid ground
	// (relative to stage start height, so it works even with a landscape far
	// below the floating maze)
	if (GetActorLocation().Z < StartTransform.GetLocation().Z - FallRespawnDepth)
	{
		ResetTo(LastGroundedLocation);
	}

	// Advance the IMU-Adv calibration wizard even while control is disabled -
	// the "調整" button that starts it lives on the home screen, where
	// bControlEnabled is false, so gating this on bControlEnabled would freeze
	// the wizard at whatever phase it was in the moment the player opened it.
	if (CalibPhase != 0)
	{
		if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
		{
			if (CalibPhase == 1 || CalibPhase == 3)
			{
				const FSprBoxImuData Imu = SprBox->GetImuData();
				if (Imu.Sequence != LastCalibCheckSequence)
				{
					LastCalibCheckSequence = Imu.Sequence;
					LastCalibDataTime = GetWorld()->GetTimeSeconds();
				}
				bCalibDataFlowing = (GetWorld()->GetTimeSeconds() - LastCalibDataTime) < CalibDataStallSeconds;
			}
			else
			{
				bCalibDataFlowing = false;
			}
			const float Elapsed = GetWorld()->GetTimeSeconds() - CalibPhaseStart;
			if ((CalibPhase == 1 || CalibPhase == 3) && Elapsed >= CalibStepSeconds)
			{
				SprBox->FinishImuCalibration();
				if (CalibPhase == 1)
				{
					CalibPhase = 2; // wait for the user to change the attitude
					CalibPhaseStart = GetWorld()->GetTimeSeconds();
				}
				else
				{
					CalibPhase = 0; // done - high-precision estimation running
				}
			}
		}
		CurrentRollDeg = 0.f;
		CurrentPitchDeg = 0.f;
		CurrentYawDeg = 0.f;
		return;
	}

	if (!bControlEnabled)
	{
		ResetTouchControls();
		return;
	}

	float TiltX = 0.f; // forward(+)/backward(-), from pitch
	float TiltY = 0.f; // right(+)/left(-), from roll
	bool bHasImu = false;
	bUsingSprBoxInput = false;
	bUsingDeviceMotion = false;
	auto MapTilt = [this](float AngleDeg) -> float
	{
		if (FMath::Abs(AngleDeg) < TiltDeadZoneDeg)
		{
			return 0.f;
		}
		return FMath::Clamp(AngleDeg / MaxTiltDeg, -1.f, 1.f);
	};

	// Attitude comes from the shared service (fresh SPR-BOX samples first, then
	// the phone sensor, then the keyboard). It is already neutral-relative and
	// smoothed, so the HUD readout below is exactly what moves the ball.
	bool bForceTouchForReview = false;
#if !UE_BUILD_SHIPPING && (PLATFORM_ANDROID || PLATFORM_IOS)
	bForceTouchForReview = FParse::Param(FCommandLine::Get(), TEXT("MazeForceTouch"));
#endif
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	if (Attitude && !bForceTouchForReview)
	{
		const FAttitudeState& Att = Attitude->GetState();
		if (Att.bAvailable)
		{
			bHasImu = true;
			bUsingSprBoxInput = Att.Source == EAttitudeSource::SprBox;
			bUsingDeviceMotion = Att.Source == EAttitudeSource::DeviceMotion;
			CurrentRollDeg = Att.RollDeg;
			CurrentPitchDeg = Att.PitchDeg;
			CurrentYawDeg = Att.YawDeg;

			// A different source has a different neutral pose: hold the ball
			// until the player re-centres with 姿勢リセット instead of letting the
			// ball lurch toward whatever the new sensor happens to read.
			const int32 Epoch = Attitude->GetSourceEpoch();
			if (SeenSourceEpoch != INDEX_NONE && Epoch != SeenSourceEpoch &&
				LastSourceSeen != EAttitudeSource::None)
			{
				bAwaitingNeutral = true;
			}
			SeenSourceEpoch = Epoch;
			LastSourceSeen = Att.Source;

			// Over-tilt guard with hysteresis: block input past ~95deg, release under 80deg
			if (bUsingSprBoxInput)
			{
				const float MaxAngle = FMath::Max(FMath::Abs(Att.RawRollDeg), FMath::Abs(Att.RawPitchDeg));
				if (!bOrientationWarning && MaxAngle > 95.f)
				{
					bOrientationWarning = true;
				}
				else if (bOrientationWarning && MaxAngle < 80.f)
				{
					bOrientationWarning = false;
				}
			}
			else
			{
				bOrientationWarning = false;
			}
			if (bOrientationWarning || bAwaitingNeutral)
			{
				// Freeze tilt input (physics keeps running so the ball settles naturally)
				ResetTouchControls();
				return;
			}

			TiltX = MapTilt(CurrentPitchDeg); // tilt away from you -> ball rolls forward
			TiltY = MapTilt(CurrentRollDeg);  // tilt right -> ball rolls right

			// Jump: a sharp acceleration spike from lifting the device quickly.
			if (Att.SampleCount != LastImuSequence)
			{
				LastImuSequence = Att.SampleCount;
				const float AccelMag = Att.Accel.Size();
				MotionShock = FMath::Max(MotionShock, FMath::Abs(AccelMag - 9.80665f));
				if (bUsingSprBoxInput)
				{
					// At rest |a| ~= 9.8 m/s^2 (gravity).
					if (AccelMag > JumpAccelThreshold)
					{
						TryJump();
					}
				}
				else if (bUsingDeviceMotion)
				{
					if (FMath::Abs(AccelMag - 9.80665f) > FMath::Max(JumpAccelThreshold - 9.80665f, 5.f))
					{
						TryJump();
					}
				}
			}
			if (Att.Source == EAttitudeSource::Keyboard)
			{
				if (APlayerController* PC = Cast<APlayerController>(GetController());
					PC && PC->WasInputKeyJustPressed(EKeys::SpaceBar))
				{
					TryJump();
				}
			}
		}
	}

	// Keyboard fallback for editor / desktop testing
	if (!bHasImu)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			// This is the last-resort mobile fallback for devices without motion data.
#if PLATFORM_ANDROID || PLATFORM_IOS
			bTouchControllerActive = true;
			UpdateTouchControls(PC);
			TiltX += TouchMoveInput.X;
			TiltY += TouchMoveInput.Y;
#else
			ResetTouchControls();
#endif

			if (PC->IsInputKeyDown(EKeys::Up)    || PC->IsInputKeyDown(EKeys::W)) { TiltX += 1.f; }
			if (PC->IsInputKeyDown(EKeys::Down)  || PC->IsInputKeyDown(EKeys::S)) { TiltX -= 1.f; }
			if (PC->IsInputKeyDown(EKeys::Right) || PC->IsInputKeyDown(EKeys::D)) { TiltY += 1.f; }
			if (PC->IsInputKeyDown(EKeys::Left)  || PC->IsInputKeyDown(EKeys::A)) { TiltY -= 1.f; }
			if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) { TryJump(); }
		}
	}
	else
	{
		ResetTouchControls();
	}

	if (TiltX != 0.f || TiltY != 0.f)
	{
		// Camera-relative: X = camera forward (projected), Y = camera right
		const FRotator YawRot(0.f, SpringArm->GetComponentRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right   = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
		const FVector Force = (Forward * TiltX + Right * TiltY) * TiltForce * SurfaceControlScale;
		BallMesh->AddForce(Force, NAME_None, /*bAccelChange=*/true);
	}

	// No-wall-hit streak distance (reset in HandleWallHit); MazeGenerator sets
	// bStreakActive once this clears its 5-cell threshold.
	const float Speed = BallMesh->GetPhysicsLinearVelocity().Size();
	DistanceSinceLastWallHit += Speed * DeltaSeconds;

	if (bOnMud && Speed > 40.f && Now - LastMudSplashTime >= 0.08f)
	{
		LastMudSplashTime = Now;
		EmitMudSplash();
	}
	if (bStreakActive && Now - LastStreakTrailTime >= 0.03f)
	{
		LastStreakTrailTime = Now;
		EmitStreakTrail();
	}
}

void AMazeBallPawn::GetTouchControlLayout(const FVector2D& ViewSize,
	FVector2D& OutStickCenter, float& OutStickRadius,
	FVector2D& OutJumpCenter, float& OutJumpRadius) const
{
	// Scale from a 1080px-tall landscape reference while retaining comfortable
	// physical targets at lower resolutions.
	const float Scale = FMath::Clamp(ViewSize.Y / 1080.f, 0.7f, 1.35f);
	const float Edge = 42.f * Scale;
	OutStickRadius = 100.f * Scale;
	OutJumpRadius = 78.f * Scale;
	OutStickCenter = FVector2D(Edge + OutStickRadius, ViewSize.Y - Edge - OutStickRadius);
	OutJumpCenter = FVector2D(ViewSize.X - Edge - OutJumpRadius, ViewSize.Y - Edge - OutJumpRadius);
}

void AMazeBallPawn::UpdateTouchControls(APlayerController* PC)
{
	if (!PC)
	{
		ResetTouchControls();
		return;
	}

	int32 ViewWidth = 0;
	int32 ViewHeight = 0;
	PC->GetViewportSize(ViewWidth, ViewHeight);
	if (ViewWidth <= 0 || ViewHeight <= 0)
	{
		ResetTouchControls();
		return;
	}

	FVector2D StickCenter;
	FVector2D JumpCenter;
	float StickRadius = 0.f;
	float JumpRadius = 0.f;
	GetTouchControlLayout(FVector2D(ViewWidth, ViewHeight),
		StickCenter, StickRadius, JumpCenter, JumpRadius);

	auto ReadTouch = [PC](int32 Finger, FVector2D& OutLocation) -> bool
	{
		if (Finger < 0 || Finger >= static_cast<int32>(ETouchIndex::CursorPointerIndex))
		{
			return false;
		}
		float X = 0.f;
		float Y = 0.f;
		bool bPressed = false;
		PC->GetInputTouchState(static_cast<ETouchIndex::Type>(Finger), X, Y, bPressed);
		OutLocation = FVector2D(X, Y);
		return bPressed;
	};

	// Release fingers that ended before assigning newly pressed contacts.
	FVector2D TouchLocation;
	if (StickTouchIndex != INDEX_NONE && !ReadTouch(StickTouchIndex, TouchLocation))
	{
		StickTouchIndex = INDEX_NONE;
		TouchMoveInput = FVector2D::ZeroVector;
	}
	if (JumpTouchIndex != INDEX_NONE && !ReadTouch(JumpTouchIndex, TouchLocation))
	{
		JumpTouchIndex = INDEX_NONE;
		bTouchJumpPressed = false;
	}

	for (int32 Finger = 0; Finger < static_cast<int32>(ETouchIndex::CursorPointerIndex); ++Finger)
	{
		if (!ReadTouch(Finger, TouchLocation))
		{
			continue;
		}

		if (StickTouchIndex == INDEX_NONE && Finger != JumpTouchIndex &&
			FVector2D::Distance(TouchLocation, StickCenter) <= StickRadius * 1.35f)
		{
			StickTouchIndex = Finger;
		}
		if (JumpTouchIndex == INDEX_NONE && Finger != StickTouchIndex &&
			FVector2D::Distance(TouchLocation, JumpCenter) <= JumpRadius * 1.35f)
		{
			JumpTouchIndex = Finger;
			bTouchJumpPressed = true;
			TryJump();
		}
	}

	if (StickTouchIndex != INDEX_NONE && ReadTouch(StickTouchIndex, TouchLocation))
	{
		FVector2D Delta = (TouchLocation - StickCenter) / FMath::Max(StickRadius, 1.f);
		if (Delta.SizeSquared() > 1.f)
		{
			Delta.Normalize();
		}
		// Screen Y grows downward: upward drag is positive forward input.
		TouchMoveInput = FVector2D(-Delta.Y, Delta.X);
	}
	if (JumpTouchIndex != INDEX_NONE)
	{
		bTouchJumpPressed = ReadTouch(JumpTouchIndex, TouchLocation);
	}
}

void AMazeBallPawn::ResetTouchControls()
{
	bTouchControllerActive = false;
	bTouchJumpPressed = false;
	TouchMoveInput = FVector2D::ZeroVector;
	StickTouchIndex = INDEX_NONE;
	JumpTouchIndex = INDEX_NONE;
}

void AMazeBallPawn::ResetTo(const FVector& Location)
{
	BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BallMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	StartTransform.SetLocation(Location); // fall-respawn now returns to the new stage start
	LastGroundedLocation = Location;
}

void AMazeBallPawn::StartCalibration(bool bFullReset)
{
	if (CalibPhase != 0)
	{
		return;
	}
	USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>();
	bool bServiceOnline = false;
	bool bBoardOnline = false;
	if (SprBox)
	{
		SprBox->GetConnectionStatus(bServiceOnline, bBoardOnline);
	}
	if (!bBoardOnline)
	{
		// Phone motion only needs a neutral-angle capture. Both the home-screen
		// adjustment and the in-game reset intentionally complete instantly.
		ResetAttitudeNeutral();
		return;
	}

	if (!bFullReset)
	{
		// Instant re-centre: the current hold becomes neutral for both games.
		ResetAttitudeNeutral();
		return;
	}

	// Full 2-step wizard, step 1: board flat & still.
	// BeginPlay() already auto-starts continuous estimation via InitSprBox()
	// whenever the board is connected at launch, so the SDK stream is normally
	// already running by the time the player opens this wizard. Passing
	// bReuseRunningStream=true lets ImuAdvController.startCalibration() attach
	// to that stream instead of being blocked by its own
	// "isCommandStarted && !reuseRunningStream" guard; it still starts a fresh
	// stream correctly when nothing is running yet, since the SDK checks
	// (!reuseRunningStream || !isCommandStarted) before deciding to (re)send
	// the stream-start command.
	if (SprBox && SprBox->StartImuCalibration(/*bReuseRunningStream=*/true))
	{
		CalibPhase = 1;
		CalibPhaseStart = GetWorld()->GetTimeSeconds();
		LastCalibCheckSequence = -1;
		LastCalibDataTime = -100.f;
		bCalibDataFlowing = false;
	}
}

void AMazeBallPawn::ResetAttitudeNeutral()
{
	if (UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>())
	{
		Attitude->ResetNeutral();
		SeenSourceEpoch = Attitude->GetSourceEpoch();
	}
	bAwaitingNeutral = false;
	CurrentRollDeg = 0.f;
	CurrentPitchDeg = 0.f;
	CurrentYawDeg = 0.f;
}

void AMazeBallPawn::ContinueCalibration()
{
	if (CalibPhase != 2)
	{
		return;
	}
	// Step 2: reuse the running stream, board in a DIFFERENT attitude, still
	if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
	{
		if (SprBox->StartImuCalibration(/*bReuseRunningStream=*/true))
		{
			CalibPhase = 3;
			CalibPhaseStart = GetWorld()->GetTimeSeconds();
			LastCalibCheckSequence = -1;
			LastCalibDataTime = -100.f;
			bCalibDataFlowing = false;
			return;
		}
	}
	CalibPhase = 0;
}

float AMazeBallPawn::GetCalibrationProgress() const
{
	if (CalibPhase == 1 || CalibPhase == 3)
	{
		return FMath::Clamp((GetWorld()->GetTimeSeconds() - CalibPhaseStart) / CalibStepSeconds, 0.f, 1.f);
	}
	return 0.f;
}

void AMazeBallPawn::TryJump()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastJumpTime < JumpCooldown)
	{
		return;
	}
	if (bRequireGrounded && !IsGrounded())
	{
		return;
	}

	// Cap the launch speed so the apex keeps the top of the ball under JumpCeilingZ.
	// Standing on the floor this is slack and the full JumpImpulse is used; standing
	// on a hurdle the ball already starts a hurdle-height up, so the jump is
	// shortened just enough that it can never carry the ball over a wall.
	float Impulse = JumpImpulse;
	const float Gravity = FMath::Abs(GetWorld()->GetGravityZ());
	if (JumpCeilingZ < 1.e8f && Gravity > 1.f)
	{
		const float BallTopZ = GetActorLocation().Z + BallMesh->Bounds.SphereRadius;
		const float HeadRoom = FMath::Max(JumpCeilingZ - BallTopZ, 0.f);
		Impulse = FMath::Min(Impulse, FMath::Sqrt(2.f * Gravity * HeadRoom));
	}
	if (Impulse < 1.f)
	{
		return; // no room to jump at all - keep the cooldown free for the next try
	}

	LastJumpTime = Now;
	BallMesh->AddImpulse(FVector(0.f, 0.f, Impulse), NAME_None, /*bVelChange=*/true);
	OnJumped();
	EmitJumpBurst(/*bLanding=*/false);
}

void AMazeBallPawn::HandleWallHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Only horizontal impacts (walls), not rolling contact with the floor
	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.5f)
	{
		return;
	}

	// A real wall contact breaks the no-hit streak regardless of impact speed.
	DistanceSinceLastWallHit = 0.f;

	const float Now = GetWorld()->GetTimeSeconds();
	// Chaos may report the callback after resolving part of the velocity. Convert
	// its normal impulse back to an equivalent speed and keep whichever signal is
	// stronger, so a hard hit still produces a hard response on Android.
	const FVector BallVelocity = BallMesh->GetPhysicsLinearVelocity();
	const float VelocitySpeed = FMath::Abs(FVector::DotProduct(BallVelocity, Hit.ImpactNormal));
	const float ImpulseSpeed = NormalImpulse.Size() / FMath::Max(BallMesh->GetMass(), 0.1f);
	// Some mobile Chaos builds provide neither the pre-solve normal velocity nor a
	// useful impulse in the callback. Planar speed is therefore a conservative
	// fallback, but only after this callback has already identified a wall contact.
	const float PlanarSpeed = BallVelocity.Size2D() * 0.7f;
	const float ImpactSpeed = FMath::Max3(VelocitySpeed, ImpulseSpeed, PlanarSpeed);

	if (ImpactSpeed >= SparkMinImpactSpeed && Now - LastSparkTime >= 0.065f)
	{
		const float SparkIntensity = FMath::Clamp(
			(ImpactSpeed - SparkMinImpactSpeed) / FMath::Max(SparkMaxImpactSpeed - SparkMinImpactSpeed, 1.f),
			0.08f, 1.f);
		EmitImpactSparks(Hit, SparkIntensity);
		LastSparkTime = Now;
	}

	if (bVibrateOnWallHit && ImpactSpeed >= VibrationMinImpactSpeed && Now - LastVibrationTime >= 0.15f)
	{
		LastVibrationTime = Now;
		// A concave curve (Pow > 1) keeps weak grazes clearly weak and only lets
		// genuinely fast hits approach the max - the previous linear map with a
		// 0.1 intensity floor and a 70ms-wide duration range made every hit past
		// the threshold feel about the same.
		const float T = FMath::Clamp(
			(ImpactSpeed - VibrationMinImpactSpeed) / FMath::Max(VibrationMaxImpactSpeed - VibrationMinImpactSpeed, 1.f),
			0.f, 1.f);
		const float Shaped = FMath::Pow(T, 1.6f);
		const float Intensity = FMath::Lerp(0.15f, 1.f, Shaped);
		const float Duration = FMath::Lerp(0.035f, 0.25f, Shaped);
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->PlayDynamicForceFeedback(Intensity, Duration, true, true, true, true);
		}
	}
}

void AMazeBallPawn::EmitImpactSparks(const FHitResult& Hit, float Intensity)
{
	if (!ImpactSparks || !GetWorld())
	{
		return;
	}

	constexpr int32 MaxSparkPool = 64;
	const int32 SparkCount = 5 + FMath::RoundToInt(Intensity * 11.f);
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	FRandomStream Random(FMath::RoundToInt(GetWorld()->GetTimeSeconds() * 1000.f) ^ SparkCount * 7919);

	for (int32 Spark = 0; Spark < SparkCount; ++Spark)
	{
		int32 PoolIndex = INDEX_NONE;
		if (ImpactSparkPool.Num() < MaxSparkPool)
		{
			PoolIndex = ImpactSparkPool.AddDefaulted();
		}
		else
		{
			PoolIndex = NextImpactSpark;
			NextImpactSpark = (NextImpactSpark + 1) % MaxSparkPool;
		}

		FImpactSparkAnim& Anim = ImpactSparkPool[PoolIndex];
		const float Side = Random.FRandRange(-0.95f, 0.95f);
		const float Lift = Random.FRandRange(0.18f, 0.9f);
		const FVector Direction = (Normal * Random.FRandRange(0.35f, 0.75f) + Tangent * Side + FVector::UpVector * Lift).GetSafeNormal();
		const float Speed = Random.FRandRange(145.f, 235.f) + Intensity * Random.FRandRange(180.f, 390.f);
		Anim.StartLocation = Hit.ImpactPoint + Normal * 7.f + FVector::UpVector * Random.FRandRange(-5.f, 8.f);
		Anim.Velocity = Direction * Speed;
		Anim.StartTime = GetWorld()->GetTimeSeconds();
		Anim.Lifetime = Random.FRandRange(0.28f, 0.4f) + Intensity * 0.22f;
		Anim.Length = Random.FRandRange(7.f, 11.f) + Intensity * Random.FRandRange(5.f, 12.f);
		Anim.bActive = true;

		FTransform Transform(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction),
			Anim.StartLocation, FVector(Anim.Length / 100.f, 0.05f, 0.05f));
		if (Anim.InstanceIndex == INDEX_NONE)
		{
			Anim.InstanceIndex = ImpactSparks->AddInstance(Transform, true);
		}
		else
		{
			ImpactSparks->UpdateInstanceTransform(Anim.InstanceIndex, Transform, true, false, true);
		}
	}
	ImpactSparks->MarkRenderStateDirty();
}

void AMazeBallPawn::UpdatePool(UInstancedStaticMeshComponent* ISM, TArray<FImpactSparkAnim>& Pool)
{
	if (!ISM || !GetWorld() || Pool.Num() == 0)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	bool bDirty = false;
	for (FImpactSparkAnim& Anim : Pool)
	{
		if (!Anim.bActive || Anim.InstanceIndex == INDEX_NONE)
		{
			continue;
		}

		const float Age = static_cast<float>(Now - Anim.StartTime);
		if (Age >= Anim.Lifetime)
		{
			FTransform Hidden(FQuat::Identity, Anim.StartLocation, FVector::ZeroVector);
			ISM->UpdateInstanceTransform(Anim.InstanceIndex, Hidden, true, false, true);
			Anim.bActive = false;
			bDirty = true;
			continue;
		}

		const FVector Gravity(0.f, 0.f, GetWorld()->GetGravityZ());
		const FVector Location = Anim.StartLocation + Anim.Velocity * Age + Gravity * (0.5f * Age * Age);
		const FVector CurrentVelocity = Anim.Velocity + Gravity * Age;
		const float Fade = FMath::Square(1.f - Age / Anim.Lifetime);
		const FVector FacingSource = CurrentVelocity.IsNearlyZero() ? FVector::ForwardVector : CurrentVelocity.GetSafeNormal();
		FTransform Transform(FQuat::FindBetweenNormals(FVector::ForwardVector, FacingSource),
			Location, FVector(Anim.Length * Fade / 100.f, 0.05f * Fade, 0.05f * Fade));
		ISM->UpdateInstanceTransform(Anim.InstanceIndex, Transform, true, false, true);
		bDirty = true;
	}
	if (bDirty)
	{
		ISM->MarkRenderStateDirty();
	}
}

void AMazeBallPawn::EmitJumpBurst(bool bLanding)
{
	if (!JumpBurst || !GetWorld())
	{
		return;
	}

	constexpr int32 MaxJumpBurstPool = 32;
	const int32 Count = bLanding ? (4 + FMath::RandRange(0, 2)) : (10 + FMath::RandRange(0, 3));
	const FVector BasePos = GetActorLocation() - FVector(0.f, 0.f, BallMesh->Bounds.SphereRadius * 0.9f);
	FRandomStream Random(FMath::RoundToInt(GetWorld()->GetTimeSeconds() * 1000.f) ^ (bLanding ? 0x51ED : 0x9137));

	for (int32 i = 0; i < Count; ++i)
	{
		int32 PoolIndex = INDEX_NONE;
		if (JumpBurstPool.Num() < MaxJumpBurstPool)
		{
			PoolIndex = JumpBurstPool.AddDefaulted();
		}
		else
		{
			PoolIndex = NextJumpBurst;
			NextJumpBurst = (NextJumpBurst + 1) % MaxJumpBurstPool;
		}

		FImpactSparkAnim& Anim = JumpBurstPool[PoolIndex];
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		const float Radial = bLanding ? Random.FRandRange(0.7f, 1.f) : Random.FRandRange(0.3f, 0.85f);
		const FVector RadialDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const float UpBias = bLanding ? Random.FRandRange(0.1f, 0.35f) : Random.FRandRange(0.25f, 0.6f);
		const FVector Direction = (RadialDir * Radial + FVector::UpVector * UpBias).GetSafeNormal();
		const float Speed = bLanding ? Random.FRandRange(110.f, 220.f) : Random.FRandRange(180.f, 340.f);

		Anim.StartLocation = BasePos + RadialDir * Random.FRandRange(6.f, 22.f);
		Anim.Velocity = Direction * Speed;
		Anim.StartTime = GetWorld()->GetTimeSeconds();
		Anim.Lifetime = Random.FRandRange(0.22f, 0.36f);
		Anim.Length = Random.FRandRange(6.f, 10.f);
		Anim.bActive = true;

		FTransform Transform(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction),
			Anim.StartLocation, FVector(Anim.Length / 100.f, 0.05f, 0.05f));
		if (Anim.InstanceIndex == INDEX_NONE)
		{
			Anim.InstanceIndex = JumpBurst->AddInstance(Transform, true);
		}
		else
		{
			JumpBurst->UpdateInstanceTransform(Anim.InstanceIndex, Transform, true, false, true);
		}
	}
	JumpBurst->MarkRenderStateDirty();
}

void AMazeBallPawn::EmitMudSplash()
{
	if (!MudSplashes || !GetWorld())
	{
		return;
	}

	constexpr int32 MaxMudSplashPool = 40;
	const FVector Velocity = BallMesh->GetPhysicsLinearVelocity();
	const FVector BackDir = -Velocity.GetSafeNormal2D();
	const int32 Count = 2 + FMath::RandRange(0, 2);
	const FVector BasePos = GetActorLocation() - FVector(0.f, 0.f, BallMesh->Bounds.SphereRadius * 0.95f);
	FRandomStream Random(FMath::RoundToInt(GetWorld()->GetTimeSeconds() * 1000.f) ^ 0x6D75);

	for (int32 i = 0; i < Count; ++i)
	{
		int32 PoolIndex = INDEX_NONE;
		if (MudSplashPool.Num() < MaxMudSplashPool)
		{
			PoolIndex = MudSplashPool.AddDefaulted();
		}
		else
		{
			PoolIndex = NextMudSplash;
			NextMudSplash = (NextMudSplash + 1) % MaxMudSplashPool;
		}

		FImpactSparkAnim& Anim = MudSplashPool[PoolIndex];
		const float Side = Random.FRandRange(-0.6f, 0.6f);
		const FVector Tangent = FVector::CrossProduct(FVector::UpVector, BackDir).GetSafeNormal();
		const FVector Direction = (BackDir * Random.FRandRange(0.5f, 0.9f) + Tangent * Side + FVector::UpVector * Random.FRandRange(0.5f, 0.9f)).GetSafeNormal();
		const float Speed = Random.FRandRange(60.f, 140.f);

		Anim.StartLocation = BasePos + FVector(Random.FRandRange(-6.f, 6.f), Random.FRandRange(-6.f, 6.f), 0.f);
		Anim.Velocity = Direction * Speed;
		Anim.StartTime = GetWorld()->GetTimeSeconds();
		Anim.Lifetime = Random.FRandRange(0.25f, 0.4f);
		Anim.Length = Random.FRandRange(3.f, 6.f);
		Anim.bActive = true;

		FTransform Transform(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction),
			Anim.StartLocation, FVector(Anim.Length / 100.f, 0.04f, 0.04f));
		if (Anim.InstanceIndex == INDEX_NONE)
		{
			Anim.InstanceIndex = MudSplashes->AddInstance(Transform, true);
		}
		else
		{
			MudSplashes->UpdateInstanceTransform(Anim.InstanceIndex, Transform, true, false, true);
		}
	}
	MudSplashes->MarkRenderStateDirty();
}

void AMazeBallPawn::EmitStreakTrail()
{
	if (!StreakTrail || !GetWorld())
	{
		return;
	}

	constexpr int32 MaxStreakTrailPool = 24;
	int32 PoolIndex = INDEX_NONE;
	if (StreakTrailPool.Num() < MaxStreakTrailPool)
	{
		PoolIndex = StreakTrailPool.AddDefaulted();
	}
	else
	{
		PoolIndex = NextStreakTrail;
		NextStreakTrail = (NextStreakTrail + 1) % MaxStreakTrailPool;
	}

	// Near-zero drift, opposite the ball's motion: this reads as a fading trail
	// left behind the ball rather than a launched fragment.
	const FVector Velocity = BallMesh->GetPhysicsLinearVelocity();
	const FVector Drift = -Velocity.GetSafeNormal() * 25.f;
	const FVector Direction = Drift.IsNearlyZero() ? FVector::ForwardVector : Drift.GetSafeNormal();

	FImpactSparkAnim& Anim = StreakTrailPool[PoolIndex];
	Anim.StartLocation = GetActorLocation();
	Anim.Velocity = Drift;
	Anim.StartTime = GetWorld()->GetTimeSeconds();
	Anim.Lifetime = 0.3f;
	Anim.Length = BallMesh->Bounds.SphereRadius * 0.9f;
	Anim.bActive = true;

	FTransform Transform(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction),
		Anim.StartLocation, FVector(Anim.Length / 100.f, 0.06f, 0.06f));
	if (Anim.InstanceIndex == INDEX_NONE)
	{
		Anim.InstanceIndex = StreakTrail->AddInstance(Transform, true);
	}
	else
	{
		StreakTrail->UpdateInstanceTransform(Anim.InstanceIndex, Transform, true, false, true);
	}
	StreakTrail->MarkRenderStateDirty();
}

bool AMazeBallPawn::IsGrounded() const
{
	const FVector Start = GetActorLocation();
	const float Radius = BallMesh->Bounds.SphereRadius;
	const FVector End = Start - FVector(0.f, 0.f, Radius + 10.f);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}
