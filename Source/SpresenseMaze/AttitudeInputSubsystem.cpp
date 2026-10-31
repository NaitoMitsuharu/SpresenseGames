#include "AttitudeInputSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "SprBoxSubsystem.h"

namespace
{
	constexpr float Gravity = 9.80665f;

	// Time constants for the exponential smoother. The Spresense stream is
	// 480 Hz and clean, the phone sensor arrives at frame rate with more jitter.
	float SmoothingSeconds(EAttitudeSource Source)
	{
		switch (Source)
		{
		case EAttitudeSource::SprBox: return 0.035f;
		case EAttitudeSource::DeviceMotion: return 0.07f;
		default: return 0.f;
		}
	}
}

void UAttitudeInputSubsystem::EnsureSprBox(int32 Rate)
{
	if (bSprBoxStarted)
	{
		return;
	}
	bSprBoxStarted = true;
	if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
	{
		// IMU-Adv recommended ranges (from the SDK demo): 16g / 2000dps
		SprBox->InitSprBox(Rate, /*AccelRange=*/16, /*GyroRange=*/2000, /*Beta=*/0.1f);
	}
}

const FAttitudeState& UAttitudeInputSubsystem::GetState()
{
	if (LastUpdateFrame != GFrameCounter)
	{
		LastUpdateFrame = GFrameCounter;
		Update();
	}
	return State;
}

double UAttitudeInputSubsystem::GetSampleAge()
{
	const FAttitudeState& S = GetState();
	return S.SampleTime > 0.0 ? FPlatformTime::Seconds() - S.SampleTime : 1.e6;
}

void UAttitudeInputSubsystem::GetSprBoxStatus(bool& bServiceOnline, bool& bBoardOnline, bool& bStreaming)
{
	GetState();
	bServiceOnline = bSprBoxService;
	bBoardOnline = bSprBoxBoard;
	bStreaming = LastSprBoxSampleTime > 0.0 &&
		FPlatformTime::Seconds() - LastSprBoxSampleTime < SprBoxStaleSeconds;
}

FString UAttitudeInputSubsystem::SourceLabel(EAttitudeSource Source)
{
	switch (Source)
	{
	case EAttitudeSource::SprBox: return TEXT("SPRESENSE");
	case EAttitudeSource::DeviceMotion: return TEXT("ANDROID");
	case EAttitudeSource::Keyboard: return TEXT("KEYBOARD");
	default: return TEXT("TOUCH");
	}
}

APlayerController* UAttitudeInputSubsystem::FindPlayerController() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetFirstLocalPlayerController() : nullptr;
}

void UAttitudeInputSubsystem::ResetNeutral()
{
	GetState();
	FNeutral& N = Neutral[static_cast<int32>(State.Source)];
	N.Roll = RawRoll;
	N.Pitch = RawPitch;
	N.Yaw = RawYaw;
	N.bSet = true;
	bSnapSmoothing = true;
	State.RollDeg = State.PitchDeg = State.YawDeg = 0.f;
	State.RawRollDeg = State.RawPitchDeg = 0.f;
}

void UAttitudeInputSubsystem::Update()
{
	const double Now = FPlatformTime::Seconds();
	const float Dt = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, 0.1f);
	const EAttitudeSource PrevSource = State.Source;

	EAttitudeSource Source = EAttitudeSource::None;
	bool bNewSample = false;
	FVector Accel = FVector::ZeroVector;

	// 1) Spresense: only while samples keep arriving. A completed USB handshake
	//    alone says nothing about whether the IMU-Adv stream is delivering.
	bSprBoxService = bSprBoxBoard = false;
	if (USprBoxSubsystem* SprBox = GetGameInstance()->GetSubsystem<USprBoxSubsystem>())
	{
		SprBox->GetConnectionStatus(bSprBoxService, bSprBoxBoard);
		const FSprBoxImuData Imu = SprBox->GetImuData();
		if (Imu.bValid && Imu.Sequence != LastSprBoxSequence)
		{
			LastSprBoxSequence = Imu.Sequence;
			LastSprBoxSampleTime = Now;
			bNewSample = true;
		}
		const bool bFresh = LastSprBoxSampleTime > 0.0 && Now - LastSprBoxSampleTime < SprBoxStaleSeconds;
		if (bSprBoxBoard && Imu.bValid && bFresh)
		{
			Source = EAttitudeSource::SprBox;
			// IMU-Adv attitude is gravity-referenced: flat is 0/0, which the
			// maze has always relied on. Pitch + = tilted away, roll + = right down.
			RawRoll = Imu.Roll;
			RawPitch = Imu.Pitch;
			RawYaw = Imu.Yaw;
			Accel = FVector(Imu.Ax, Imu.Ay, Imu.Az);
			FNeutral& N = Neutral[static_cast<int32>(EAttitudeSource::SprBox)];
			if (!N.bSet)
			{
				// Heading has no absolute zero: start from wherever the board points.
				N.Yaw = RawYaw;
				N.bSet = true;
			}
		}
	}

	APlayerController* PC = FindPlayerController();

#if PLATFORM_ANDROID || PLATFORM_IOS
	// 2) The phone's own attitude, screen-orientation corrected by UE. Its Y
	//    component follows the long-axis tilt and Z the short-axis tilt.
	if (!PC && Source == EAttitudeSource::None && PrevSource == EAttitudeSource::DeviceMotion)
	{
		// No controller for a frame or two while a level loads: keep the last
		// phone reading instead of reporting a source switch.
		Source = EAttitudeSource::DeviceMotion;
		Accel = State.Accel;
	}
	else if (PC)
	{
		FVector Tilt, RotationRate, DeviceGravity, DeviceAcceleration;
		PC->GetInputMotionState(Tilt, RotationRate, DeviceGravity, DeviceAcceleration);
		if (DeviceGravity.SizeSquared() > 0.2f)
		{
			if (!Tilt.Equals(LastDeviceTilt, 1.e-6f))
			{
				LastDeviceTilt = Tilt;
				LastDeviceSampleTime = Now;
				if (Source == EAttitudeSource::None)
				{
					bNewSample = true;
				}
			}
			if (Source == EAttitudeSource::None)
			{
				Source = EAttitudeSource::DeviceMotion;
				RawRoll = FMath::RadiansToDegrees(Tilt.Z);
				RawPitch = FMath::RadiansToDegrees(Tilt.Y);
				RawYaw = FMath::RadiansToDegrees(Tilt.X);
				Accel = (DeviceGravity + DeviceAcceleration) * Gravity;
				FNeutral& N = Neutral[static_cast<int32>(EAttitudeSource::DeviceMotion)];
				if (!N.bSet)
				{
					// A phone has no "flat" convention: the first pose is neutral.
					N.Roll = RawRoll;
					N.Pitch = RawPitch;
					N.Yaw = RawYaw;
					N.bSet = true;
				}
			}
		}
	}
#else
	// 3) Keyboard emulates a held tilt so both games keep one code path.
	if (Source == EAttitudeSource::None && !PC && PrevSource == EAttitudeSource::Keyboard)
	{
		Source = EAttitudeSource::Keyboard;  // level transition, see above
		Accel = State.Accel;
	}
	else if (Source == EAttitudeSource::None && PC)
	{
		constexpr float KeyTiltDeg = 35.f;
		auto Axis = [PC](FKey Pos, FKey PosAlt, FKey Neg, FKey NegAlt) -> float
		{
			float V = 0.f;
			if (PC->IsInputKeyDown(Pos) || PC->IsInputKeyDown(PosAlt)) { V += 1.f; }
			if (PC->IsInputKeyDown(Neg) || PC->IsInputKeyDown(NegAlt)) { V -= 1.f; }
			return V;
		};
		auto Approach = [Dt](float& Value, float Target)
		{
			const float Rate = FMath::Abs(Target) > FMath::Abs(Value) ? 240.f : 300.f;
			Value = FMath::FInterpConstantTo(Value, Target, Dt, Rate);
		};
		Approach(KeyPitch, KeyTiltDeg * Axis(EKeys::W, EKeys::Up, EKeys::S, EKeys::Down));
		Approach(KeyRoll, KeyTiltDeg * Axis(EKeys::D, EKeys::Right, EKeys::A, EKeys::Left));
		Approach(KeyYaw, 15.f * Axis(EKeys::E, EKeys::E, EKeys::Q, EKeys::Q));
		Source = EAttitudeSource::Keyboard;
		RawRoll = KeyRoll;
		RawPitch = KeyPitch;
		RawYaw = KeyYaw;
		Accel = FVector(0.f, 0.f, Gravity);
		bNewSample = true;
	}
#endif

	if (Source != PrevSource)
	{
		++SourceEpoch;
		bSnapSmoothing = true;
	}

	State.Source = Source;
	State.bAvailable = Source != EAttitudeSource::None;
	if (!State.bAvailable)
	{
		State.RollDeg = State.PitchDeg = State.YawDeg = 0.f;
		State.RawRollDeg = State.RawPitchDeg = 0.f;
		State.Accel = FVector::ZeroVector;
		return;
	}

	if (bNewSample)
	{
		++State.SampleCount;
		State.SampleTime = Source == EAttitudeSource::DeviceMotion ? LastDeviceSampleTime : Now;
	}
	else if (Source == EAttitudeSource::SprBox)
	{
		State.SampleTime = LastSprBoxSampleTime;
	}
	State.Accel = Accel;

	const FNeutral& N = Neutral[static_cast<int32>(Source)];
	const float Roll = FMath::FindDeltaAngleDegrees(N.Roll, RawRoll);
	const float Pitch = FMath::FindDeltaAngleDegrees(N.Pitch, RawPitch);
	const float Yaw = FMath::FindDeltaAngleDegrees(N.Yaw, RawYaw);
	State.RawRollDeg = Roll;
	State.RawPitchDeg = Pitch;

	const float Tau = SmoothingSeconds(Source);
	if (bSnapSmoothing || Tau <= 0.f)
	{
		State.RollDeg = Roll;
		State.PitchDeg = Pitch;
		State.YawDeg = Yaw;
		bSnapSmoothing = false;
	}
	else
	{
		const float Alpha = 1.f - FMath::Exp(-Dt / Tau);
		State.RollDeg += Alpha * (Roll - State.RollDeg);
		State.PitchDeg += Alpha * (Pitch - State.PitchDeg);
		State.YawDeg += Alpha * FMath::FindDeltaAngleDegrees(State.YawDeg, Yaw);
	}
}
