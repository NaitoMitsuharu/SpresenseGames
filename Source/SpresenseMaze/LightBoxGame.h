#pragma once

#include "CoreMinimal.h"
#include "LightBoxPerformance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameHudBase.h"
#include "LightBoxSimulation.h"
#include "LightBoxSensors.h"
#include "LightBoxReadiness.h"
#include "LightBoxGame.generated.h"

class ISceneViewExtension;
class UCameraComponent;
class UPostProcessComponent;
class SWidget;
class FLightBoxBatchReview;
class SLightBoxControls;
class ULightBoxPointCloud;
class APlayerController;

UCLASS()
class SPRESENSEMAZE_API ALightBoxGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ALightBoxGameMode();
};

UCLASS()
class SPRESENSEMAZE_API ALightBoxPawn : public APawn
{
	GENERATED_BODY()
public:
	/** True once the base pose has formed and a storyboard is flying between poses. */
	bool IsPlayingStory() const { return bPlayingStory; }
	ALightBoxPawn();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	/** A wave through the scene: from a clap (travelling away from the mic) or a shake (Dir zero: spreading from the centre). */
	void Pulse(const FVector& Dir = FVector::ZeroVector, float Strength = .7f, float Lead = 0);
	void ToggleCameraOrbit();
	void ResetCameraView();
	bool IsCameraOrbiting() const { return !FMath::IsNearlyZero(ViewOrbitSpeed); }
	/** What the phone sensors are doing right now (for the HUD). */
	const FLightBoxSensorReading& GetSensorReading() const { return Felt; }
	/** Player sensor switches (the dock badges). A nearing hand, when on, raises the aurora curtain; tilt moves the motes. */
	bool IsSensorOn(ELightBoxSensor S) const { return S < ELightBoxSensor::Count && bSensorOn[int32(S)]; }
	/** Where the warming finger rests (or last rested), 0..1 across the viewport. */
	FVector2D GetWarmPoint() const { return WarmPoint; }
	/** Shared monotonic clock for the sound wave and its delayed particle burst. */
	double GetClapStartedAt() const { return ClapStartedAt; }
	float GetClapStrength() const { return ClapStrength; }
	/** +1: the microphone is at the right screen edge, -1: at the left (set by the ambience overlay). */
	void SetMicSide(int32 Side) { MicSide = Side < 0 ? -1 : 1; }
	void SetSensorOn(ELightBoxSensor S, bool bOn);
	FLightBoxSimulation Simulation;

	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY() TObjectPtr<ULightBoxPointCloud> PointCloud;
private:
    friend class FLightBoxAmbienceLifecycleTest;
	friend class FLightBoxBatchReview;
	TSharedPtr<FLightBoxBatchReview> BatchReview;
	void ConfigureParticles();
	void ApplyScene(const FLightBoxStyle& Style);
	void UpdateCameraInput(APlayerController* PC,bool Blocked);
	void UpdateCameraView(const FLightBoxStyle& Style,float Dt);
	void ApplySensorLook(float Dt);
	FLightBoxSensorReader Sensors;
	/** This frame's sensors after the scene's per-sensor sensitivity. */
	FLightBoxSensorReading Felt;
	TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> AtelierView;
	float BaseBloom = 0.7f;
	bool bSensorOn[int32(ELightBoxSensor::Count)] = { true, true, true, false, true, true };
	/** Smoothed light-level gain on the motes (1 = as authored). */
	float LightGain = 1.f;
	float HandHue = 0.f;
	bool bTiltSeen = false;
	FVector2D WarmPoint = FVector2D(.6f, .55f);
	/** The clap wave and its sound rings share one clock; the front reaches the scene centre as the rings do. */
	double ClapStartedAt = -1;
	float ClapStrength = 0, ClapCooldown = 0;
	int32 MicSide = 1;
	float BaseVignette = .35f;
#if !UE_BUILD_SHIPPING
	FString ReviewSensor;
	bool bReviewClapped = false;
#endif
	/** Palm-hold warming: a single touch resting still on the scene. */
	double HoldStarted = -1;
	FVector2D HoldOrigin = FVector2D::ZeroVector;
	bool bHoldMoved = false, bHoldWarm = false;
	uint64 AppliedRevision = MAX_uint64;
	float ObjectBurst = 0;
	float ShakeCooldown = 0;
	float LastYaw = 0;
	int32 SourceEpoch = -1;
	int64 LastSample = 0;
	bool bShakeArmed = false;
	float Elapsed = 0;
	bool bAutoShot = false;
	double UIShotAt=0;
	FString UIShotName;
	float LastAspect=0;
	FTransform CameraStart,CameraTarget;
	float CameraBlendAge=100;
	float CameraBlendSeconds=1;
	FBox CameraBounds=FBox(ForceInit);
	FVector2D ViewAngles=FVector2D::ZeroVector,LastViewTouch=FVector2D::ZeroVector;
	float ViewDistance=1,ViewOrbitSpeed=0,LastPinchDistance=0;
	bool bViewDragging=false;
	FString AppliedContent;
	/** Continuous clip playback starts once the first formation is complete. */
	bool bPlayingStory=false;
	/** Startup particle-budget calibration (first launch / after an update / on request). */
	struct FCalibrationRun { int32 Level=0; double FormedAt=0, LastFrame=0; bool bAssembling=false; TArray<float> Frames; TArray<LightBoxPerformance::FSample> Samples; };
	TUniquePtr<FCalibrationRun> Calibration;
	void TickCalibration();
	FLightBoxStyle Budgeted(const FLightBoxStyle& Style) const;
    FString AppliedCamera;
};

UCLASS()
class SPRESENSEMAZE_API ALightBoxHUD : public AGameHudBase
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName Name) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void OpenWords();
	/** 0 connection and key, 1 AI model, 2 particle limit. */
	void OpenSettings(int32 Section = 0);
	void OpenExamples();
	void OpenJournal();
	void OpenSetup();
	void OpenMenu();
#if !UE_BUILD_SHIPPING
	/** Review capture: keyboard open with a composition. */
	void ReviewKeyboard();
	void ReviewSensors();
#endif
	void CloseDialog();
	bool HasDialog() const;
	bool IsModalOpen() const { return Dialog.IsValid(); }
private:
	void EnsureControls();
	void UpdateSetupDialog();
	bool bSetupOpen = false;
	ELightBoxSetupNeed DismissedNeed = ELightBoxSetupNeed::None;
	bool bDismissedFailure = false;
	void AttachDialog(TSharedPtr<SWidget> Focus = nullptr);
	TSharedPtr<SWidget> Dialog;
	TSharedPtr<SLightBoxControls> Controls;
	TSharedPtr<class SLightBoxAmbience> Ambience;
};
