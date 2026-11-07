#include "LightBoxInput.h"
#include "LightBoxKana.h"
#include "LightBoxPaint.h"
#include "LightBoxSensors.h"
#include "Framework/Application/SlateUser.h"

using namespace LightBoxPaint;

namespace
{
float EaseOut(float T) { T = FMath::Clamp(T, 0.f, 1.f); return 1 - FMath::Pow(1 - T, 3.f); }
float BackOut(float T) { T = FMath::Clamp(T, 0.f, 1.f); const float C = 1.9f; return 1 + (C + 1) * FMath::Pow(T - 1, 3.f) + C * FMath::Pow(T - 1, 2.f); }
FLinearColor Alpha(FLinearColor C, float A) { C.A *= A; return C; }

}

// ---------------------------------------------------------------- pressable

FReply SLightBoxPressable::OnMouseButtonDown(const FGeometry&, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
    if (ActivePointer != INDEX_NONE) { return FReply::Handled(); }
    if (!Enabled.Get()) { return FReply::Handled(); }
    ActivePointer = E.GetPointerIndex(); ActiveUser = E.GetUserIndex();
    bPressed = true;
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SLightBoxPressable::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton && E.GetEffectingButton().IsValid()) { return FReply::Unhandled(); }
    if (ActivePointer != int32(E.GetPointerIndex()) || ActiveUser != int32(E.GetUserIndex())) { return FReply::Handled(); }
    const bool bWas = bPressed; bPressed = false; ActivePointer = INDEX_NONE;
    if (bWas && G.IsUnderLocation(E.GetScreenSpacePosition()) && Enabled.Get()) { Clicked.ExecuteIfBound(); }
    return FReply::Handled().ReleaseMouseCapture();
}

void SLightBoxPressable::Tick(const FGeometry& G, double T, float Dt)
{
    SLeafWidget::Tick(G, T, Dt);
    const float Previous=Press;
    Press=FMath::FInterpTo(Press,bPressed?1.f:0.f,Dt,22.f);
    if(FMath::Abs(Press-(bPressed?1.f:0.f))<.001f) { Press=bPressed?1.f:0.f; }
    const bool Animate=NeedsAnimation(), Active=Enabled.Get(); const double Now=FPlatformTime::Seconds();
    if(Press!=Previous || Active!=bLastEnabled || Animate!=bWasAnimating || (Animate&&Now>=NextPaintAt))
    {
        Invalidate(EInvalidateWidgetReason::Paint); NextPaintAt=Now+PaintInterval();
    }
    bWasAnimating=Animate; bLastEnabled=Active;
}

// ---------------------------------------------------------------- mic

void SLightBoxMicButton::Tick(const FGeometry& G, double T, float Dt)
{
    Listen = FMath::FInterpTo(Listen, Listening.Get() ? 1.f : 0.f, Dt, 9.f);
    if (Listen < .001f) { Listen = 0; }
    const float Raw = Listening.Get() && FMath::IsFinite(VoiceLevel.Get()) ? FMath::Clamp(VoiceLevel.Get(), 0.f, 1.f) : 0.f;
    Envelope = FMath::FInterpTo(Envelope, Raw, Dt, Raw > Envelope ? 22.f : 9.f);
    const double Now = FPlatformTime::Seconds();
    if (Listening.Get() && Now >= NextSampleAt)
    {
        for (int32 I = 0; I < 6; ++I) { Samples[I] = Samples[I + 1]; }
        Samples[6] = Envelope; NextSampleAt = Now + 1. / 30.;
    }
    if (!Listening.Get()) { Envelope = 0; for (float& Sample : Samples) { Sample = 0; } }
    if (Enabled.Get()) { RingPhase = FMath::Frac(RingPhase + Dt * FMath::Lerp(.025f, .08f, Listen)); }
    SLightBoxPressable::Tick(G, T, Dt);
}

int32 SLightBoxMicButton::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if (Style.GetColorAndOpacityTint().A <= .001f) { return L; }
    FVectorPaintBatch Batch(Out, Style.GetColorAndOpacityTint().A);
    const float U = float(G.GetLocalSize().X) / 160.f, Shrink = 1 - .035f * Press;
    const FVector2D C(G.GetLocalSize().X * .5, G.GetLocalSize().Y * .5 - 16 * U);
    const float On = Enabled.Get() ? 1.f : .42f, R = 60 * U * Shrink;
    const FLinearColor VoiceCyan(.22f, 1.f, .90f);
    // One batched feathered rim. Only vertex colours move: 15 Hz idle, 30 Hz listening, no blur pass.
    if (Listen > .001f) { Disc(Out, L, G, C, R + 16 * U, Alpha(VoiceCyan, (.07f + .07f * Envelope) * Listen * On), FLinearColor::Transparent, 32); }
    Disc(Out, L, G, C, R, FLinearColor(.030f, .043f, .080f, .86f), FLinearColor(.010f, .016f, .038f, .91f), 40);
    GlowRing(Out, L, G, C, R, (1.5f + 1.1f * Listen) * U, (3 + 4 * Listen) * U, -90, 360,
        [&](float S) { return Alpha(FMath::Lerp(Aurora(S - RingPhase), VoiceCyan, Listen * .65f), (.40f + .50f * Listen) * On); }, 48);
    const FLinearColor Ink = Alpha(Paper, .94f * On); const float M = U * Shrink;
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * M; };
    // Rounded outline glyph matches the paper plane and other light line icons.
    ArcStroke(Out, L, G, P(0, -16), 8 * M, 180, 180, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(-8, -16), P(-8, -1)}, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(8, -16), P(8, -1)}, Ink, 2.5f * M);
    ArcStroke(Out, L, G, P(0, -1), 8 * M, 0, 180, Ink, 2.5f * M);
    ArcStroke(Out, L, G, P(0, -1), 15 * M, 0, 180, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(-15, -7), P(-15, -1)}, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(15, -7), P(15, -1)}, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(0, 14), P(0, 23)}, Ink, 2.5f * M);
    Stroke(Out, L, G, {P(-8, 23), P(8, 23)}, Ink, 2.5f * M);
    if (Listen > .001f)
    {
        // Each bar is a real recent sample. Silence stays flat, without a synthetic sine wave.
        for (int32 I = 0; I < 7; ++I)
        {
            const float H = (1.2f + 8.f * Samples[I]) * U;
            Fill(Out, L, G, C + FVector2D((I - 3) * 5.f * U - U, 38 * U - H), FVector2D(2 * U, 2 * H), U, Alpha(VoiceCyan, .85f * Listen * On));
        }
    }
    Batch.Flush();
    Text(Out, L + 1, G, C + FVector2D(0, 83 * U), Listening.Get() ? TEXT("聞いています") : TEXT("声でつくる"), Spaced(17, 30), Alpha(Listening.Get() ? VoiceCyan : Paper, (Listening.Get() ? .94f : .73f) * On));
    return L + 2;
}

// ---------------------------------------------------------------- send

int32 SLightBoxSendButton::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out, Style.GetColorAndOpacityTint().A);
    const FVector2D C = G.GetLocalSize() * .5; const float U = float(G.GetLocalSize().X) / 76.f, T = Time(), Age = T - LaunchedAt;
    FVector2D ToCentre(-240, 470);
    if (Age >= 0 && Age < 1.1f) if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(ConstCastSharedRef<SWidget>(AsShared())))
    {
        const FVector2D Screen = Window->GetPositionInScreen() + Window->GetSizeInScreen() * .5;
        ToCentre = G.AbsoluteToLocal(Screen) - C;
    }
    const bool bReady = Ready.Get(), bBusy = Busy.Get();
    const float R = 31 * U * (1 - .08f * Press);
    // Waiting for the AI: the plane has flown; a single star circles the button, like the one on the hairlines,
    // over a thin stop mark (tap to cancel). Shown only after the launch has played.
    if (bBusy && (Age < 0 || Age >= .85f))
    {
        const float Fade = Age < 0 ? 1.f : FMath::Clamp((Age - .85f) / .3f, 0.f, 1.f);
        Disc(Out, L, G, C, R, FLinearColor(.018f, .030f, .060f, .78f * Fade), FLinearColor(.008f, .014f, .034f, .82f * Fade), 40);
        ArcStroke(Out, L, G, C, R, 0, 360, Alpha(Paper, .14f * Fade), 1.f);
        const float Ang = T * 120.f - 90.f;
        Ring(Out, L + 1, G, C, R, 1.6f * U, Ang - 80, 80, [&](float S) { return Alpha(Hairline(S), S * .85f * Fade); }, 24);
        const FVector2D Star = C + FVector2D(FMath::Cos(FMath::DegreesToRadians(Ang)), FMath::Sin(FMath::DegreesToRadians(Ang))) * R;
        Disc(Out, L + 2, G, Star, 9 * U, Alpha(Mint, .30f * Fade), FLinearColor::Transparent, 16);
        Disc(Out, L + 2, G, Star, 2.6f * U, Alpha(Paper, Fade), Alpha(Mint, .6f * Fade), 10);
        const FVector2D H(7.5f * U);
        Stroke(Out, L + 2, G, { C - H, C + FVector2D(H.X, -H.Y), C + H, C + FVector2D(-H.X, H.Y), C - H }, Alpha(Paper, .62f * Fade), 1.4f * U);
        return L + 3;
    }
    // Glow that breathes while there is something to send.
    if (bReady) { Disc(Out, L, G, C, R * 1.45f, Alpha(Mint, .12f + .035f * FMath::Sin(T * 3)), FLinearColor::Transparent); }
    Disc(Out,L+1,G,C,R,FLinearColor(.018f,.030f,.060f,.78f),FLinearColor(.008f,.014f,.034f,.82f),40);
    ArcStroke(Out,L+1,G,C,R,0,360,Alpha(Paper,bReady?.30f:.16f),1.f);
    Disc(Out,L+2,G,C,R*.62f,Alpha(Mint,bReady?.20f:.10f),FLinearColor::Transparent,24);
    // The resting plane hides while one is in flight, then pops back.
    const float Back = bBusy ? 0.f : Age < .55f ? 0.f : BackOut((Age - .55f) / .35f);
    if (Back > 0)
    {
        Plane(Out, L + 3, G, C + FVector2D(-1, 1) * U, 30 * U * Back, -24, FLinearColor(.70f, 1.f, .95f), bReady ? FLinearColor(.25f, 1.f, .86f) : FLinearColor(.25f, 1.f, .86f, .78f));
    }
    if (Age >= 0 && Age < 1.1f)
    {
        // Ripple from the button.
        if (Age < .6f)
        {
            const float P = Age / .6f;
            Ring(Out, L + 4, G, C, R + 46 * U * EaseOut(P), 4 * U * (1 - P), 0, 360, [&](float S) { return Alpha(Aurora(S), 1 - P); }, 48);
        }
        // The plane climbs away to the upper right, leaving an aurora trail.
        // From the button down into the scene centre, where the words become particles.
        const FVector2D Goal = ToCentre / FMath::Max(.01f, U);
        auto Path = [&](float E) { const float S = E * E * (3 - 2 * E); return C + (FVector2D(Goal.X * S, Goal.Y * S) + FVector2D(0, -120 * FMath::Sin(PI * E))) * U; };
        const float E = FMath::Clamp(Age / 1.f, 0.f, 1.f), Fade = 1 - FMath::SmoothStep(.6f, 1.f, E);
        for (int32 K = 12; K >= 1; --K)
        {
            const float Ek = E - K * .03f; if (Ek <= 0) { continue; }
            Disc(Out, L + 5, G, Path(Ek), 3.2f * U * (1 - K / 14.f), Alpha(Hairline(K / 12.f), Fade * (1 - K / 13.f)), Alpha(Hairline(K / 12.f), 0), 12);
        }
        const FVector2D Dir = Path(E + .01f) - Path(E);
        Plane(Out, L + 6, G, Path(E), 30 * U * (1 - .35f * E), FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), Alpha(Paper, Fade), Alpha(FLinearColor(.5f, .42f, .95f), Fade));
        for (int32 K = 0; K < 8 && Age < .45f; ++K)
        {
            const float A = 2 * PI * K / 8 + .3f, P = Age / .45f;
            Disc(Out, L + 5, G, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (R + 30 * U * EaseOut(P)), 3 * U * (1 - P), Alpha(Aurora(K / 8.f), 1 - P), Alpha(Aurora(K / 8.f), 0), 10);
        }
    }
    return L + 7;
}

// ---------------------------------------------------------------- prompt

namespace
{
struct FRun { FString Text; int32 Kind; };  // 0 committed, 1 reading, 2 segment, 3 focused segment
TArray<FRun> Runs(const FLightBoxComposer& C)
{
    TArray<FRun> Out;
    if (!C.Committed.IsEmpty()) { Out.Add({ C.Committed, 0 }); }
    if (C.bConverting) { for (int32 I = 0; I < C.Segments.Num(); ++I) { Out.Add({ C.Segments[I].Chosen(), I == C.Focus ? 3 : 2 }); } }
    else if (C.IsComposing()) { Out.Add({ C.Reading + C.Romaji.Pending, 1 }); }
    return Out;
}
const TCHAR* Placeholder = TEXT("つくりたいもの");
constexpr float LineReach = 56, LineY = 74;
/** The title underline reaches past the words on both sides and carries a star at its centre. */
void Underline(FSlateWindowElementList& Out, int32 L, const FGeometry& G, float Centre, float TextWidth, float Opacity)
{
    const float Length = FMath::Min(float(G.GetLocalSize().X) - 4, TextWidth + 2 * LineReach);
    GlowLine(Out, L, G, FVector2D(Centre - Length * .5f, LineY), Length, Opacity, .5f, 1.f, 1.6f);
}
}

bool SLightBoxPrompt::NeedsAnimation() const { return Open.Get() || (Composer.IsValid() && Runs(*Composer).IsEmpty()); }

void SLightBoxPrompt::Tick(const FGeometry& G, double T, float Dt)
{
    SLightBoxPressable::Tick(G, T, Dt);
    if (Composer.IsValid() && SeenRevision != Composer->Revision)
    {
        SeenRevision = Composer->Revision;
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

FVector2D SLightBoxPrompt::ComputeDesiredSize(float) const
{
    const FSlateFontInfo F = Spaced(30, 190);
    float Width = 0; for (const FRun& R : Runs(*Composer)) { Width += Measure(R.Text, F).X; }
    if (Width == 0) { Width = Measure(Placeholder, F).X; }
    return FVector2D(FMath::Clamp(Width + 2 * LineReach, 300.f, FMath::Max(300.f, MaxWidth.Get())), 84);
}

int32 SLightBoxPrompt::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out, Style.GetColorAndOpacityTint().A);
    const FVector2D Size = G.GetLocalSize(); const float T = Time(); const bool bOpen = Open.Get();
    const FSlateFontInfo F = Spaced(30, 190);
    const TArray<FRun> Parts = Runs(*Composer);
    float Total = 0; TArray<float> Widths; for (const FRun& R : Parts) { Widths.Add(Measure(R.Text, F).X); Total += Widths.Last(); }
    const float Base = 36, LineH = Measure(TEXT("景"), F).Y;
    // Keep the tail in view once the words outgrow the space.
    float X = Total + LineReach > Size.X ? float(Size.X) - Total - LineReach * .5f : float(Size.X - Total) * .5f;
    const float Y = Base - LineH * .5f;
    Batch.Flush();
    Out.PushClip(FSlateClippingZone(G));
    if (Parts.IsEmpty())
    {
        const FVector2D P = Measure(Placeholder, F); const float PX = float(Size.X - P.X) * .5f;
        TextAt(Out, L + 2, G, FVector2D(PX, Y), Placeholder, F, Alpha(Paper, bOpen ? .40f : .46f));
        Underline(Out, L + 1, G, float(Size.X) * .5f, float(P.X), bOpen ? .95f : .75f);
        // A pen just before the words says "write here"; the star on the line breathes until something is typed.
        const FVector2D Pen(PX - 30, Base + 1); const FLinearColor Ink = Alpha(Mint, bOpen ? .95f : .75f);
        Stroke(Out, L + 2, G, { Pen + FVector2D(-7, 9), Pen + FVector2D(7, -7), Pen + FVector2D(10, -4), Pen + FVector2D(-4, 12), Pen + FVector2D(-8, 13), Pen + FVector2D(-7, 9) }, Ink, 1.6f);
        Stroke(Out, L + 2, G, { Pen + FVector2D(4, -4), Pen + FVector2D(7, -1) }, Ink, 1.4f);
        const float Breath = .5f + .5f * FMath::Sin(T * 2.2f);
        Disc(Out, L + 2, G, FVector2D(Size.X * .5f, LineY), 10 + 6 * Breath, Alpha(Mint, .10f + .16f * Breath), FLinearColor::Transparent, 16);
        if (bOpen) { Fill(Out, L + 3, G, FVector2D(PX - 8, Y + 2), FVector2D(3, LineH - 4), 1.5f, Alpha(Aurora(T * .4f), .55f + .45f * FMath::Cos(T * 6.5f))); }
        Batch.Flush(); Out.PopClip();
        return L + 4;
    }
    Underline(Out, L, G, X + Total * .5f, FMath::Min(Total, float(Size.X)), .95f);
    for (int32 I = 0; I < Parts.Num(); ++I)
    {
        const FRun& R = Parts[I]; const float W = Widths[I];
        if (R.Kind == 3)
        {
            Fill(Out, L + 1, G, FVector2D(X - 4, Y - 2), FVector2D(W + 8, LineH + 4), 10, Alpha(Aurora(T * .2f), .55f), Alpha(Aurora(T * .2f + .35f), .55f), Orient_Horizontal);
        }
        TextAt(Out, L + 2, G, FVector2D(X + 1, Y + 1.5f), R.Text, F, FLinearColor(0, 0, 0, .5f));
        TextAt(Out, L + 3, G, FVector2D(X, Y), R.Text, F, R.Kind == 0 ? Paper : FLinearColor(.86f, 1.f, .96f));
        if (R.Kind == 1 || R.Kind == 2) { Fill(Out, L + 3, G, FVector2D(X + 2, Y + LineH - 1), FVector2D(W - 4, 2.5f), 1.2f, Alpha(Mint, .9f)); }
        X += W;
    }
    if (bOpen) { Fill(Out, L + 4, G, FVector2D(X + 4, Y + 2), FVector2D(3, LineH - 4), 1.5f, Alpha(Aurora(T * .4f), .55f + .45f * FMath::Cos(T * 6.5f))); }
    Batch.Flush(); Out.PopClip();
    return L + 5;
}

// ---------------------------------------------------------------- coin

int32 SLightBoxCoin::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out);
    const FVector2D C = G.GetLocalSize() * .5; const float R = float(FMath::Min(G.GetLocalSize().X, G.GetLocalSize().Y)) * .5f - .5f;
    const float A = Style.GetColorAndOpacityTint().A;
    Disc(Out, L, G, C, R, FLinearColor(1.f, .9f, .55f, A), FLinearColor(.86f, .58f, .16f, A), 32);
    Ring(Out, L + 1, G, C, R * .72f, R * .14f, 0, 360, [&](float) { return FLinearColor(.62f, .38f, .06f, .55f * A); }, 32);
    Ring(Out, L + 1, G, C, R * .36f, R * .2f, 40, 280, [&](float) { return FLinearColor(.5f, .3f, .04f, .9f * A); }, 20);
    Disc(Out, L + 2, G, C + FVector2D(-R * .38f, -R * .42f), R * .22f, FLinearColor(1, 1, 1, .85f * A), FLinearColor(1, 1, 1, 0), 10);
    return L + 3;
}

// ---------------------------------------------------------------- keyboard

namespace
{
constexpr float Pad = 16, BarTop = 66, BarH = 56, GridTop = 138, Gap = 8;
constexpr double ToggleWindow = 1.0;
}

FSlateRect SLightBoxKeyboard::KeyRect(int32 Key) const
{
    if(Key>=KanaMode) { const int32 I=Key-KanaMode; const float X=I==3?328:Pad+I*100; return FSlateRect(X,14,I==3?Width-Pad:X+92,56); }
    const float KW = (Width - 2 * Pad - 3 * Gap) / 4, KH = (Height - GridTop - Pad - 3 * Gap) / 4;
    const int32 Col = Key >= Delete ? 3 : Key % 3, Row = Key >= Delete ? Key - Delete : Key / 3;
    const float X = Pad + Col * (KW + Gap), Y = GridTop + Row * (KH + Gap);
    return FSlateRect(X, Y, X + KW, Y + KH);
}

int32 SLightBoxKeyboard::HitKey(FVector2D P) const
{
    for(int32 K=KanaMode;K<=Shift;++K) { if(KeyRect(K).ContainsPoint(FVector2f(P))) { return K; } }
    for (int32 K = 0; K < 12; ++K) { if (KeyRect(K).ExtendBy(FMargin(Gap * .5f)).ContainsPoint(FVector2f(P))) { return K; } }
    for (int32 K = Delete; K <= Close; ++K) { if (KeyRect(K).ExtendBy(FMargin(Gap * .5f)).ContainsPoint(FVector2f(P))) { return K; } }
    return INDEX_NONE;
}

int32 SLightBoxKeyboard::FlickDirection(FVector2D D) const
{
    if (D.Size() < 26) { return 0; }
    return FMath::Abs(D.X) > FMath::Abs(D.Y) ? (D.X < 0 ? 1 : 3) : (D.Y < 0 ? 2 : 4);
}

FReply SLightBoxKeyboard::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
    if (ActivePointer != INDEX_NONE) { return FReply::Handled(); }
    ActivePointer = E.GetPointerIndex(); ActiveUser = E.GetUserIndex();
    Pressed = INDEX_NONE;

    const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if (P.Y >= BarTop && P.Y <= BarTop + BarH)
    {
        bBarDrag = true; bBarMoved = false; BarStart = P; BarScrollStart = BarScroll;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    Pressed = HitKey(P); PressPos = P; Flick = 0; PressedAt = FPlatformTime::Seconds();
    if (Pressed == Delete) { LastTapKey = INDEX_NONE; Composer->Backspace(); RepeatAt = PressedAt + .45; }
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SLightBoxKeyboard::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton && E.GetEffectingButton().IsValid()) { return FReply::Unhandled(); }
    if (ActivePointer != int32(E.GetPointerIndex()) || ActiveUser != int32(E.GetUserIndex())) { return FReply::Handled(); }
    if (!HasMouseCaptureByUser(E.GetUserIndex(), E.GetPointerIndex())) { return FReply::Unhandled(); }
    const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if (bBarDrag)
    {
        const float Dx = float(P.X - BarStart.X);
        bBarMoved |= FMath::Abs(Dx) > 8;
        BarScroll = FMath::Clamp(BarScrollStart - Dx, 0.f, BarScrollMax);
    }
    else if ((Pressed >= 0 && Pressed < 12 && (Pressed != 9 || Mode!=0)) || Pressed == Convert) { Flick = FlickDirection(P - PressPos); }
    return FReply::Handled();
}

FReply SLightBoxKeyboard::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton && E.GetEffectingButton().IsValid()) { return FReply::Unhandled(); }
    if (ActivePointer != int32(E.GetPointerIndex()) || ActiveUser != int32(E.GetUserIndex())) { return FReply::Handled(); }

    const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if (bBarDrag && !bBarMoved && P.Y >= BarTop && P.Y <= BarTop + BarH && P.X >= Pad && P.X <= Width - Pad)
    {
        for (int32 I = 0; I < BarHits.Num(); ++I)
        {
            if (P.X >= BarHits[I].X && P.X <= BarHits[I].X + BarHits[I].Y) { Composer->Pick(I); BarScroll = 0; break; }
        }
    }
    else if (!bBarDrag && Pressed != INDEX_NONE)
    {
        if (Pressed < Delete) { Flick = FlickDirection(P - PressPos); Release(Pressed, Flick); }
        else if (Pressed == Convert || HitKey(P) == Pressed) { Release(Pressed, FlickDirection(P - PressPos)); }
    }
    bBarDrag = false; Pressed = INDEX_NONE; Flick = 0; ActivePointer = INDEX_NONE;
    return FReply::Handled().ReleaseMouseCapture();
}

void SLightBoxKeyboard::Emit(int32 Key, const FString& Glyph)
{
    const FSlateRect R = KeyRect(Key);
    if(Bursts.Num()>=4) { Bursts.RemoveAt(0); }
    Bursts.Add({ FVector2D(R.GetCenter()), Glyph, FPlatformTime::Seconds() });
}

#if !UE_BUILD_SHIPPING
void SLightBoxKeyboard::ShowLatin() { Mode=1; Pressed=INDEX_NONE; Composer->SetText(TEXT("Light Atelier 2026")); }
#endif

FString SLightBoxKeyboard::Glyphs(int32 Key) const
{
    if(Key<0 || Key>=12) { return FString(); }
    if(Mode==0) { return LightBoxKana::FlickGlyphs(Key); }
    static const TCHAR* Latin[]={TEXT(".,?!@"),TEXT("abc"),TEXT("def"),TEXT("ghi"),TEXT("jkl"),TEXT("mno"),TEXT("pqrs"),TEXT("tuv"),TEXT("wxyz"),TEXT("-_/@"),TEXT(" "),TEXT("'\"():")};
    static const TCHAR* Numbers[]={TEXT("1"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("5"),TEXT("6"),TEXT("7"),TEXT("8"),TEXT("9"),TEXT("+-=/#"),TEXT("0"),TEXT(".,?!")};
    const FString Value=Mode==1?Latin[Key]:Numbers[Key];
    return bUpper&&Mode==1?Value.ToUpper():Value;
}

void SLightBoxKeyboard::Release(int32 Key, int32 Direction)
{
    const double Now = FPlatformTime::Seconds();
    if(Key>=KanaMode)
    {
        Composer->CommitComposition(); LastTapKey=INDEX_NONE; BarScroll=0;
        if(Key==Shift) { bUpper=!bUpper; } else { Mode=Key-KanaMode; }
        Invalidate(EInvalidateWidgetReason::Paint); return;
    }
    if (Key == 9 && Mode==0) { Composer->ReplaceLast([](TCHAR C) { return LightBoxKana::NextVariant(C); }); Emit(Key, TEXT("゛")); LastTapKey = INDEX_NONE; return; }
    if (Key >= 0 && Key < 12)
    {
        const FString Glyphs = this->Glyphs(Key);
        if(Mode!=0)
        {
            if(Glyphs.IsEmpty()) { return; } const int32 Choice=FMath::Clamp(Direction,0,Glyphs.Len()-1);
            FString Glyph=Glyphs.Mid(Choice,1);
            if(Mode==1 && Glyphs.Len()>1 && Direction==0 && LastTapKey==Key && Now-LastTapAt<ToggleWindow && !Composer->Committed.IsEmpty())
            {
                int32 Index=0; if(Glyphs.FindChar(Composer->Committed[Composer->Committed.Len()-1],Index))
                { Glyph=Glyphs.Mid((Index+1)%Glyphs.Len(),1); Composer->Backspace(); }
            }
            Composer->TypeLiteral(Glyph); Emit(Key,Glyph);
            LastTapKey=Direction==0?Key:INDEX_NONE; LastTapAt=Now; SeenRevision=Composer->Revision; return;
        }
        if (Direction == 0 && LastTapKey == Key && Now - LastTapAt < ToggleWindow)
        {
            const FString Cycle = LightBoxKana::ToggleCycle(Key);
            TCHAR Shown = 0;
            const bool bCycled = Composer->ReplaceLast([&](TCHAR C) { int32 I; if (!Cycle.FindChar(C, I)) { return C; } Shown = Cycle[(I + 1) % Cycle.Len()]; return Shown; });
            if (bCycled) { LastTapAt = Now; SeenRevision = Composer->Revision; Emit(Key, FString(1, &Shown)); return; }
        }
        if (Glyphs.IsValidIndex(Direction))
        {
            const FString Glyph = Glyphs.Mid(Direction, 1);
            Composer->Type(Glyph); Emit(Key, Glyph);
        }
        LastTapKey = Direction == 0 ? Key : INDEX_NONE; LastTapAt = Now; SeenRevision = Composer->Revision;
        return;
    }
    LastTapKey = INDEX_NONE;
    if (Key == Convert) { if(Mode!=0) { Composer->TypeLiteral(TEXT(" ")); } else if (Composer->bConverting && (Direction == 1 || Direction == 3)) { Composer->MoveFocus(Direction == 1 ? -1 : 1); } else { Composer->Convert(); } }
    else if (Key == Enter) { Composer->CommitComposition(); } // Sending belongs to the top paper-plane button.
    else if (Key == Close) { OnClose.ExecuteIfBound(); }
}

void SLightBoxKeyboard::CancelInteraction()
{
    // Closing by Escape/menu/voice must also stop a held Delete before the fade finishes.
    if (ActivePointer != INDEX_NONE && HasMouseCaptureByUser(ActiveUser, ActivePointer))
    {
        if (const auto User = FSlateApplication::Get().GetUser(ActiveUser)) { User->ReleaseCapture(ActivePointer); }
    }
    Pressed = ActivePointer = LastTapKey = INDEX_NONE; bBarDrag = false; Flick = 0;
}

void SLightBoxKeyboard::Tick(const FGeometry& G, double T, float Dt)
{
    SLeafWidget::Tick(G, T, Dt);
    const double Now = FPlatformTime::Seconds();
    if (Pressed == Delete && ActivePointer != INDEX_NONE && HasMouseCaptureByUser(ActiveUser, ActivePointer) && Now >= RepeatAt) { Composer->Backspace(); RepeatAt = Now + .07; }
    Bursts.RemoveAll([Now](const FBurst& B) { return Now - B.Born > .6; });
    if (SeenRevision != Composer->Revision) { SeenRevision = Composer->Revision; BarScroll = 0; LastTapKey = INDEX_NONE; bBarMoved = true; }
    const bool Changed=PaintedRevision!=Composer->Revision||PaintedPressed!=Pressed||PaintedFlick!=Flick||PaintedScroll!=BarScroll||PaintedMode!=Mode||bPaintedUpper!=bUpper;
    const bool Animate=!Bursts.IsEmpty();
    if(Changed || bHadBursts!=Animate || (Animate&&Now>=NextPaintAt))
    {
        Invalidate(EInvalidateWidgetReason::Paint); NextPaintAt=Now+1./30.;
    }
    PaintedMode=Mode; bPaintedUpper=bUpper;
    PaintedRevision=Composer->Revision; PaintedPressed=Pressed; PaintedFlick=Flick; PaintedScroll=BarScroll; bHadBursts=Animate;
}

int32 SLightBoxKeyboard::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out, Style.GetColorAndOpacityTint().A);
    const float T = float(FPlatformTime::Seconds() - FMath::Floor(FPlatformTime::Seconds() / 1000.0) * 1000.0);
    const double Now = FPlatformTime::Seconds();
    // Local halos stay inside the panel's bounds, behind the keys and candidate clip.
    GlassPanel(Out,L,G,FVector2D(0),FVector2D(Width,Height),24);

    const TCHAR* Tabs[]={TEXT("かな"),TEXT("ABC"),TEXT("123"),TEXT("a / A")};
    for(int32 K=KanaMode;K<=Shift;++K)
    {
        const FSlateRect R=KeyRect(K); const bool Selected=K==KanaMode+Mode || (K==Shift&&bUpper);
        KeyTile(Out,L,G,FVector2D(R.Left,R.Top),FVector2D(R.GetSize()),14,Mint,Selected?.8f:.06f);
    }
    for(int32 K=KanaMode;K<=Shift;++K)
    {
        const FSlateRect R=KeyRect(K); const bool Selected=K==KanaMode+Mode || (K==Shift&&bUpper);
        Text(Out,L+1,G,FVector2D(R.GetCenter()),Tabs[K-KanaMode],Font(20),Selected?Mint:Alpha(Paper,.70f));
    }
    // Candidate bar.
    const TArray<FString> List = Mode==0?Composer->Suggestions():TArray<FString>();
    const int32 Chosen = Composer->bConverting ? (Composer->Segments.IsValidIndex(Composer->Focus) ? Composer->Segments[Composer->Focus].Choice : 0) : 0;
    const FSlateFontInfo CF = Spaced(21, 60);
    BarHits.Reset(); BarScrollMax = 0;
    Batch.Flush();
    Out.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect(FSlateRect(Pad, BarTop, Width - Pad, BarTop + BarH))));
    if (List.IsEmpty())
    {
        Text(Out, L + 2, G, FVector2D(Width * .5f, BarTop + BarH * .5f), Mode==0?TEXT("フリックで入力 · 変換で漢字に"):Mode==1?TEXT("英字を直接入力 · タップ / フリック"):TEXT("数字と記号を直接入力"), Spaced(16, 100), Alpha(Paper, .5f));
    }
    else
    {
        // Words only, split by faint hairlines; the chosen one glows with a short underline.
        float X = Pad + 2 - BarScroll; const float Mid = BarTop + BarH * .5f;
        for (int32 I = 0; I < List.Num(); ++I)
        {
            const float W = float(Measure(List[I], CF).X) + 34;
            const bool bOn = I == Chosen;
            KeyTile(Out,L+1,G,FVector2D(X,BarTop+7),FVector2D(W,BarH-14),14,Mint,I==Chosen?.45f:0.f);
            Text(Out, L + 3, G, FVector2D(X + W * .5f, Mid - 2), List[I], CF, bOn ? FLinearColor(.55f, 1.f, .90f) : Alpha(Paper, .82f));

            BarHits.Add(FVector2f(X, W));
            X += W + 8;
        }
        BarScrollMax = FMath::Max(0.f, X + BarScroll - (Width - Pad));
        // Fade out rather than cut a word at the right edge.
        if (BarScrollMax - BarScroll > 1) { Fill(Out, L + 4, G, FVector2D(Width - Pad - 56, BarTop), FVector2D(56, BarH), 0, FLinearColor(.010f, .015f, .040f, 0), FLinearColor(.010f, .015f, .040f, .96f), Orient_Horizontal); }
        if (BarScroll > 1) { Fill(Out, L + 4, G, FVector2D(Pad, BarTop), FVector2D(40, BarH), 0, FLinearColor(.010f, .015f, .040f, .96f), FLinearColor(.010f, .015f, .040f, 0), Orient_Horizontal); }
    }
    Batch.Flush(); Out.PopClip();
    GlowLine(Out, L + 2, G, FVector2D(Pad + 6, GridTop - 7), Width - 2 * Pad - 12, .5f, .5f, .6f, 1.f);

    const bool bComposing=Composer->IsComposing()||Composer->bConverting;
    auto Face=[&](int32 Key)
    {
        const FSlateRect R=KeyRect(Key); const bool bDown=Pressed==Key;
        const FVector2D P(R.Left,R.Top),S(R.GetSize());
        KeyTile(Out,L+4,G,P,S,18,Mint,bDown?.85f:Key==Enter?.20f:0.f);
    };
    for(int32 K=0;K<12;++K) { Face(K); }
    for(int32 K=Delete;K<=Close;++K) { Face(K); }
    auto DrawKey=[&](int32 Key)
    {
        const FSlateRect R=KeyRect(Key); const bool bDown=Pressed==Key;
        const FVector2D S(R.GetSize()),C(R.GetCenter()); const bool bPrimary=Key==Enter;
        const FLinearColor Fore = bDown || bPrimary ? Mint : Paper;
        if (Key < 12)
        {
            const FString Glyphs = this->Glyphs(Key);
            if (Key == 9 && Mode==0) { Text(Out, L + 6, G, C, TEXT("゛゜小"), Spaced(18, 80), Alpha(Fore, .9f)); return; }
            if(Mode!=0) { Text(Out,L+6,G,C,Mode==1&&Key==10?TEXT("空白"):Mode==1?Glyphs:Glyphs.Left(1),Font(Mode==1?23:32),Fore); return; }
            Text(Out, L + 6, G, C + FVector2D(0, -2), Glyphs.Left(1), Font(31), Fore);
            // Tiny flick guide around the edge.
            const FVector2D Off[] = { {0, 0}, {-S.X * .34, 0}, {0, -S.Y * .34}, {S.X * .34, 0}, {0, S.Y * .33} };
            for (int32 D = 1; D < 5 && D < Glyphs.Len(); ++D) { Text(Out, L + 6, G, C + Off[D], Glyphs.Mid(D, 1), Font(15), Alpha(Fore, bDown ? .72f : .55f)); }
            return;
        }
        if (Key == Delete)
        {
            const float U = float(S.X) / 100.f; const FVector2D O = C + FVector2D(2, 0) * U;
            Stroke(Out, L + 6, G, { O + FVector2D(-26, 0) * U, O + FVector2D(-12, -15) * U, O + FVector2D(20, -15) * U, O + FVector2D(20, 15) * U, O + FVector2D(-12, 15) * U, O + FVector2D(-26, 0) * U }, Fore, 2.6f);
            Stroke(Out, L + 6, G, { O + FVector2D(-3, -6) * U, O + FVector2D(9, 6) * U }, Fore, 2.6f);
            Stroke(Out, L + 6, G, { O + FVector2D(9, -6) * U, O + FVector2D(-3, 6) * U }, Fore, 2.6f);
        }
        else if (Key == Convert)
        {
            const TCHAR* Label = Mode!=0?TEXT("空白"):!bComposing ? TEXT("空白") : Composer->bConverting ? TEXT("次候補") : TEXT("変換");
            Text(Out, L + 6, G, C, Label, Spaced(19, 40), bComposing ? Mint : Alpha(Paper, .9f));
            if (Composer->bConverting) { Text(Out, L + 6, G, C + FVector2D(0, 28), TEXT("‹ 文節 ›"), Font(14), Alpha(Paper,.7f)); }
        }
        else if (Key == Enter)
        {
            Text(Out, L + 6, G, C, TEXT("確定"), Spaced(20, 40), FLinearColor(.55f, 1.f, .90f));
        }
        else if (Key == Close)
        {
            // Close the keyboard: a downward chevron (it slides away) over the word, matching the other labelled keys.
            const float U = float(S.X) / 100.f;
            Stroke(Out, L + 6, G, { C + FVector2D(-11, -16) * U, C + FVector2D(0, -6) * U, C + FVector2D(11, -16) * U }, Fore, 2.4f);
            Text(Out, L + 6, G, C + FVector2D(0, 15 * U), TEXT("閉じる"), Spaced(17, 100), Alpha(Fore, .9f));
        }
    };
    for (int32 K = 0; K < 12; ++K) { DrawKey(K); }
    for (int32 K = Delete; K <= Close; ++K) { DrawKey(K); }

    // Typed glyphs float up from their key in a little burst.
    for (const FBurst& B : Bursts)
    {
        const float A = float((Now - B.Born) / .6), E = EaseOut(A);
        for (int32 K = 0; K < 4; ++K)
        {
            const float Ang = 2 * PI * K / 4 + B.Born;
            Disc(Out, L + 7, G, B.At + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * (12 + 46 * E), 3.5f * (1 - A), Alpha(Aurora(K / 8.f + T * .2f), 1 - A), Alpha(Aurora(K / 8.f), 0), 10);
        }
        Text(Out, L + 8, G, B.At + FVector2D(0, -70 * E), B.Glyph, Font(30 + 10 * E), Alpha(Aurora(T * .3f), 1 - A));
    }

    // Flick guide: the five choices as a cross over the pressed key.
    if (Pressed >= 0 && Pressed < 12 && (Pressed != 9 || Mode!=0))
    {
        const FString Glyphs = this->Glyphs(Pressed); const FSlateRect R = KeyRect(Pressed);
        if(Glyphs.Len()<2) { return L+13; }
        const FVector2D S(R.GetSize()),KeyCentre(R.GetCenter());
        // Edge keys must still show all flick choices inside the visible keyboard.
        const FVector2D C(FMath::Clamp(KeyCentre.X,S.X*1.5+4,Width-S.X*1.5-4),FMath::Clamp(KeyCentre.Y,S.Y*1.5+4,Height-S.Y*1.5-4));
        const FVector2D Off[] = { {0, 0}, {-S.X, 0}, {0, -S.Y}, {S.X, 0}, {0, S.Y} };
        Disc(Out, L + 9, G, C, float(S.X) * 2.1f, FLinearColor(0, 0, 0, .8f), FLinearColor(0, 0, 0, 0));
        for (int32 D = 0; D < 5 && D < Glyphs.Len(); ++D)
        {
            const FVector2D Cell = C + Off[D], Half = S * .47; const bool bOn = D == Flick;
            if(bOn) { Disc(Out,L+10,G,Cell,float(S.X)*.85f,Alpha(Mint,.16f),FLinearColor::Transparent,24); }
            Fill(Out,L+11,G,Cell-Half,Half*2,16,FLinearColor(.014f,.022f,.052f,.96f));
            KeyTile(Out,L+11,G,Cell-Half,Half*2,16,Mint,bOn?1.f:.15f);
            Text(Out, L + 12, G, Cell, Glyphs.Mid(D, 1), Font(bOn ? 38 : 28), bOn ? Mint : Paper);
        }
    }
    return L + 13;
}

// ---------------------------------------------------------------- sensor dock

namespace
{
const FLinearColor WarmCol(1.f, .55f, .25f);

}

void SLightBoxSensorDock::Tick(const FGeometry& G, double T, float Dt)
{
    SLeafWidget::Tick(G, T, Dt);
    ClapAge += Dt;
    for (float& A : TapAge) { A += Dt; }
    const FLightBoxSensorReading* R = Reading ? Reading() : nullptr;
    for (bool& A : Available) { A = false; }
    float Target[Count] = {}; float DarkT = 0, BrightT = 0;
    if (R)
    {
        if (R->bClap) { ClapAge = 0; }
        Target[0] = R->Breath; Target[1] = R->Warmth; DarkT = R->Darkness; BrightT = R->Brightness; Target[3] = R->Near; Target[4] = R->Magnet; Target[5] = R->Tilt;
        Available[0] = R->bHasMic; Available[1] = true; Available[2] = R->bHasLight; Available[3] = R->bHasNear; Available[4] = R->bHasMagnet; Available[5] = R->bHasTilt;
    }
#if !UE_BUILD_SHIPPING
    if (bDemo)
    {
        for (bool& A : Available) { A = true; }
        Target[0] = Target[1] = Target[4] = Target[5] = DarkT = 0; BrightT = .9f; ClapAge = 10;
    }
#endif
    Dark = FMath::FInterpTo(Dark, DarkT, Dt, 10.f); Bright = FMath::FInterpTo(Bright, BrightT, Dt, 10.f);
    Target[2] = FMath::Max(DarkT, BrightT);
    for (int32 I = 0; I < Count; ++I) { Level[I] = FMath::FInterpTo(Level[I], Target[I], Dt, 10.f); }
    for (int32 I = 0; I < Count; ++I)
    {
        if (IsOn && !IsOn(I)) { Level[I] = 0; if (I == 0) { ClapAge = 10; } if (I == 2) { Dark = Bright = 0; } }
    }
    uint16 State=0; bool Active=ClapAge<.8f;
    for(int32 I=0;I<Count;++I)
    {
        State|=uint16(Available[I])<<I; State|=uint16(!IsOn||IsOn(I))<<(I+Count);
        Active|=Level[I]>.002f||TapAge[I]<.3f;
    }
    const double Now=FPlatformTime::Seconds();
    if(State!=LastState || Active!=bWasActive || (Active&&Now>=NextPaintAt))
    {
        Invalidate(EInvalidateWidgetReason::Paint); NextPaintAt=Now+1./30.;
    }
    LastState=State; bWasActive=Active;
    if(R) { Snapshot=*R; }
    // A short history per sensor for the detail card's trace (15 samples a second).
    if(Now>=NextSampleAt)
    {
        NextSampleAt=Now+1./15.; HistoryAt=(HistoryAt+1)%HistoryLength;
        for(int32 I=0;I<Count;++I) { History[I][HistoryAt]=FMath::Clamp(I==0?FMath::Max(Level[0],FMath::Max(0.f,1-ClapAge/.8f)):Level[I],0.f,1.f); }
    }
    if(PressedBadge!=INDEX_NONE&&DetailBadge==INDEX_NONE&&Now-PressedAt>=.45) { DetailBadge=PressedBadge; }
    // The card eases in over ~0.18 s and out a little faster; it keeps the last badge while fading.
    if(DetailBadge!=INDEX_NONE) { DetailShown=DetailBadge; }
    const float Was=DetailAppear;
    DetailAppear=FMath::Clamp(DetailAppear+(DetailBadge!=INDEX_NONE?Dt/.18f:-Dt/.12f),0.f,1.f);
    if(DetailAppear<=0) { DetailShown=INDEX_NONE; }
    if(Was!=DetailAppear) { Invalidate(EInvalidateWidgetReason::Paint); }
    else if(DetailBadge!=INDEX_NONE&&Now>=NextPaintAt) { Invalidate(EInvalidateWidgetReason::Paint); NextPaintAt=Now+1./20.; }
}

int32 SLightBoxSensorDock::HitBadge(FVector2D P) const
{
    if(P.X>=0 && P.X<=CellW+20 && P.Y>=10 && P.Y<10+CellH*Count) { return FMath::Clamp(int32((P.Y-10)/CellH),0,Count-1); }
    return INDEX_NONE;
}

FReply SLightBoxSensorDock::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
    if (ActivePointer != INDEX_NONE) { return FReply::Handled(); }
    const int32 I = HitBadge(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
    if (I == INDEX_NONE) { return FReply::Handled(); }
    // Unavailable sensors can still be held to read why; only available ones toggle.
    PressedBadge = I; PressedAt = FPlatformTime::Seconds(); DetailBadge = INDEX_NONE;
    ActivePointer = E.GetPointerIndex(); ActiveUser = E.GetUserIndex();
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SLightBoxSensorDock::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
    if (!E.IsTouchEvent() && E.GetEffectingButton() != EKeys::LeftMouseButton && E.GetEffectingButton().IsValid()) { return FReply::Unhandled(); }
    if (ActivePointer != int32(E.GetPointerIndex()) || ActiveUser != int32(E.GetUserIndex())) { return FReply::Handled(); }
    const int32 I = PressedBadge; const bool bWasDetail = DetailBadge != INDEX_NONE;
    PressedBadge = ActivePointer = DetailBadge = INDEX_NONE;
    Invalidate(EInvalidateWidgetReason::Paint);
    if (!bWasDetail && I != INDEX_NONE && HitBadge(G.AbsoluteToLocal(E.GetScreenSpacePosition())) == I && Available[I])
    {
        TapAge[I] = 0; if (OnToggle) { OnToggle(I); }
    }
    return FReply::Handled().ReleaseMouseCapture();
}

int32 SLightBoxSensorDock::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const
{
    if(Style.GetColorAndOpacityTint().A<=.001f) { return L; }
    FVectorPaintBatch Batch(Out,Style.GetColorAndOpacityTint().A);
    const float Clap=FMath::Max(0.f,1-ClapAge/.8f);
    float Lv[Count],Tap[Count]; bool On[Count]; FLinearColor Col[Count];
    for(int32 I=0;I<Count;++I)
    {
        On[I]=Available[I]&&(!IsOn||IsOn(I));
        Lv[I]=On[I]?FMath::Clamp(I==0?FMath::Max(Level[0],Clap):Level[I],0.f,1.f):0;
        Tap[I]=FMath::Max(0.f,1-TapAge[I]/.3f);
        Col[I]=FLinearColor(.12f,1.f,.88f); // Shared cyan feedback, independent of the scene effect colour.
    }
    {
        const FVector2D Size=ComputeDesiredSize(1);
        Fill(Out,L,G,FVector2D(0),Size,26,FLinearColor(.016f,.024f,.060f,.42f),FLinearColor(.008f,.012f,.034f,.50f));
        TintStroke(Out,L,G,RoundedOutline(G,FVector2D(0),Size,26),[&](const FVector2D& P){return Hairline(float(P.Y/Size.Y)).CopyWithNewOpacity(.22f);},1.f);
    }
    for(int32 I=0;I<Count;++I)
    {
        const FVector2D C=Centre(I); const float R=Radius+2, Energy=FMath::Max(Lv[I],Tap[I]*.6f);
        if(Energy>.01f)
        {
            Fill(Out,L,G,FVector2D(12,12+CellH*I),FVector2D(CellW-4,CellH-4),20,Alpha(Mint,.15f*Energy),Alpha(Mint,.035f*Energy));
            Disc(Out,L,G,C,Radius*1.6f,Alpha(Col[I],.12f*Energy),FLinearColor::Transparent,24);
        }
        const float U=1.6f; auto P=[&](float X,float Y){return C+FVector2D(X,Y)*U;};
        const FLinearColor Ic=On[I]?FMath::Lerp(Paper,Col[I],Lv[I]):Alpha(Paper,Available[I]?.42f:.34f);
        const float W=2.1f*U;
        switch(I)
        {
        case 0:
            Disc(Out,L,G,C,2.4f*U,Ic,Ic,8);
            for(int32 K=0;K<2;++K) { const float Rr=(6+K*4)*U; ArcStroke(Out,L,G,C,Rr,-42,84,Ic,W); ArcStroke(Out,L,G,C,Rr,138,84,Ic,W); }
            break;
        case 1:
            for(int32 K=-1;K<=1;++K)
            {
                TArray<FVector2D> Heat;
                for(int32 J=0;J<=12;++J) { const float T=J/12.f; Heat.Add(P(K*7+FMath::Sin(T*2*PI)*1.8f,10-T*21)); }
                Stroke(Out,L,G,Heat,Ic,1.7f*U);
            }
            break;
        case 2:
            if(Lv[I]>.02f) { Disc(Out,L,G,C,5.8f*U,Ic,Ic,20); }
            else { ArcStroke(Out,L,G,C,5.8f*U,0,360,Ic,W); }
            for(int32 K=0;K<8;++K) { const float A=2*PI*K/8; const FVector2D D(FMath::Cos(A),FMath::Sin(A)); Stroke(Out,L,G,{C+D*9*U,C+D*11.4f*U},Ic,1.7f*U); }
            break;
        case 3:
            Fill(Out,L,G,P(-5,0),FVector2D(12,10)*U,4*U,Ic);
            for(const FVector4f Finger:{FVector4f(-5,-9,3,14),FVector4f(-1.5f,-12,3,17),FVector4f(2,-10,3,15),FVector4f(5.5f,-6,3,12)})
            { Fill(Out,L,G,P(Finger.X,Finger.Y),FVector2D(Finger.Z,Finger.W)*U,1.5f*U,Ic); }
            Poly(Out,L,G,{P(-5,6),P(-10,0),P(-10,-2),P(-8,-3),P(-4,1)},Ic);
            break;
        case 5:
        {
            // A phone leaning over, with a short arc for the motion.
            const float A=FMath::DegreesToRadians(-18.f); const FVector2D X(FMath::Cos(A),FMath::Sin(A)),Y(-X.Y,X.X);
            auto Q=[&](float Px,float Py){return C+(X*Px+Y*Py)*U;};
            Stroke(Out,L,G,{Q(-5,-10),Q(5,-10),Q(5,10),Q(-5,10),Q(-5,-10)},Ic,W*.8f);
            Stroke(Out,L,G,{Q(-1.6f,7),Q(1.6f,7)},Ic,W*.8f);
            ArcStroke(Out,L,G,C,13*U,200,55,Alpha(Ic,Ic.A*.8f),W*.7f);
            break;
        }
        default:
            ArcStroke(Out,L,G,P(0,1),6*U,0,180,Ic,3*U);
            Stroke(Out,L,G,{P(-6,1),P(-6,-8)},Ic,3*U); Stroke(Out,L,G,{P(6,1),P(6,-8)},Ic,3*U);
            Stroke(Out,L,G,{P(-6,-8),P(-6,-5)},Alpha(Mint,On[I]?.95f:.3f),3*U);
            Stroke(Out,L,G,{P(6,-8),P(6,-5)},Alpha(Pink,On[I]?.95f:.3f),3*U);
        }
        if(!On[I]&&Available[I]) { Stroke(Out,L,G,{C+FVector2D(-R*.42f,R*.42f),C+FVector2D(R*.42f,-R*.42f)},Alpha(Paper,.34f),1.2f); }
    }
    // Small status marks distinguish enabled, off and unavailable without a row of gauges.
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector2D At = Centre(I) + FVector2D(0, 54);
        if (On[I]) { Disc(Out, L, G, At, 2.2f, Alpha(Mint, .45f + .5f * Lv[I]), Alpha(Mint, .45f + .5f * Lv[I]), 8); }
        else if (Available[I]) { ArcStroke(Out, L, G, At, 2.8f, 0, 360, Alpha(Paper, .4f), 1.f); }
        else { Stroke(Out, L, G, {At - FVector2D(3, 0), At + FVector2D(3, 0)}, Alpha(Paper, .3f), 1.f); }
    }
    // All geometry above shares one layer/resource; labels follow in a separate text pass.
    for(int32 I=0;I<Count;++I)
    {
        const FString Label=I==0?TEXT("息・拍手"):I==1?TEXT("ぬくもり"):I==2?TEXT("明るさ"):I==3?TEXT("手"):I==4?TEXT("磁石"):TEXT("傾き");
        // An off sensor is simply dimmer (and struck through); no extra words crowd its label.
        // Off is shown by the dimmed, struck-through badge alone; tiny captions were unreadable at a distance.
        const FVector2D At=Centre(I)+FVector2D(0,36); const FSlateFontInfo LabelFont=Spaced(18,30);
        Text(Out,L+1,G,At+FVector2D(0,1),Label,LabelFont,FLinearColor(0,0,0,.75f));
        Text(Out,L+2,G,At,Label,LabelFont,On[I]?FMath::Lerp(Alpha(Paper,.9f),Col[I],Lv[I]):Alpha(Paper,Available[I]?.52f:.35f));
    }
    // Detail card for a held badge, opening to the right over the scene (not clipped to the column).
    if(DetailShown!=INDEX_NONE&&DetailAppear>.001f) { Batch.Flush(); PaintDetail(Out,L+3,G,DetailShown,On[DetailShown],DetailAppear); }
    return L+8;
}

void SLightBoxSensorDock::PaintDetail(FSlateWindowElementList& Out, int32 L, const FGeometry& G, int32 I, bool bOn, float Appear) const
{
    // Same height as the held badge's cell: one large value, one short line, a trace on the right.
    const FLightBoxSensorReading& R = Snapshot;
    const float Lv = History[I][HistoryAt];
    auto Pct = [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(FMath::Clamp(V, 0.f, 1.f) * 100)); };
    FString Main, Sub;
    if (!Available[I]) { Main = TEXT("使えません"); Sub = TEXT("この端末にありません"); }
    else if (!bOn) { Main = TEXT("オフ"); Sub = TEXT("タップでオン"); }
    else switch (I)
    {
    case 0: Main = Pct(R.Breath); Sub = R.Claps > 0 ? FString::Printf(TEXT("拍手 %d ・ 強さ %d%%"), R.Claps, FMath::RoundToInt(R.ClapStrength * 100)) : FString(TEXT("拍手 0")); break;
    case 1: Main = Pct(R.Warmth); Sub = TEXT("指をのせる"); break;
    case 2: Main = R.Lux >= 0 ? FString::Printf(TEXT("%.0f lx"), R.Lux) : FString(TEXT("— lx")); Sub = FString::Printf(TEXT("基準 %.0f lx"), FMath::Max(0.f, R.BaseLux)); break;
    case 3: Main = Pct(R.Near); Sub = R.bProximityNear ? TEXT("近接：近い") : TEXT("近接：遠い"); break;
    case 4: Main = FString::Printf(TEXT("%.0f µT"), R.MagnetMicroTesla); Sub = TEXT("反応 ") + Pct(R.Magnet); break;
    default: Main = FString::Printf(TEXT("%+.0f° %+.0f°"), R.RollDeg, R.PitchDeg); Sub = TEXT("左右 ・ 前後"); break;
    }
    const bool bTrace = Available[I] && bOn;
    const FSlateFontInfo MainFont = Spaced(26, 40), SubFont = Spaced(15, 60);
    const float TextW = FMath::Max(float(Measure(Main, MainFont).X), float(Measure(Sub, SubFont).X));
    const float TraceW = bTrace ? 120 : 0, W = 24 + TextW + (bTrace ? 22 + TraceW : 0) + 22, H = CellH - 8;
    const float E = 1 - FMath::Pow(1 - Appear, 3.f);
    // Grows out of the badge: slides right and unfolds from the column edge.
    const FVector2D Pos(CellW + 26 - 18 * (1 - E), 10 + CellH * I + 4);
    const float Shown = W * (.55f + .45f * E);
    {
        FVectorPaintBatch Batch(Out, E);
        Out.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect(FSlateRect(Pos.X - 12, Pos.Y - 20, Pos.X + Shown, Pos.Y + H + 20))));
        GlassPanel(Out, L, G, Pos, FVector2D(W, H), 18);
        Poly(Out, L, G, { FVector2D(Pos.X - 8, Pos.Y + H * .5f), FVector2D(Pos.X + 1, Pos.Y + H * .5f - 8), FVector2D(Pos.X + 1, Pos.Y + H * .5f + 8) }, FLinearColor(.016f, .024f, .060f, .90f));
        if (bTrace)
        {
            // The last four seconds, newest on the right.
            const FVector2D From(Pos.X + W - 22 - TraceW, Pos.Y + H - 20); const float TH = H - 40;
            Fill(Out, L + 1, G, From - FVector2D(0, .5f), FVector2D(TraceW, 1), .5f, Alpha(Paper, .12f));
            TArray<FVector2D> Line;
            for (int32 K = 0; K < HistoryLength; ++K) { Line.Add(From + FVector2D(TraceW * K / (HistoryLength - 1), -TH * History[I][(HistoryAt + 1 + K) % HistoryLength])); }
            Stroke(Out, L + 1, G, Line, FLinearColor(.45f, 1.f, .88f, .9f), 1.5f);
            Disc(Out, L + 1, G, Line.Last(), 6, Alpha(Mint, .35f), FLinearColor::Transparent, 12);
            Disc(Out, L + 1, G, Line.Last(), 2.3f, Paper, Alpha(Mint, .6f), 10);
        }
        Batch.Flush();
        const FLinearColor Ink = (bTrace ? FMath::Lerp(Paper, FLinearColor(.55f, 1.f, .90f), Lv) : Alpha(Paper, .8f)).CopyWithNewOpacity(E);
        TextAt(Out, L + 2, G, Pos + FVector2D(24, 12), Main, MainFont, Ink);
        TextAt(Out, L + 2, G, Pos + FVector2D(24, H - 34), Sub, SubFont, Alpha(Paper, .62f * E));
        Out.PopClip();
    }
}
