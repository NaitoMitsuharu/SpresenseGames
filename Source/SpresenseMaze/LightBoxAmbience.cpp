#include "LightBoxAmbience.h"
#include "LightBoxGame.h"
#include "LightBoxPaint.h"
#include "LightBoxSensors.h"
#include "GameFramework/PlayerController.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

using namespace LightBoxPaint;

namespace
{
FLinearColor WithAlpha(FLinearColor C, float A) { C.A = A; return C; }

/** Light seeping in from every edge: Colour at the screen border fading to nothing on an inner ellipse (no corner seams). */
void EdgeFade(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Size, const FLinearColor& Colour, float Alpha, float Depth)
{
    if (Alpha <= .002f) { return; }
    const FLinearColor A = WithAlpha(Colour, Alpha), Z = WithAlpha(Colour, 0);
    const FVector2D C = Size * .5, Inner(FMath::Max(10.0, C.X - Depth), FMath::Max(10.0, C.Y - Depth));
    TArray<FVector2D> P; TArray<FLinearColor> Col; TArray<SlateIndex> I; const int32 N = 64;
    // Uniform angles alone cut across the rectangle corners, leaving black triangular holes.
    // Include each exact corner ray so the fade reaches the entire screen at every aspect ratio.
    TArray<double, TInlineAllocator<69>> Angles;
    for (int32 K = 0; K <= N; ++K) { Angles.Add(2 * PI * K / N); }
    const double Corner = FMath::Atan2(C.Y, C.X);
    for (double EdgeAngle : { Corner, PI - Corner, PI + Corner, 2 * PI - Corner }) { Angles.Add(EdgeAngle); }
    Angles.Sort();
    for (int32 K = 0; K < Angles.Num(); ++K)
    {
        const double Ang = Angles[K]; const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang));
        // Where this ray leaves the screen rectangle.
        const double T = FMath::Min(D.X != 0 ? C.X / FMath::Abs(D.X) : 1e9, D.Y != 0 ? C.Y / FMath::Abs(D.Y) : 1e9);
        P.Add(C + D * T); P.Add(C + D * Inner); Col.Add(A); Col.Add(Z);
        if (K > 0) { const SlateIndex B = SlateIndex(K * 2); I.Append({ SlateIndex(B - 2), SlateIndex(B - 1), B, SlateIndex(B - 1), SlateIndex(B + 1), B }); }
    }
    Tris(Out, L, G, P, Col, I);
}
}

void SLightBoxAmbience::Construct(const FArguments& A)
{
    Controller = A._Controller;
    SetVisibility(EVisibility::HitTestInvisible);
}

void SLightBoxAmbience::Tick(const FGeometry& G, double T, float Dt)
{
    SLeafWidget::Tick(G, T, Dt);
    const bool bWasActive = HasEffect();
    const bool bResized = Size != G.GetLocalSize();
    Dt = FMath::Min(Dt, .1f);
    Size = G.GetLocalSize(); Time += Dt;
#if PLATFORM_ANDROID
    RotationAge += Dt;
    if (RotationAge > 1.f)
    {
        RotationAge = 0;
        if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
        {
            static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_LightBoxRotation", "()I", false);
            if (Method) { Side = MicSide(FJavaWrapper::CallIntMethod(Env, FJavaWrapper::GameActivityThis, Method)); }
        }
    }
#endif
    ALightBoxPawn* Current = Controller.IsValid() ? Cast<ALightBoxPawn>(Controller->GetPawn()) : nullptr;
    if (Pawn.Get() != Current)
    {
        Pawn = Current;
        Breath = Dark = Bright = Warm = Near = Magnet = 0;
        Streaks.Reset(); Embers.Reset(); Waves.Reset(); StreakDebt = EmberDebt = 0;
    }
    if (ALightBoxPawn* Writable = Pawn.Get()) { Writable->SetMicSide(Side); }
    const ALightBoxPawn* P = Pawn.Get();
    if (P)
    {
        const FLightBoxSensorReading& R = P->GetSensorReading();
        Breath = FMath::FInterpTo(Breath, R.Breath, Dt, 6.f);
        Dark = FMath::FInterpTo(Dark, R.Darkness, Dt, 3.f);
        Bright = FMath::FInterpTo(Bright, R.Brightness, Dt, 3.f);
        Warm = FMath::FInterpTo(Warm, R.Warmth, Dt, 4.f);
        Near = FMath::FInterpTo(Near, R.Near, Dt, 4.f);
        Magnet = FMath::FInterpTo(Magnet, R.Magnet, Dt, 4.f);
        if (!R.MagnetScreen.IsNearlyZero()) { MagnetDir = FMath::Lerp(MagnetDir, R.MagnetScreen, FMath::Min(1.f, Dt * 4)).GetSafeNormal(); }
        WarmAt = P->GetWarmPoint();
        if (!P->IsSensorOn(ELightBoxSensor::Sound)) { Breath = 0; Streaks.Reset(); Waves.Reset(); StreakDebt = 0; }
        if (!P->IsSensorOn(ELightBoxSensor::Warmth)) { Warm = 0; Embers.Reset(); EmberDebt = 0; }
        if (!P->IsSensorOn(ELightBoxSensor::Light)) { Dark = Bright = 0; }
        if (!P->IsSensorOn(ELightBoxSensor::Hand)) { Near = 0; }
        if (!P->IsSensorOn(ELightBoxSensor::Magnet)) { Magnet = 0; }
        if (R.bClap && Waves.Num() < 4) { Waves.Add(P->GetClapStartedAt() >= 0 ? P->GetClapStartedAt() : FPlatformTime::Seconds()); WaveStrengths.Add(FMath::Clamp(P->GetClapStrength(), .2f, 1.f)); }
    }
    else { Breath = Dark = Bright = Warm = Near = Magnet = 0; Streaks.Reset(); Embers.Reset(); Waves.Reset(); StreakDebt = EmberDebt = 0; }
    const float W = float(Size.X), H = float(Size.Y);
    // Wind streaks enter from the microphone edge.
    StreakDebt += Breath * 34 * Dt;
    while (StreakDebt >= 1)
    {
        StreakDebt -= 1;
        Streaks.Add({ float(Random.FRandRange(.14f, .86f)) * H, 0, float(Random.FRandRange(.8f, 1.3f)) * W, float(Random.FRandRange(.12f, .26f)) * W,
            float(Random.FRandRange(6, 24)), float(Random.FRandRange(0, 6.28f)), float(Random.FRandRange(1.4f, 3.2f)) });
    }
    for (FStreak& S : Streaks) { S.X += S.Speed * Dt * (.55f + .6f * Breath); }
    Streaks.RemoveAll([W](const FStreak& S) { return S.X - S.Length > W * .9f; });
    // Embers rise from under a warm finger.
    EmberDebt += Warm * 24 * Dt;
    while (EmberDebt >= 1)
    {
        EmberDebt -= 1;
        const float A = float(Random.FRandRange(0, 6.28f)), Rr = float(Random.FRandRange(0, 46));
        Embers.Add({ WarmAt * Size + FVector2D(FMath::Cos(A), FMath::Sin(A) * .5f) * Rr, FVector2D(float(Random.FRandRange(-18, 18)), -float(Random.FRandRange(55, 130))),
            0, float(Random.FRandRange(1.f, 1.9f)), float(Random.FRandRange(1.6f, 3.6f)) });
    }
    for (FEmber& E : Embers) { E.Age += Dt; E.Vel.X += FMath::Sin(Time * 3 + E.Life * 9) * 40 * Dt; E.At += E.Vel * Dt; }
    Embers.RemoveAll([](const FEmber& E) { return E.Age > E.Life; });
    const double Now = FPlatformTime::Seconds();
    WaveStrengths.SetNum(Waves.Num());
    for (int32 K = Waves.Num() - 1; K >= 0; --K) { if (Now - Waves[K] > 1.5) { Waves.RemoveAt(K); WaveStrengths.RemoveAt(K); } }
    const bool bBackdropDue=Now>=NextBackdropAt;
    if(bBackdropDue) { BackdropSeconds=Now-BackdropBorn; NextBackdropAt=Now+1./15.; }
    if (bWasActive || HasEffect() || bResized || bBackdropDue) { Invalidate(EInvalidateWidgetReason::Paint); }
}

int32 SLightBoxAmbience::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out,Style.GetColorAndOpacityTint().A);
    if(!CornerAurora.IsValid() || CornerAurora->Size!=G.GetLocalSize())
    { CornerAurora=MakeShared<FCornerAuroraMesh>(G.GetLocalSize()); }
    CornerAurora->Update(BackdropSeconds);
    // A soft navy edge over the world clear colour, preserving the sculpture's authored colours.
    EdgeFade(Out,L,G,G.GetLocalSize(),FLinearColor(.010f,.020f,.050f),.32f,float(FMath::Min(G.GetLocalSize().X,G.GetLocalSize().Y))*.62f);
    Tris(Out,L,G,CornerAurora->Positions,CornerAurora->Colours,CornerAurora->Indices);
    // Backdrop stays alive with sensors disabled; positions update at 15 Hz without rebuilding topology.
    if (!HasEffect()) { return L+1; }
    ++L;
    const float W = float(Size.X), H = float(Size.Y), Short = FMath::Min(W, H);
    const float MicX = Side > 0 ? W : 0, Inward = -float(Side);

    // Light: darkness closes in from every edge; bright light seeps in softly.
    EdgeFade(Out, L, G, Size, FLinearColor::Black, .94f * Dark, Short * .42f);
    EdgeFade(Out, L, G, Size, FLinearColor::Black, .6f * Dark, Short * .16f);
    const float Breathe = .86f + .14f * FMath::Sin(Time * 1.6f);
    // Bright light: a pale silver haze at the edges, kept faint so the navy and the aurora still read.
    EdgeFade(Out, L + 1, G, Size, FLinearColor(.82f, .92f, 1.f), .15f * Bright * Breathe, Short * .38f);
    EdgeFade(Out, L + 1, G, Size, FLinearColor(.92f, .97f, 1.f), .07f * Bright * Breathe, Short * .10f);

    // Breath: a cool glow at the microphone edge and streaks of wind across the scene.
    if (Breath > .01f) { Disc(Out, L + 2, G, FVector2D(MicX, H * .5f), H * .5f, FLinearColor(.4f, .85f, 1.f, .16f * Breath), FLinearColor(.4f, .85f, 1.f, 0), 40); }
    for (const FStreak& S : Streaks)
    {
        TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I; const int32 N = 18;
        const float Life = 1 - FMath::SmoothStep(W * .55f, W * .9f, S.X - S.Length * .5f);
        for (int32 K = 0; K <= N; ++K)
        {
            const float Along = S.X - S.Length * K / N; if (Along < 0) { break; }
            const float X = MicX + Inward * Along, Y = S.Y + FMath::Sin(Along * .006f + S.Phase + Time * 1.4f) * S.Amp;
            const float A = (1 - float(K) / N) * Life * FMath::Min(1.f, Breath * 1.4f) * .75f, Half = S.Width * (1 - .6f * K / N);
            const FLinearColor Col = WithAlpha(FMath::Lerp(FLinearColor(.75f, .97f, 1.f), FLinearColor(.4f, .8f, 1.f), float(K) / N), A);
            P.Add(FVector2D(X, Y - Half)); P.Add(FVector2D(X, Y + Half)); C.Add(Col); C.Add(Col);
            if (P.Num() >= 4) { const SlateIndex B = SlateIndex(P.Num() - 2); I.Append({ SlateIndex(B - 2), SlateIndex(B - 1), B, SlateIndex(B - 1), SlateIndex(B + 1), B }); }
        }
        if (I.Num()) { Tris(Out, L + 3, G, P, C, I); }
    }

    // Clap: rings of sound spreading from the microphone; they reach the scene centre as the burst lands.
    const float Speed = W * .5f / FLightBoxSensorReader::ClapTravelSeconds;
    const double Now = FPlatformTime::Seconds();
    for (int32 Wv = 0; Wv < Waves.Num(); ++Wv)
    {
        // Louder claps draw brighter, thicker rings.
        const float Age = float(Now - Waves[Wv]), Loud = WaveStrengths.IsValidIndex(Wv) ? WaveStrengths[Wv] : .6f;
        const float Fade = FMath::Pow(FMath::Max(0.f, 1 - Age / 1.5f), 1.5f) * (.35f + .65f * Loud);
        for (int32 K = 0; K < 3; ++K)
        {
            const float R = Age * Speed - K * 34; if (R <= 0) { continue; }
            const float A = Fade * (K == 0 ? .7f : K == 1 ? .42f : .24f);
            Ring(Out, L + 4, G, FVector2D(MicX, H * .5f), R, (3.4f - K) * (1 + Age) * (.6f + .7f * Loud), Side > 0 ? 90.f : -90.f, 180, [&](float S) { return WithAlpha(FMath::Lerp(FLinearColor(1.f, .92f, .62f), Aurora((S * .5f + (Side > 0 ? .25f : .75f)) * 2 + Age), .45f), A); }, 64);
        }
    }

    // Warmth: a glow under the finger and embers drifting up.
    if (Warm > .01f)
    {
        const FVector2D C = WarmAt * Size;
        Disc(Out, L + 5, G, C, 150 * (.8f + .2f * Warm), FLinearColor(1.f, .5f, .18f, .3f * Warm), FLinearColor(1.f, .4f, .1f, 0), 40);
        Disc(Out, L + 5, G, C, 52, FLinearColor(1.f, .9f, .7f, .26f * Warm), FLinearColor(1.f, .7f, .4f, 0), 24);
    }
    for (const FEmber& E : Embers)
    {
        const float T = E.Age / E.Life, A = FMath::Sin(PI * T) * .85f;
        const FLinearColor Col = FMath::Lerp(FLinearColor(1.f, .85f, .5f), FLinearColor(1.f, .4f, .12f), T);
        Disc(Out, L + 6, G, E.At, E.Size * 2.6f, WithAlpha(Col, A * .35f), WithAlpha(Col, 0), 12);
        Disc(Out, L + 6, G, E.At, E.Size, WithAlpha(FLinearColor(1.f, .95f, .8f), A), WithAlpha(Col, 0), 10);
    }

    // Hand: an aurora curtain hanging from the earpiece edge, where the proximity sensor sits.
    if (Near > .01f)
    {
        const float Edge = Side > 0 ? 0 : W, In = float(Side);
        for (int32 K = 0; K < 6; ++K)
        {
            TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I; const int32 N = 24;
            const float Depth = (.03f + .05f * K) * W, Half = (.02f + .012f * FMath::Sin(K * 1.9f + 1)) * W;
            const FLinearColor Hue = Aurora(K * .15f + Time * .07f);
            for (int32 S = 0; S <= N; ++S)
            {
                const float Y = H * S / N, X = Edge + In * (Depth + FMath::Sin(Y * .007f + Time * .9f + K) * W * .018f);
                const float A = FMath::Pow(Near, 1.3f) * .62f * (1 - K / 7.f) * (.45f + .55f * FMath::Sin(Y * .011f + Time * 1.2f + K * 1.7f)) * FMath::Sin(PI * S / N);
                P.Append({ FVector2D(X - Half, Y), FVector2D(X, Y), FVector2D(X + Half, Y) });
                C.Append({ WithAlpha(Hue, 0), WithAlpha(Hue, FMath::Max(0.f, A)), WithAlpha(Hue, 0) });
                if (S > 0)
                {
                    const SlateIndex B = SlateIndex(S * 3), Pv = SlateIndex(B - 3);
                    I.Append({ Pv, SlateIndex(Pv + 1), B, SlateIndex(Pv + 1), SlateIndex(B + 1), B, SlateIndex(Pv + 1), SlateIndex(Pv + 2), SlateIndex(B + 1), SlateIndex(Pv + 2), SlateIndex(B + 2), SlateIndex(B + 1) });
                }
            }
            Tris(Out, L + 7, G, P, C, I);
        }
    }

    // Magnet: dotted field lines bending toward the magnet, light flowing along them.
    if (Magnet > .01f)
    {
        const FVector2D C = Size * .5, D = FVector2D(MagnetDir.X, -MagnetDir.Y).GetSafeNormal(), Perp(-D.Y, D.X);
        const FVector2D Target = C + D * FMath::Max(W, H) * .48f;
        Disc(Out, L + 8, G, Target, H * .38f, FLinearColor(1.f, .42f, .66f, .24f * Magnet), FLinearColor(1.f, .42f, .66f, 0), 40);
        for (int32 K = -3; K <= 3; ++K)
        {
            const FVector2D Start = C - D * H * .45f + Perp * (K * H * .11f), Ctrl = (Start + Target) * .5 + Perp * (K * H * .17f);
            const int32 N = 28;
            const float LineA = Magnet * .2f * (1 - FMath::Abs(K) / 5.f);
            TArray<FVector2D> Curve; for (int32 S = 0; S <= 40; ++S) { const float T = S / 40.f, U = 1 - T; Curve.Add(Start * (U * U) + Ctrl * (2 * U * T) + Target * (T * T)); }
            Line(Out, L + 8, G, Curve, FLinearColor(1.f, .5f, .72f, LineA), 1.5f);
            for (int32 S = 0; S < N; ++S)
            {
                const float T = FMath::Frac(float(S) / N + Time * .22f), U = 1 - T;
                const FVector2D Pt = Start * (U * U) + Ctrl * (2 * U * T) + Target * (T * T);
                const float A = Magnet * .7f * FMath::Sin(PI * T) * (1 - FMath::Abs(K) / 5.f);
                const FLinearColor Dot = FMath::Lerp(FLinearColor(1.f, .55f, .75f), Paper, T * .5f);
                Disc(Out, L + 9, G, Pt, 9.f, WithAlpha(Dot, A * .28f), WithAlpha(Dot, 0), 12);
                Disc(Out, L + 9, G, Pt, 2.6f + 1.4f * T, WithAlpha(Dot, A), WithAlpha(Dot, A * .5f), 10);
            }
        }
    }
    return L + 10;
}
