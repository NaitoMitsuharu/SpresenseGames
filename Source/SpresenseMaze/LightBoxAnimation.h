#pragma once
#include "LightBoxStyle.h"

/** Stable surface bindings: animation moves the original skin, never samples a new particle model. */
struct FLightBoxSkinBinding
{
    int32 Part[4]={INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE};
    float Weight[4]={0,0,0,0};
};

namespace LightBoxAnimation
{
    struct FInterval { int32 From=0,To=0; float Mix=0; };
    TArray<FLightBoxStyle> Prepare(const FLightBoxStyle& Scene);
    FInterval Interval(const FLightBoxStyle& Scene,float Time);
    FLightBoxStyle Sample(const TArray<FLightBoxStyle>& Poses,const FInterval& Frame);
    FLightBoxSkinBinding Bind(const FLightBoxObject& Object,const FVector& Point);
    struct FSkinTransform
    {
        FVector A,Next,Axis,X,Y,Z,Offset;
        FQuat Rotation=FQuat::Identity;
        FVector2D RestRadius,NextRadius;
        double InvLength=0;
        bool bCapsule=false;
    };
    TArray<FSkinTransform> Palette(const FLightBoxObject& Rest,const FLightBoxObject& Pose);
    FVector Skin(const FVector& Point,const FLightBoxSkinBinding& Binding,const TArray<FSkinTransform>& Palette);
    FVector Skin(const FLightBoxObject& Rest,const FLightBoxObject& Pose,const FVector& Point,const FLightBoxSkinBinding& Binding);
    TArray<FVector> Remap(const TArray<FVector>& Rest,const TArray<FVector>& Target);
}
