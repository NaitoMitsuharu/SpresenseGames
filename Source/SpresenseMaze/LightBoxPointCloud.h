#pragma once
#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LightBoxPointData.h"
#include "LightBoxPointCloud.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;
class FLightBoxSimulation;

/** One billboard batch; GPU interpolation, with shared destination caching for complex skins. */
UCLASS()
class SPRESENSEMAZE_API ULightBoxPointCloud : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()
public:
    ULightBoxPointCloud();
    void Assemble(const FLightBoxStyle& Style);
    void Advance(float Seconds,const FVector2D& Tilt,const FVector& Right,const FVector& Up,const FLightBoxSimulation* Field=nullptr);
    /** While the AI is composing the next scene the grains loosen and breathe, so the scene itself shows work in progress. */
    void SetThinking(bool bOn) { bThinking = bOn; }
    float Breath() const;
    /** A clap or shake front crossing the sculpture (WorldDir zero: spreading from its centre). Strength 0..1; Lead delays the front. */
    void AddWave(const FVector& WorldDir,float Strength,float Lead=0);
    /** Breath (wind away from the mic), magnet pull and a warm touch, in world space; strengths 0..1. */
    void SetSensorForces(const FVector& WindDir,float Wind,const FVector& PullDir,float Pull,const FVector& HeatAt,float Heat)
    { WindWorld=WindDir;WindAmount=Wind;PullWorld=PullDir;PullAmount=Pull;HeatWorld=HeatAt;HeatAmount=Heat; }
    FVector WorldCentre() const;
    /** Half the sculpture's largest extent; sensor forces scale with it. */
    float Reach() const { return FMath::Max(20.f,FMath::Lerp(ReachFrom,ReachTo,Progress())); }
    /** A wave crosses the whole sculpture in .4 s. */
    float WaveSpeed() const { return 2*Reach()/.4f; }
    void HideCloud();
    void ResizePoints(int32 Count);
    bool IsPreparing() const { return bPreparing; }
    float Progress() const { return FMath::Clamp(Age/FMath::Max(1.f,Settings.AssembleSeconds),0.f,1.f); }
    int32 PointTotal() const { return RenderedPoints; }
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
    UTexture2D* MakeOrderTexture(int32 Count,int32 Width);
    void UploadOrder(const TArray<float>& Order);
    UTexture2D* ColourTexture(const TArray<FLinearColor>& Colours,int32 Width);
    UTexture2D* VertexTexture(const TArray<FVector4f>& Vertices,int32 Width);
    void UploadVertices(UTexture2D* Texture,const TArray<FVector4f>& From,const TArray<FVector4f>& To);
    void RefreshFrame(const FVector2D& Tilt,const FLightBoxSimulation* Field);
    FVector3f CurrentDestination(int32 Index) const;
    void RefreshDrawOrder(const FVector& Right,const FVector& Up);
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PointMaterial;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> DataTextures;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> MotionTextures;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> StoryColourTextures;
    UPROPERTY() TObjectPtr<UTexture2D> OrderTexture;
    int32 ColourFrom=INDEX_NONE,ColourTo=INDEX_NONE;
    TSharedPtr<FLightBoxPointLayout,ESPMode::ThreadSafe> Layout;
    TArray<FVector4f> FrameFrom,FrameTo;
    TArray<FVector4f> CacheFrom,CacheTo;
    TSharedPtr<const TArray<FLightBoxPointBinding>,ESPMode::ThreadSafe> CacheBindings;
    int32 RenderedPoints=0;
    bool bCacheDestinations=false;
    bool bSortPending=false,bSorted=false;
    float SortClock=0;
    FVector3f SortedForward=FVector3f::ZeroVector;
    TArray<FLightBoxPoint> Sources,Goals;
    FLightBoxStyle Settings;
    FVector CenterFrom=FVector(0,0,160),CenterTo=FVector(0,0,160);
    float Age=100,OrientationAge=0,Burst=0,Think=0;
    float ReachFrom=100,ReachTo=100,SensorClock=0;
    FVector WaveDir[2]={FVector::ZeroVector,FVector::ZeroVector};float WaveAge[2]={-100,-100},WaveStrength[2]={0,0};
    FVector WindWorld=FVector::ZeroVector,PullWorld=FVector::ZeroVector,HeatWorld=FVector::ZeroVector;float WindAmount=0,PullAmount=0,HeatAmount=0;
    bool bThinking=false;
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
