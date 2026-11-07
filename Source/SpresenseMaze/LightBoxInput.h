#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "LightBoxSensors.h"

class FLightBoxComposer;

/** Shared click handling for the animated leaf buttons (mouse and touch). */
class SLightBoxPressable : public SLeafWidget
{
public:
    virtual FReply OnTouchStarted(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonDown(G, E); }
    virtual FReply OnTouchEnded(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonUp(G, E); }
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { bPressed = false; ActivePointer = INDEX_NONE; }
    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::Hand); }
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
protected:
    TAttribute<bool> Enabled = true;
    FSimpleDelegate Clicked;
    int32 ActivePointer = INDEX_NONE, ActiveUser = INDEX_NONE;
    bool bPressed = false;
    bool bWasAnimating = false, bLastEnabled = true;
    double NextPaintAt = 0;
    virtual bool NeedsAnimation() const { return false; }
    virtual double PaintInterval() const { return 1. / 30.; }
    float Press = 0;  // eased 0..1
    double Born = FPlatformTime::Seconds();
    float Time() const { return float(FPlatformTime::Seconds() - Born); }
};

/** Quiet glass halo at rest; cyan light and sampled microphone levels while listening. */
class SLightBoxMicButton final : public SLightBoxPressable
{
public:
    SLATE_BEGIN_ARGS(SLightBoxMicButton) {}
        SLATE_ATTRIBUTE(bool, Listening)
        SLATE_ATTRIBUTE(float, VoiceLevel)
        SLATE_ATTRIBUTE(bool, IsActive)
        SLATE_EVENT(FSimpleDelegate, OnClicked)
    SLATE_END_ARGS()
    void Construct(const FArguments& A) { Listening = A._Listening; VoiceLevel = A._VoiceLevel; Enabled = A._IsActive; Clicked = A._OnClicked; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(190, 220); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
private:
    virtual bool NeedsAnimation() const override { return Enabled.Get() || Listen > .001f; }
    virtual double PaintInterval() const override { return Listening.Get() ? 1. / 30. : 1. / 15.; }
    TAttribute<bool> Listening;
    TAttribute<float> VoiceLevel;
    friend class FLightBoxVoiceMeterTest;
    float Listen = 0, Envelope = 0, RingPhase = 0, Samples[7] = {};
    double NextSampleAt = 0;
};

/** Send button: aurora disc with a paper plane that takes off when a request is sent. */
class SLightBoxSendButton final : public SLightBoxPressable
{
public:
    SLATE_BEGIN_ARGS(SLightBoxSendButton) {}
        SLATE_ATTRIBUTE(bool, Ready)
        SLATE_ATTRIBUTE(bool, Busy)
        SLATE_EVENT(FSimpleDelegate, OnClicked)
    SLATE_END_ARGS()
    void Construct(const FArguments& A) { Enabled = TAttribute<bool>::CreateLambda([R = A._Ready, B = A._Busy]() { return R.Get() || B.Get(); }); Ready = A._Ready; Busy = A._Busy; Clicked = A._OnClicked; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(76); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
    /** Fly the plane away; the button grows a fresh one. */
    void Launch() { LaunchedAt = Time(); }
private:
    virtual bool NeedsAnimation() const override { return Ready.Get() || Busy.Get() || Time()-LaunchedAt<1.1f; }
    TAttribute<bool> Ready, Busy;
    float LaunchedAt = -10;
};

/** Typed words shown on the scene itself (no box): committed text, the composition, and a caret. */
class SLightBoxPrompt final : public SLightBoxPressable
{
public:
    SLATE_BEGIN_ARGS(SLightBoxPrompt) {}
        SLATE_ARGUMENT(TSharedPtr<FLightBoxComposer>, Composer)
        SLATE_ATTRIBUTE(bool, Open)
        SLATE_ATTRIBUTE(float, MaxWidth)
        SLATE_EVENT(FSimpleDelegate, OnClicked)
    SLATE_END_ARGS()
    void Construct(const FArguments& A) { Composer = A._Composer; Open = A._Open; MaxWidth = A._MaxWidth; Clicked = A._OnClicked; }
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
private:
    virtual bool NeedsAnimation() const override;
    TSharedPtr<FLightBoxComposer> Composer;
    TAttribute<bool> Open;
    TAttribute<float> MaxWidth;
    uint32 SeenRevision = MAX_uint32;
};

/** Small gold coin that stands for credits. */
class SLightBoxCoin final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxCoin) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(18); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
};

/**
 * Kana, literal Latin and number 12-key keyboard drawn inside the scene (the system keyboard is awkward in landscape).
 * Tap cycles a row, a flick picks a vowel directly; 変換 converts, the bar lists spellings.
 */
class SLightBoxKeyboard final : public SLeafWidget
{
public:
    virtual FReply OnTouchStarted(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonDown(G, E); }
    virtual FReply OnTouchEnded(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonUp(G, E); }
    virtual FReply OnTouchMoved(const FGeometry& G, const FPointerEvent& E) override { return OnMouseMove(G, E); }
    SLATE_BEGIN_ARGS(SLightBoxKeyboard) {}
        SLATE_ARGUMENT(TSharedPtr<FLightBoxComposer>, Composer)
        SLATE_EVENT(FSimpleDelegate, OnClose)
    SLATE_END_ARGS()
    void Construct(const FArguments& A) { Composer = A._Composer; OnClose = A._OnClose; }
    static constexpr float Width = 470, Height = 620;
    bool IsLiteralMode() const { return Mode!=0; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
    virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { if (ActivePointer != INDEX_NONE) { LastTapKey = INDEX_NONE; } Pressed = INDEX_NONE; ActivePointer = INDEX_NONE; bBarDrag = false; }
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
    /** Review helper: show a flick in progress on a key. */
#if !UE_BUILD_SHIPPING
    void ShowLatin();
    void ShowFlick(int32 Key, int32 Direction) { Pressed = Key; Flick = Direction; PressedAt = FPlatformTime::Seconds(); }

#endif
    void CancelInteraction();

    enum EKey : int32 { Delete = 100, Convert = 101, Enter = 102, Close = 103, KanaMode = 104, LatinMode = 105, NumberMode = 106, Shift = 107 };
private:
    friend class FLightBoxKeyboardModesTest;
    friend class FLightBoxTouchKeyboardTest;
    int32 Mode = 0, PaintedMode = INDEX_NONE;
    bool bUpper = false, bPaintedUpper = false;
    FString Glyphs(int32 Key) const;
    TSharedPtr<FLightBoxComposer> Composer;
    FSimpleDelegate OnClose;
    int32 ActivePointer = INDEX_NONE, ActiveUser = INDEX_NONE;
    int32 Pressed = INDEX_NONE, Flick = 0, LastTapKey = INDEX_NONE;
    FVector2D PressPos = FVector2D::ZeroVector;
    double PressedAt = 0, LastTapAt = 0, RepeatAt = 0;
    bool bBarDrag = false, bBarMoved = false;
    float BarScroll = 0; mutable float BarScrollMax = 0;
    uint32 SeenRevision = 0, PaintedRevision = MAX_uint32;
    int32 PaintedPressed = MIN_int32, PaintedFlick = INDEX_NONE;
    float PaintedScroll = -1;
    bool bHadBursts = false;
    double NextPaintAt = 0;
    FVector2D BarStart = FVector2D::ZeroVector; float BarScrollStart = 0;
    mutable TArray<FVector2f> BarHits;  // x, width per candidate (paint fills, input reads)
    struct FBurst { FVector2D At; FString Glyph; double Born; };
    TArray<FBurst> Bursts;
    FSlateRect KeyRect(int32 Key) const;
    int32 HitKey(FVector2D Local) const;
    int32 FlickDirection(FVector2D Delta) const;
    void Release(int32 Key, int32 Direction);
    void Emit(int32 Key, const FString& Glyph);
};

struct FLightBoxSensorReading;

/**
 * Left sensor column: six glass touch cells in one quiet vertical tray
 * with cyan reaction feedback. Tapping a cell switches its sensor.
 */
class SLightBoxSensorDock final : public SLeafWidget
{
public:
    virtual FReply OnTouchStarted(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonDown(G, E); }
    virtual FReply OnTouchEnded(const FGeometry& G, const FPointerEvent& E) override { return OnMouseButtonUp(G, E); }
    SLATE_BEGIN_ARGS(SLightBoxSensorDock) {}
        SLATE_ARGUMENT(TFunction<const FLightBoxSensorReading*()>, Reading)
        SLATE_ARGUMENT(TFunction<bool(int32)>, IsOn)
        SLATE_ARGUMENT(TFunction<void(int32)>, OnToggle)
    SLATE_END_ARGS()
    void Construct(const FArguments& A) { Reading = A._Reading; IsOn = A._IsOn; OnToggle = A._OnToggle; }
    /** Badge order follows ELightBoxSensor: sound, warmth, light, hand, magnet, tilt (IMU). */
    static constexpr int32 Count = 6;
    /** A column down the left edge: one cell per sensor, name under the icon, a gauge under the name. */
    static constexpr float CellW = 128, CellH = 100, Radius = 27;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(CellW + 20, CellH * Count + 20); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle&, bool) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { PressedBadge = ActivePointer = DetailBadge = INDEX_NONE; }
    virtual void Tick(const FGeometry& G, double T, float Dt) override;
    /** Review helper: light up badges as if sensors were reacting. */
#if !UE_BUILD_SHIPPING
    void ShowDemo() { bDemo = true; DetailBadge = 2; }
#endif
private:
    TFunction<const FLightBoxSensorReading*()> Reading;
    TFunction<bool(int32)> IsOn;
    TFunction<void(int32)> OnToggle;
    int32 PressedBadge = INDEX_NONE, ActivePointer = INDEX_NONE, ActiveUser = INDEX_NONE;
    float Level[Count] = {}, Dark = 0, Bright = 0;
    bool Available[Count] = {};
    uint16 LastState = MAX_uint16;
    double NextPaintAt = 0;
    bool bWasActive = false;
    float ClapAge = 10, TapAge[Count] = { 10, 10, 10, 10, 10, 10 };
    /** Long press (held .45 s) shows the live values beside the badge; releasing it never toggles. */
    double PressedAt = 0; int32 DetailBadge = INDEX_NONE, DetailShown = INDEX_NONE; float DetailAppear = 0;
    static constexpr int32 HistoryLength = 60;
    float History[Count][HistoryLength] = {}; int32 HistoryAt = 0; double NextSampleAt = 0;
    FLightBoxSensorReading Snapshot;
#if !UE_BUILD_SHIPPING
    bool bDemo = false;
#endif
    /** Centres share the same baseline; the full cell remains tappable. */
    FVector2D Centre(int32 I) const { return FVector2D(10 + CellW * .5f, 10 + CellH * I + 34); }
    void PaintDetail(FSlateWindowElementList& Out, int32 L, const FGeometry& G, int32 I, bool bOn, float Appear) const;
    int32 HitBadge(FVector2D Local) const;
};
