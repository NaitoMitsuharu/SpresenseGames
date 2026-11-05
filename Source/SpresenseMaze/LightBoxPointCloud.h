#pragma once
#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LightBoxPointData.h"
#include "LightBoxPointCloud.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

/** Static quad instances, GPU vertex-texture animation; no per-frame particle uploads. */
UCLASS()
class SPRESENSEMAZE_API ULightBoxPointCloud : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()
public:
    ULightBoxPointCloud();
    void Assemble(const FLightBoxStyle& Style);
    void Advance(float Seconds,const FVector2D& Tilt,const FVector& Right,const FVector& Up);
    void ScatterPulse() { if(Settings.Shake==TEXT("scatter"))Burst=1;else if(Settings.Shake==TEXT("gather"))Burst=0; }
    void HideCloud();
    void ResizePoints(int32 Count);
    bool IsPreparing() const { return bPreparing; }
    float Progress() const { return FMath::Clamp(Age/FMath::Max(1.f,Settings.AssembleSeconds),0.f,1.f); }
    int32 PointTotal() const { return Goals.Num(); }
    virtual void OnComponentDestroyed(bool Hierarchy) override;
private:
    void Commit(TArray<FLightBoxPoint>&& Points,const FLightBoxStyle& Style,uint32 Request);
    UTexture2D* Texture(const TArray<FLightBoxPoint>& Points,int32 Width,bool Color);
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PointMaterial;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> DataTextures;
    TArray<FLightBoxPoint> Sources,Goals;
    FLightBoxStyle Settings;
    FVector CenterFrom=FVector(0,0,160),CenterTo=FVector(0,0,160);
    float Age=100,OrientationAge=0,Burst=0;
    uint32 Serial=0;
    bool bPreparing=false;
    bool bTrimmed=true;
    TArray<float> ReviewFrames;
    float ReviewElapsed=0;
    bool bReviewWritten=false;
    double LastReviewTick=0;
};
