#pragma once
#include "LightBoxStyle.h"

/** Deterministic animation shared by rendering, framing and tests; no network or scene state. */
class FLightBoxMotionSystem
{
public:
	static FVector Curve(const TArray<FVector>& Points, double U, bool Closed=false);
	static float Wave(const FLightBoxMotion& M, float Time);
	static TArray<FTransform> Evaluate(const FLightBoxStyle& Scene,float Time,const FVector2D& Tilt=FVector2D::ZeroVector,float Burst=0);
	static FBox Bounds(const FLightBoxStyle& Scene);
	static FTransform Camera(const FLightBoxStyle& Scene,float AspectRatio,FVector* Focus=nullptr);
	static FVector Anchor(const FLightBoxObject& Object,const FString& Name);
	static TArray<FLightBoxObject> ResolveBindings(const FLightBoxStyle& Scene,const TArray<FTransform>& World);
};
