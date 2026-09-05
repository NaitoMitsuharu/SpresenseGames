#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MazeBallPawn.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class USpringArmComponent;
class UCameraComponent;
class APlayerController;

struct FImpactSparkAnim
{
	int32 InstanceIndex = INDEX_NONE;
	FVector StartLocation = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	double StartTime = 0.0;
	float Lifetime = 0.3f;
	float Length = 5.f;
	bool bActive = false;
};

/**
 * Physics ball controlled by Spresense IMU tilt.
 * - Tilt (roll/pitch) rolls the ball
 * - A sharp upward acceleration spike (lifting the Spresense quickly) makes it jump
 * - Keyboard fallback in editor: arrow keys / WASD to tilt, Space to jump
 */
UCLASS()
class SPRESENSEMAZE_API AMazeBallPawn : public APawn
{
	GENERATED_BODY()

public:
	AMazeBallPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Ball mesh (physics enabled) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<UCameraComponent> Camera;

	/** Soft fill light following the ball (visibility inside shadowed corridors) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball")
	TObjectPtr<class UPointLightComponent> BallLight;

	/** Short-lived emissive fragments emitted by horizontal impacts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UInstancedStaticMeshComponent> ImpactSparks;

	/** Cyan burst emitted on takeoff and landing so a jump reads clearly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UInstancedStaticMeshComponent> JumpBurst;

	/** Small dark droplets kicked up while rolling across mud. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UInstancedStaticMeshComponent> MudSplashes;

	/** Fading cyan trail shown while on a no-wall-hit streak. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UInstancedStaticMeshComponent> StreakTrail;


	// ---- Tilt tuning ----

	/** Acceleration applied at full tilt (cm/s^2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	float TiltForce = 1500.f;

	/** Tilt angle (deg) mapped to full force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	float MaxTiltDeg = 30.f;

	/** Ignore tilt below this angle (deg) to avoid drift */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt")
	float TiltDeadZoneDeg = 3.f;

	// ---- Jump tuning ----

	/** Jump triggers when |accel| exceeds this (m/s^2). Gravity alone is ~9.8 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	float JumpAccelThreshold = 17.f;

	/** Upward impulse velocity (cm/s). Keep low enough not to clear the maze walls */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	float JumpImpulse = 400.f;

	/** Minimum seconds between jumps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	float JumpCooldown = 0.8f;

	/** Only allow jumping while touching the ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	bool bRequireGrounded = true;

	/**
	 * World Z the top of the ball must not pass when jumping. The maze sets this
	 * just under the wall tops every tick, which caps a jump taken from a hurdle
	 * (where the ball already starts 60cm up) without shortening a floor jump.
	 * A very large value means unrestricted.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Jump")
	float JumpCeilingZ = 1.e9f;

	// ---- Misc ----

	/** Respawn when the ball falls this far (cm) below the stage start height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Respawn")
	float FallRespawnDepth = 500.f;

	/** IMU sample rate requested from the SPR-BOX (Hz). IMU-Adv recommends 480 */
	UPROPERTY(EditAnywhere, Category = "SprBox")
	int32 ImuRate = 480;

	// ---- Haptics ----

	/** Vibrate the phone when the ball hits a wall */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
	bool bVibrateOnWallHit = true;

	/** Minimum impact speed (cm/s) to trigger vibration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
	float VibrationMinImpactSpeed = 60.f;

	/** Impact speed (cm/s) mapped to maximum vibration intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Haptics")
	float VibrationMaxImpactSpeed = 550.f;

	/** Minimum wall impact speed that emits visible sparks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback")
	float SparkMinImpactSpeed = 35.f;

	/** Impact speed that reaches the maximum spark count and travel speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback")
	float SparkMaxImpactSpeed = 520.f;

	/** Fired when a jump is triggered (hook sounds/effects here in Blueprint) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Jump")
	void OnJumped();

	/** True while the device is tilted past ~90 deg: input is ignored and the HUD shows a warning */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprBox")
	bool bOrientationWarning = false;

	// ---- Calibration / drift correction ----

	/**
	 * Start calibration.
	 * bFullReset=true: IMU-Adv 2-step calibration wizard (SDK startCalibration/
	 *   finishCalibration; 5s flat -> change attitude -> 5s still).
	 * bFullReset=false: instant SDK lightReset (drift fix, usable mid-game).
	 */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	void StartCalibration(bool bFullReset);

	/** Proceed to calibration step 2 after repositioning the device */
	UFUNCTION(BlueprintCallable, Category = "SprBox")
	void ContinueCalibration();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	bool IsCalibrating() const { return CalibPhase != 0; }

	/** 0=idle, 1=step1 sampling, 2=waiting for user (change attitude), 3=step2 sampling */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	int32 GetCalibrationPhase() const { return CalibPhase; }

	/** 0..1 progress of the current sampling step */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	float GetCalibrationProgress() const;

	/**
	 * True while a calibration sampling step (phase 1 or 3) is running AND
	 * the SPR-BOX IMU sequence counter has advanced within the last
	 * CalibDataStallSeconds - i.e. real samples are actually arriving right
	 * now, not just that the board handshake completed. Lets the wizard show
	 * the difference between "collecting" and "connected but no data" while
	 * it is still happening, instead of only failing silently at the end.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	bool IsCalibDataFlowing() const { return bCalibDataFlowing; }

	/** Set by the maze each tick: 1=normal, lower on ice/mud (control authority) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprBox")
	float SurfaceControlScale = 1.f;

	/** Set by the maze each tick: true while standing on a mud cell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface")
	bool bOnMud = false;

	/** Set by the maze each tick: true once DistanceSinceLastWallHit clears the no-hit streak threshold. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface")
	bool bStreakActive = false;

	/** Distance rolled (cm) since the last wall contact; reset to 0 on every wall hit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface")
	float DistanceSinceLastWallHit = 0.f;

	/** Latest tilt values for the HUD attitude indicator (degrees) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprBox")
	float CurrentRollDeg = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprBox")
	float CurrentPitchDeg = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprBox")
	float CurrentYawDeg = 0.f;

	/**
	 * Short-lived linear-acceleration impulse above/below gravity (m/s^2).
	 * Glass and mud use this so a physical jolt has a gameplay consequence.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SprBox")
	float GetMotionShock() const { return MotionShock; }

	/** Teleport the ball to a location and stop all motion (used on stage change) */
	UFUNCTION(BlueprintCallable, Category = "Ball")
	void ResetTo(const FVector& Location);

	/** Enable/disable tilt & jump input (disabled during stage-clear celebration) */
	UFUNCTION(BlueprintCallable, Category = "Ball")
	void SetControlEnabled(bool bEnabled) { bControlEnabled = bEnabled; }

	/** True when the Android on-screen fallback controller is being used. */
	bool IsTouchControllerActive() const { return bTouchControllerActive; }

	/** Input source currently driving the maze tilt. */
	bool IsUsingSprBoxInput() const { return bUsingSprBoxInput; }
	bool IsUsingDeviceMotion() const { return bUsingDeviceMotion; }

	/** Current touch stick input: X=forward, Y=right, each in -1..1. */
	FVector2D GetTouchMoveInput() const { return TouchMoveInput; }

	/** True while the on-screen jump button is held. */
	bool IsTouchJumpPressed() const { return bTouchJumpPressed; }
	float GetLastJumpTime() const { return LastJumpTime; }

	/** Shared responsive layout used by both touch hit testing and HUD drawing. */
	void GetTouchControlLayout(const FVector2D& ViewSize, FVector2D& OutStickCenter,
		float& OutStickRadius, FVector2D& OutJumpCenter, float& OutJumpRadius) const;

private:
	bool bControlEnabled = true;

public:

protected:
	void TryJump();
	bool IsGrounded() const;
	void UpdatePool(UInstancedStaticMeshComponent* ISM, TArray<FImpactSparkAnim>& Pool);
	void EmitImpactSparks(const FHitResult& Hit, float Intensity);
	void EmitJumpBurst(bool bLanding);
	void EmitMudSplash();
	void EmitStreakTrail();
	void UpdateTouchControls(APlayerController* PC);
	void ResetTouchControls();

	UFUNCTION()
	void HandleWallHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	FTransform StartTransform;
	float LastJumpTime = -100.f;
	float LastVibrationTime = -100.f;
	float LastSparkTime = -100.f;
	int64 LastImuSequence = 0;
	float MotionShock = 0.f;

	// Phone motion is used only while an online SPR-BOX sample is unavailable.
	bool bUsingDeviceMotion = false;
	bool bUsingSprBoxInput = false;
	bool bDeviceMotionCalibrated = false;
	float DeviceRollOffsetDeg = 0.f;
	float DevicePitchOffsetDeg = 0.f;
	float DeviceYawOffsetDeg = 0.f;
	float LastRawDeviceRollDeg = 0.f;
	float LastRawDevicePitchDeg = 0.f;
	float LastRawDeviceYawDeg = 0.f;
	TArray<FImpactSparkAnim> ImpactSparkPool;
	int32 NextImpactSpark = 0;
	TArray<FImpactSparkAnim> JumpBurstPool;
	int32 NextJumpBurst = 0;
	TArray<FImpactSparkAnim> MudSplashPool;
	int32 NextMudSplash = 0;
	TArray<FImpactSparkAnim> StreakTrailPool;
	int32 NextStreakTrail = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ImpactSparkMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> JumpBurstMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MudSplashMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StreakTrailMID;

	// Ground-contact tracking: last solid-ground position (for hole/fall respawn
	// and landing-effect detection) and the previous tick's grounded state.
	FVector LastGroundedLocation = FVector::ZeroVector;
	bool bWasGroundedLastTick = true;
	float LastMudSplashTime = -100.f;
	float LastStreakTrailTime = -100.f;

	// Android fallback controller state. Finger ids are ETouchIndex values.
	bool bTouchControllerActive = false;
	bool bTouchJumpPressed = false;
	FVector2D TouchMoveInput = FVector2D::ZeroVector;
	int32 StickTouchIndex = INDEX_NONE;
	int32 JumpTouchIndex = INDEX_NONE;

	// IMU-Adv 2-step calibration state
	int32 CalibPhase = 0;                 // 0 idle / 1 step1 / 2 await user / 3 step2
	float CalibPhaseStart = 0.f;
	static constexpr float CalibStepSeconds = 5.f; // per SDK demo (IMU_ADV_CALIBRATION_DURATION_MS)

	bool bCalibDataFlowing = false;
	int64 LastCalibCheckSequence = -1;
	float LastCalibDataTime = -100.f;
	static constexpr float CalibDataStallSeconds = 0.3f;
};
