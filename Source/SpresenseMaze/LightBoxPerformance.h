#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"

/** Startup calibration of the particle budget for this phone (pure helpers; the pawn runs the measurement). */
namespace LightBoxPerformance
{
    /** Particle counts tried in order; measurement stops at the first that cannot hold 30 fps. */
    inline const TArray<int32>& Levels(){static const TArray<int32> L={32768,49152,65536,98304,131072,196608,262144};return L;}
    /** Below this many grains shapes lose detail, so 45 fps is preferred only when it still allows this. */
    constexpr int32 DensityFloor=65536;
    struct FSample { int32 Count=0; float Fps=0, P95Ms=0; };
    bool Holds(const FSample& S,float TargetFps);
    /** 45 fps when it still allows the density floor, otherwise 30 fps, otherwise the smallest level. */
    int32 Choose(const TArray<FSample>& Samples);
    FSample Measure(TArray<float> FrameMs,int32 Count);
    /** A representative, gently turning furry SDF animal: the kind of scene the AI usually returns. */
    FLightBoxStyle Scene(int32 Count);
}
