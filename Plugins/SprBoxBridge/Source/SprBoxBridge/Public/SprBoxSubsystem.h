#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SprBoxSubsystem.generated.h"

/** Latest IMU sample from the Spresense (SPR-BOX) */
USTRUCT(BlueprintType)
struct FSprBoxImuData
{
	GENERATED_BODY()

	/** Accelerometer (m/s^2) */
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Ax = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Ay = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Az = 0.f;

	/** Gyroscope (rad/s) */
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Gx = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Gy = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Gz = 0.f;

	/** Attitude from Madgwick filter (degrees) */
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Roll = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Pitch = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") float Yaw = 0.f;

	/** Increments every new sample. 0 means no data received yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") int64 Sequence = 0;

	/** True if at least one sample has been received */
	UPROPERTY(BlueprintReadOnly, Category = "SprBox") bool bValid = false;
};

/**
 * GameInstance subsystem exposing the SPR-BOX SDK (sprboxlib-std .aar) to C++/Blueprint.
 * On non-Android platforms all calls are safe no-ops (bValid stays false),
 * so gameplay code can always use keyboard fallback in the editor.
 */
UCLASS()
class SPRBOXBRIDGE_API USprBoxSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/**
	 * Connect to the SPR-BOX Service app and start IMU streaming.
	 * Rate: 15/30/60/120/240/480/960 Hz, AccelRange: 2/4/8/16 g,
	 * GyroRange: 125/250/500/1000/2000/4000 dps, Beta: Madgwick filter 0.0-1.0
	 */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	bool InitSprBox(int32 Rate = 60, int32 AccelRange = 4, int32 GyroRange = 500, float Beta = 0.1f);

	/** Stop streaming and release the service connection */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	void ShutdownSprBox();

	/** Latest IMU sample (polled; call every Tick) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	FSprBoxImuData GetImuData() const;

	/** True while a Spresense board is connected over USB */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	bool IsSpresenseConnected() const;

	/** Connection status: SPR-BOX Service app bound / Spresense board online */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	void GetConnectionStatus(bool& bServiceOnline, bool& bBoardOnline) const;

	/** Quick attitude reset via the SDK (IMU-Adv lightReset) - instant drift fix */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	void RecalibrateImu();

	/** Start one IMU-Adv calibration step (keep the board still; ~5s per step) */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	bool StartImuCalibration(bool bReuseRunningStream);

	/** Finish the current IMU-Adv calibration step. Returns true on success */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	bool FinishImuCalibration();

private:
	bool bInitialized = false;
};
