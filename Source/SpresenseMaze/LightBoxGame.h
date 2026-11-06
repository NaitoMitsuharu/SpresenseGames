#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameHudBase.h"
#include "LightBoxSimulation.h"
#include "LightBoxSensors.h"
#include "LightBoxGame.generated.h"

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
	ALightBoxPawn();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	void Pulse();
	void ToggleCameraOrbit();
	void ResetCameraView();
	bool IsCameraOrbiting() const { return !FMath::IsNearlyZero(ViewOrbitSpeed); }
	/** What the phone sensors are doing right now (for the HUD). */
	const FLightBoxSensorReading& GetSensorReading() const { return Felt; }
	FLightBoxSimulation Simulation;

	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY() TObjectPtr<ULightBoxPointCloud> PointCloud;
private:
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
	float BaseBloom = 0.7f;
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
	void OpenSettings();
	void OpenExamples();
	void OpenJournal();
	void OpenMenu();
	void CloseDialog();
	bool HasDialog() const;
	bool IsModalOpen() const { return Dialog.IsValid(); }
private:
	void EnsureControls();
	void DrawSensorChips();
	float ClapShown = 0;
	void AttachDialog(TSharedPtr<SWidget> Focus = nullptr);
	TSharedPtr<SWidget> Dialog;
	TSharedPtr<SLightBoxControls> Controls;
};
