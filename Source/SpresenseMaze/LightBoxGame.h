#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameHudBase.h"
#include "LightBoxSimulation.h"
#include "LightBoxGame.generated.h"

class UCameraComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UProceduralMeshComponent;
class UPostProcessComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
class USkyLightComponent;
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
	FLightBoxSimulation Simulation;

	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Motes;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WarmMotes;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoolMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WarmMaterial;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ParticleMeshes;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> TailMeshes;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ParticleMaterials;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> SceneObjects;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ObjectMaterials;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FloorMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BackgroundMaterial;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Backdrop;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rim;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Grid;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
	UPROPERTY() TObjectPtr<USkyLightComponent> StudioSky;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> AccentLights;
	UPROPERTY() TObjectPtr<ULightBoxPointCloud> PointCloud;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StageFloor;
private:
	friend class FLightBoxBatchReview;
	TSharedPtr<FLightBoxBatchReview> BatchReview;
	void BuildChamber();
	void ApplyScene(const FLightBoxStyle& Style);
	void AnimateScene(float Dt, const FVector2D& Tilt, const FLightBoxStyle& Style);
	void RenderParticles(const FLightBoxStyle& Style);
	void UpdateCameraInput(APlayerController* PC,bool Blocked);
	void UpdateCameraView(const FLightBoxStyle& Style,float Dt);
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
	float GeometryClock=0, LastAspect=0;
	FTransform CameraStart,CameraTarget;
	float CameraBlendAge=100;
	float CameraBlendSeconds=1;
	FBox CameraBounds=FBox(ForceInit);
	FVector2D ViewAngles=FVector2D::ZeroVector,LastViewTouch=FVector2D::ZeroVector;
	float ViewDistance=1,ViewOrbitSpeed=0,LastPinchDistance=0;
	bool bViewDragging=false;
	FString AppliedContent;
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
	void AttachDialog(TSharedPtr<SWidget> Focus = nullptr);
	void Glow(float X, float Y, float Radius, const FLinearColor& Color);
	void EnsureGlow();
	UPROPERTY() TObjectPtr<UTexture2D> GlowTexture;
	TSharedPtr<SWidget> Dialog;
	TSharedPtr<SLightBoxControls> Controls;
};
