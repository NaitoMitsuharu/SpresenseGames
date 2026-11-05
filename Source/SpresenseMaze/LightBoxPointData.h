#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"

struct FLightBoxPoint
{
    FVector3f Position=FVector3f::ZeroVector;
    FLinearColor Color=FLinearColor::White;
    float Radius=1;
};

/** Pure CPU preparation, once per shape; animation is evaluated by the GPU material. */
namespace LightBoxPoints
{
    constexpr int32 Maximum=1000000;
    float Random(uint32 Index,uint32 Channel);
    TArray<FLightBoxPoint> Sample(const FLightBoxStyle& Style);
    FLightBoxPoint Scatter(uint32 Index,const FLightBoxPoint& Target,const FVector& Center);
    float Ease(uint32 Index,float Phase,float Stagger);
    FVector3f Position(const FLightBoxPoint& A,const FLightBoxPoint& B,uint32 Index,float Phase,float Arc,float Twist,float Stagger,float Burst=0);
}
