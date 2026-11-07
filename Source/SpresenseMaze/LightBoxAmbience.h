#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ALightBoxPawn;
class APlayerController;
namespace LightBoxPaint { struct FCornerAuroraMesh; }

/**
 * Full-screen layer between the scene and the controls that makes each phone sensor visible
 * in the Atelier itself: wind from the microphone edge, a clap's sound wave, light seeping in
 * at the screen edges, embers under a warm finger, an aurora curtain for a hovering hand and
 * magnetic field lines. Never takes input.
 */
class SLightBoxAmbience final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxAmbience) {}
        SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Controller)
    SLATE_END_ARGS()
    void Construct(const FArguments& A);
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 9); }
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;

    /** Assumes a portrait-natural phone with a bottom microphone. ROTATION_90: right; 270: left.
     * +1: the microphone is at the right edge, -1: at the left (from the display rotation). */
    static int32 MicSide(int32 Rotation) { return Rotation == 3 ? -1 : 1; }
private:
    friend class FLightBoxAmbienceLifecycleTest;
    bool HasEffect() const { return Breath > .002f || Dark > .002f || Bright > .002f || Warm > .002f || Near > .002f || Magnet > .002f || !Streaks.IsEmpty() || !Embers.IsEmpty() || !Waves.IsEmpty(); }
    mutable TSharedPtr<LightBoxPaint::FCornerAuroraMesh> CornerAurora;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<ALightBoxPawn> Pawn;
    FVector2D Size = FVector2D(1600, 900);
    double BackdropBorn=FPlatformTime::Seconds(), BackdropSeconds=0, NextBackdropAt=0;
    float Time = 0, RotationAge = 10;
    int32 Side = 1;
    // Smoothed sensor levels.
    float Breath = 0, Dark = 0, Bright = 0, Warm = 0, Near = 0, Magnet = 0;
    FVector2D MagnetDir = FVector2D(1, 0), WarmAt = FVector2D(.6f, .55f);
    struct FStreak { float Y, X, Speed, Length, Amp, Phase, Width; };
    struct FEmber { FVector2D At, Vel; float Age, Life, Size; };
    TArray<FStreak> Streaks;
    TArray<FEmber> Embers;
    TArray<double> Waves;  // clap start times on the pawn's monotonic clock
    TArray<float> WaveStrengths;  // matching clap loudness, .2 .. 1
    FRandomStream Random = FRandomStream(1729);
    float StreakDebt = 0, EmberDebt = 0;
};
