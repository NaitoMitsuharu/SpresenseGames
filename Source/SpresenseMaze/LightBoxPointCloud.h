#pragma once
#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LightBoxPointData.h"
#include "LightBoxPointCloud.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;
class FLightBoxSimulation;

/** Static quad instances, GPU vertex-texture animation; no per-frame particle uploads. */
UCLASS()
class SPRESENSEMAZE_API ULightBoxPointCloud : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()
public:
    ULightBoxPointCloud();
    void Assemble(const FLightBoxStyle& Style);
    void Advance(float Seconds,const FVector2D& Tilt,const FVector& Right,const FVector& Up,const FLightBoxSimulation* Field=nullptr);
    void ScatterPulse() { if(Settings.Shake==TEXT("scatter"))Burst=1;else if(Settings.Shake==TEXT("gather"))Burst=0; }
    void HideCloud();
    void ResizePoints(int32 Count);
    bool IsPreparing() const { return bPreparing; }
    float Progress() const { return FMath::Clamp(Age/FMath::Max(1.f,Settings.AssembleSeconds),0.f,1.f); }
    int32 PointTotal() const { return GetInstanceCount(); }
    int32 RetainedPoints() const { return Retained; }
    bool IsUpdatingFrame() const { return bFramePending; }
#if !UE_BUILD_SHIPPING
    void SetPlaybackTime(float Time) { if(!FMath::IsNearlyEqual(Age,Time)){Age=AnimationAge=Time;FrameClock=1;}bFixedReviewTime=true; }
#endif
    virtual void OnComponentDestroyed(bool Hierarchy) override;
private:
    void Commit(TSharedPtr<FLightBoxPointLayout,ESPMode::ThreadSafe> Layout,TSharedPtr<FLightBoxPointTransfer,ESPMode::ThreadSafe> Transfer,const FLightBoxStyle& Style,uint32 Request,int32 ActiveCount);
    UTexture2D* Texture(const TArray<FLightBoxPoint>& Points,int32 Width,bool Color);
    UTexture2D* BindingsTexture(bool Weights,int32 Width);
    UTexture2D* VertexTexture(const TArray<FVector4f>& Vertices,int32 Width);
    void UploadVertices(UTexture2D* Texture,const TArray<FVector4f>& Vertices);
    void RefreshFrame(const FVector2D& Tilt,const FLightBoxSimulation* Field);
    FVector3f CurrentDestination(int32 Index) const;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PointMaterial;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> DataTextures;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> MotionTextures;
    TSharedPtr<FLightBoxPointLayout,ESPMode::ThreadSafe> Layout;
    TArray<FVector4f> FrameFrom,FrameTo;
    TArray<FLightBoxPoint> Sources,Goals;
    FLightBoxStyle Settings;
    FVector CenterFrom=FVector(0,0,160),CenterTo=FVector(0,0,160);
    float Age=100,OrientationAge=0,Burst=0;
    float AnimationAge=0;
    int32 Retained=0;
    FVector2D LastTilt=FVector2D::ZeroVector;
    uint32 Serial=0;
    bool bPreparing=false;
    bool bTrimmed=true;
    bool bFramePending=false;
    float FrameClock=0,FrameAge=0;
    TArray<float> ReviewFrames;
    float ReviewElapsed=0;
    bool bReviewWritten=false;
    double LastReviewTick=0;
    bool bFixedReviewTime=false;
};
