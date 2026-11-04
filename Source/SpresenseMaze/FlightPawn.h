#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FlightModels.h"
#include "FlightScore.h"
#include "FlightPawn.generated.h"

class AFlightCourse;
class UCameraComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UAudioComponent;
class USoundWaveProcedural;

UENUM()
enum class EFlightState : uint8
{
	AwaitNeutral,  // start, or the input source changed: hold still and press 姿勢リセット
	Countdown,     // short count-in after re-centring or resuming
	Playing,
	Paused,
	Destroyed,     // shot down; respawns at the last checkpoint
	Cleared
};

/**
 * Auto-forward ship. Roll steers left/right, pitch climbs/dives inside a
 * bounded box around the course line; the model shows the live roll/pitch
 * and the yaw (clamped to +-15 deg) on the nose. Lasers fire along the nose
 * line automatically, so the reticle the HUD draws is the real line of fire.
 */
UCLASS()
class SPRESENSEMAZE_API AFlightPawn : public APawn
{
	GENERATED_BODY()

public:
	AFlightPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- tuning ----
	/** Tilt (deg) that commands full lateral speed. */
	UPROPERTY(EditAnywhere, Category = "Flight") float MaxTiltDeg = 24.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float DeadZoneDeg = 1.8f;
	UPROPERTY(EditAnywhere, Category = "Flight") float LateralSpeed = 2800.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float VerticalSpeed = 1900.f;
	/** Tilting the far edge down dives (flight-stick convention). */
	UPROPERTY(EditAnywhere, Category = "Flight") bool bInvertPitch = false;
	UPROPERTY(EditAnywhere, Category = "Flight") float YawLimitDeg = 15.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float NosePitchLimitDeg = 10.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float FireInterval = 0.11f;
	/** Distance of the far reticle; the twin guns converge here. */
	UPROPERTY(EditAnywhere, Category = "Flight") float AimDistance = 6000.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float AccelerationResponse = 12.f;
	UPROPERTY(EditAnywhere, Category = "Flight") float BrakingResponse = 17.f;
	UPROPERTY(EditAnywhere, Category = "Flight") bool bGestureDodge = true;

	// ---- state read by the course and HUD ----
	EFlightState State = EFlightState::AwaitNeutral;
	double S = 0.0;
	FVector2D Off = FVector2D::ZeroVector;
	FVector2D Vel = FVector2D::ZeroVector;
	float Shield = 100.f;
	static constexpr float MaxShield = 100.f;
	float CountdownLeft = 0.f;
	float HitFlash = 0.f;
	float HealFlash = 0.f;
	float TrailTime = 0.f;
	float TrailStrength = 0.f;
	FString Banner;
	float BannerTime = 0.f;
	int32 CheckpointIndex = 0;
	bool bInputLost = false;
	float NearSurface = 0.f;
	static constexpr float DodgeDuration = 0.26f;
	static constexpr float DodgeRecharge = 1.6f;
	float DodgeTime = 0.f, DodgeCooldown = 0.f;
	int32 DodgeDirection = 1;
	float DodgePulse() const { return DodgeTime > 0 ? FMath::Sin(PI * DodgeTime / DodgeDuration) : 0.f; }
	float SteeringAmount() const { return CommandX; }
	bool RequestDodge(int32 Direction);
	bool CanDodge(int32 Direction) const;
	void ToggleGestureDodge();

	/** Ship rotation relative to the path frame (deg): applied attitude. */
	float ShipRoll = 0.f;
	float ShipPitch = 0.f;
	float ShipYaw = 0.f;

	/** Unit nose direction in path space (X along course, Y right, Z up). */
	FVector NoseDir() const;
	/** Path-space point the guns converge on at Distance ahead. */
	FVector AimPoint(float Distance) const;
	FVector PathToWorld(double PS, const FVector2D& POff) const;

	bool IsInvulnerable() const;
	void ApplyDamage(float Amount, const FVector& FromWorld);
	void OnRingPassed(int32 Streak, bool bShieldBonus);
	void OnRingMissed();
	void OnBossDefeated();
	void ShowBanner(const FString& Text, float Seconds = 2.2f);

	// ---- HUD actions ----
	void RequestNeutralReset();
	void TogglePause();
	void RestartCourse();
	void ReturnToTop();

	AFlightCourse* GetCourse() const { return Course; }
	FVector GetCameraLocation() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ShipPivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> EngineLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Trail;

private:
	void UpdateInput(float Dt, bool bSteer);
	void UpdateShip(float Dt);
	void UpdateCamera(float Dt, bool bSnap);
	void UpdateTrail(float Dt);
	void UpdateWind(float Dt);
	void UpdateDodge(float Dt);
	void Respawn();
	void BeginCountdown(float Seconds);
	void Vibrate(float Intensity, float Duration);
	/** Dev-only (-FlightAutoShot): fly a scripted tour and save screenshots. */
	void TickAutoShot(float Dt);
	void TickUpgradeReview(float Dt);
	void TickHudReview(float Dt);
	void TickModelReview(float Dt);
	bool bAutoShot = false;
	bool bUpgradeReview = false;
	bool bHudReview = false;
	bool bModelReview = false;
	float AutoShotTime = 0.f;
	int32 AutoShotStep = 0;
	float AutoRoll = 0.f;
	float AutoPitch = 0.f;

	UPROPERTY() TObjectPtr<AFlightCourse> Course;
	FFlightShipParts Parts;
	FRotator AileronBaseL, AileronBaseR;

	float FireTimer = 0.f;
	bool bNextGunLeft = false;
	float InvulnerableUntil = 0.f;
	float DestroyedTime = 0.f;
	float ClearAt = -1.f;
	int32 SeenEpoch = INDEX_NONE;
	float CommandX = 0.f;
	float CommandZ = 0.f;
	FVector CamOffsetSmoothed = FVector::ZeroVector;
	FVector CamLookSmoothed = FVector::ZeroVector;

	struct FTrailSeg
	{
		FVector A, B;
		float Age = 0.f;
		float Life = 0.6f;
		bool bActive = false;
	};
	TArray<FTrailSeg> TrailSegs;
	int32 NextTrail = 0;
	FVector LastTipL = FVector::ZeroVector;
	FVector LastTipR = FVector::ZeroVector;
	bool bHaveLastTips = false;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TrailMID;
	UPROPERTY() TObjectPtr<UAudioComponent> WindAudio;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> WindWave;
	TArray<uint8> WindBuffer;
	FlightHandling::FFlickGesture Flick;
	float DodgeTravel = 640.f;
	int32 LastSteerSide = 1;
};
