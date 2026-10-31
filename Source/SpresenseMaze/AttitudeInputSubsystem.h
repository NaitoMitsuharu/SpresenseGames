#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AttitudeInputSubsystem.generated.h"

class APlayerController;

/** Which physical input is currently producing the attitude. */
UENUM(BlueprintType)
enum class EAttitudeSource : uint8
{
	None,          // nothing usable (Android without sensors): games fall back to touch
	SprBox,        // Spresense IMU-Adv stream, only while samples keep arriving
	DeviceMotion,  // the phone's own attitude sensor
	Keyboard       // PC / editor
};

/**
 * One frame's attitude as both games see it. Angles are relative to the
 * neutral pose captured by ResetNeutral() and already smoothed, so the value a
 * HUD prints is exactly the value gameplay applies.
 *
 * Sign convention (shared by every source):
 *   Roll  + = right edge down
 *   Pitch + = far (top) edge down, i.e. tilted away from the player
 *   Yaw   + = turned clockwise seen from above
 */
USTRUCT(BlueprintType)
struct FAttitudeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Attitude") EAttitudeSource Source = EAttitudeSource::None;
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") float RollDeg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") float PitchDeg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") float YawDeg = 0.f;

	/** Unsmoothed, neutral-relative angles (for over-tilt guards). */
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") float RawRollDeg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") float RawPitchDeg = 0.f;

	/** Acceleration including gravity (m/s^2). |Accel| is ~9.8 at rest for every source. */
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") FVector Accel = FVector::ZeroVector;

	/** Platform time (FPlatformTime::Seconds) when the active source last produced a new sample. */
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") double SampleTime = 0.0;

	/** Increments with every new sample from the active source. */
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") int64 SampleCount = 0;

	/** True while the active source is delivering fresh samples. */
	UPROPERTY(BlueprintReadOnly, Category = "Attitude") bool bAvailable = false;
};

/**
 * Shared attitude input for the maze and the flight game.
 *
 * Owns the SPR-BOX connection, chooses the input source (fresh Spresense
 * samples first, then the phone's sensor, then the keyboard), keeps one
 * neutral pose per source and smooths the result. The state is computed at
 * most once per frame, lazily, so whichever actor asks first in a frame gets
 * that frame's samples without depending on tick order.
 */
UCLASS()
class SPRESENSEMAZE_API UAttitudeInputSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Spresense samples older than this mean the board is treated as disconnected. */
	static constexpr double SprBoxStaleSeconds = 0.5;

	/** Connect to SPR-BOX once per app run (later calls are ignored). */
	void EnsureSprBox(int32 Rate = 480);

	/** This frame's state. */
	const FAttitudeState& GetState();

	/** Make the current way the device is held the neutral pose of the active source. */
	UFUNCTION(BlueprintCallable, Category = "Attitude")
	void ResetNeutral();

	/** Changes every time the active source changes (games pause on a change). */
	int32 GetSourceEpoch() { GetState(); return SourceEpoch; }

	/** Seconds since the active source's last sample. */
	double GetSampleAge();

	/** SPR-BOX link details for status displays. */
	void GetSprBoxStatus(bool& bServiceOnline, bool& bBoardOnline, bool& bStreaming);

	/** Short Latin label for HUDs: SPRESENSE / ANDROID / KEYBOARD / TOUCH. */
	static FString SourceLabel(EAttitudeSource Source);

private:
	struct FNeutral
	{
		float Roll = 0.f;
		float Pitch = 0.f;
		float Yaw = 0.f;
		bool bSet = false;
	};

	void Update();
	APlayerController* FindPlayerController() const;

	FAttitudeState State;
	uint64 LastUpdateFrame = MAX_uint64;
	int32 SourceEpoch = 0;
	bool bSprBoxStarted = false;

	// Per-source raw readings in the shared sign convention.
	float RawRoll = 0.f;
	float RawPitch = 0.f;
	float RawYaw = 0.f;

	int64 LastSprBoxSequence = 0;
	double LastSprBoxSampleTime = -1.0;
	bool bSprBoxService = false;
	bool bSprBoxBoard = false;

	FVector LastDeviceTilt = FVector(MAX_flt);
	double LastDeviceSampleTime = -1.0;

	float KeyRoll = 0.f;
	float KeyPitch = 0.f;
	float KeyYaw = 0.f;

	FNeutral Neutral[4];
	bool bSnapSmoothing = true;
};
