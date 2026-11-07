#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ALightBoxHUD;
class FLightBoxComposer;
class SLightBoxKeyboard;
class SLightBoxSendButton;
class SLightBoxSensorDock;
struct FButtonStyle;
struct FScrollBoxStyle;
struct FEditableTextBoxStyle;

/** Persistent glass controls. Only interactive regions consume scene gestures. */
class SLightBoxControls : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxControls) {}
        SLATE_ARGUMENT(TWeakObjectPtr<ALightBoxHUD>, HUD)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SLightBoxControls();
    /** Text from an external keyboard; opens the Atelier keyboard on the phone. */
    bool ExternalChar(TCHAR C);
    bool ExternalKey(const FKeyEvent& E);
    bool AcceptsExternalText() const;
    virtual void Tick(const FGeometry&, double, float) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent&) override;
    void OpenMenu();
    void FocusInput();
    bool IsInteracting() const;
    bool IsSceneBusy() const;
    TOptional<float> SceneProgress() const;
    FString SceneProgressText() const;
    /** Review capture: the keyboard open with a composition and a flick in progress. */
#if !UE_BUILD_SHIPPING
    void ReviewKeyboard();
    void ReviewSensors();
#endif
private:
    TWeakObjectPtr<ALightBoxHUD> HUD;
    TSharedPtr<FLightBoxComposer> Composer;
    TSharedPtr<SLightBoxKeyboard> Keyboard;
    TSharedPtr<SLightBoxSendButton> Send;
    TSharedPtr<SLightBoxSensorDock> Dock;
    TSharedPtr<SWidget> Menu;
    TSharedPtr<class SButton> OrbitButton;
    bool bMenuOrbit=false;
    static constexpr float KeyboardScale = 1.2f;
    TSharedPtr<class IInputProcessor> KeyRouter;
    float Scale=1, Progress=0, StatusAge=10;
    float RightInset=0,LeftInset=0,InsetAge=100;
    FVector2D HostSize=FVector2D(1600,900);
    bool bMenu=false, bKeyboard=false;
    float KeyboardOpen=0;
    FString LastStatus;
    void CloseMenu();
    void SetKeyboard(bool bOpen);
    FReply Submit();
    FReply Activate(FName Action);
    float Units() const { return float(HostSize.X/Scale); }
    float NoteWidth() const;
    FString NarrationText() const;
    FString SceneDescription() const;
    /** 0 sending, 1 thinking, 2 assembling the reply; INDEX_NONE otherwise. */
    int32 GenerationStage() const;
    TSharedRef<SWidget> StageStrip();
    float GenerationAge=100;
    float LeftReserve() const { return (bKeyboard || KeyboardOpen > 0 ? 538.f * KeyboardScale + 10.f : 360.f) + LeftInset / Scale; }
    float RightReserve() const { return NoteWidth() + 70.f + RightInset / Scale; }
    float StatusWidth() const { return FMath::Max(120.f, Units() - 2 * FMath::Max(LeftReserve(), RightReserve())); }
    TSharedRef<SWidget> Action(FName Id,const TCHAR* Icon,const TCHAR* Label);
    TSharedRef<SWidget> SettingRow(FName Id,const TCHAR* Icon,const TCHAR* Label);
    FString SettingValue(FName Id) const;
    bool IsOrbiting() const;
    bool CanUse(FName Id) const;
    float ImmersiveAge=0;
};

/** Dialog sheet in the menu's glass: deep navy, cyan-to-pink rim, lit top hairline. */
class SLightBoxSheet : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxSheet) : _Padding(28.f) {} SLATE_ARGUMENT(FMargin, Padding) SLATE_DEFAULT_SLOT(FArguments, Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
};

namespace LightBoxUI
{
    /** Quiet card for list rows inside a sheet. */
    const FSlateBrush* CardBrush(bool Selected=false);
    const FButtonStyle* MenuStyle(bool Selected=false);
    const FScrollBoxStyle* ScrollStyle();
    /** Dialog title on the left, a small round close button on the right. */
    TSharedRef<SWidget> DialogHeader(const FString& Title, float Scale, TFunction<void()> OnClose);
    TSharedRef<SWidget> Icon(const TCHAR* Name);
    const FButtonStyle* ButtonStyle(bool Primary=false);
    const FSlateBrush* PanelBrush();
    const FButtonStyle* MenuButtonStyle();
    const FButtonStyle* TileStyle();
    const FEditableTextBoxStyle* FieldStyle();
    TSharedRef<SWidget> AnimateDialog(TWeakObjectPtr<ALightBoxHUD> HUD,TSharedRef<SWidget> Content);
}
