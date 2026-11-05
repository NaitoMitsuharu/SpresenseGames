#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"

struct FLightBoxPoint
{
    FVector3f Position=FVector3f::ZeroVector;
    FLinearColor Color=FLinearColor::White;
    float Radius=1;
    FIntVector Vertices=FIntVector(-1,-1,-1);
    FVector2f Weights=FVector2f(1,0);
    FName Owner;
    int32 Sample=0;
    bool bFollow=false; // Unchanged recipes keep following their live animation during other edits.
};

struct FLightBoxPointTransfer
{
    TArray<FLightBoxPoint> From,To;
    TArray<int32> SourceIndices;
    int32 Retained=0,Traveling=0;
};

/** Mathematical sampling surfaces only. Never registered as scene/render components. */
struct FLightBoxPointLayout
{
    TArray<FLightBoxPoint> Points;
    TArray<TArray<FVector>> LocalVertices;
    TArray<int32> Offsets;
    TArray<bool> Moving;
    TArray<FVector4f> InitialVertices;
    int32 FieldOffset=0,FieldCount=0;
    bool bAnimated=false;
};

/** Pure CPU preparation, once per shape; animation is evaluated by the GPU material. */
namespace LightBoxPoints
{
    constexpr int32 Maximum=1000000;
    float Random(uint32 Index,uint32 Channel);
    TArray<FLightBoxPoint> Sample(const FLightBoxStyle& Style);
    FLightBoxPointLayout Build(const FLightBoxStyle& Style,float Time=0);
    FLightBoxPointTransfer Transfer(const TArray<FLightBoxPoint>& Current,TArray<FLightBoxPoint> Next,const FLightBoxStyle& Before,const FLightBoxStyle& After);
    TArray<FVector4f> Frame(const FLightBoxPointLayout& Layout,const FLightBoxStyle& Style,float Time,const FVector2D& Tilt,float Burst,const TArray<FVector4f>& Field);
    FVector3f Destination(const FLightBoxPoint& Point,const TArray<FVector4f>& A,const TArray<FVector4f>& B,float Mix);
    FLightBoxPoint Scatter(uint32 Index,const FLightBoxPoint& Target,const FVector& Center);
    float Ease(uint32 Index,float Phase,float Stagger);
    FVector3f Position(const FLightBoxPoint& A,const FLightBoxPoint& B,uint32 Index,float Phase,float Arc,float Twist,float Stagger,float Burst=0);
}
